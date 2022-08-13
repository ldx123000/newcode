#include "vfs.h"
#include "pmm.h"
#include "util/string.h"
#include "spike_interface/spike_utils.h"


//
// alloc a file system abstract
//
struct fs * alloc_fs(int fs_type){
  struct fs * fs = (struct fs *)alloc_page();
  fs->fs_type = fs_type;
  return fs;
}

//
// alloc an inode
//
struct inode * alloc_inode(int in_type){
  struct inode * node = (struct inode *)alloc_page();
  node->in_type = in_type;
  return node;
}

//
// mount a file system to the device named "devname"
//
int vfs_mount(const char * devname, int (*mountfunc)(struct device *dev, struct fs **vfs_fs)){
  int ret;
  struct vfs_dev_t * pdev_t = NULL;
  // 1. find the device entry in vfs_device_list(vdev_list) named devname
  for ( int i = 0; i < MAX_DEV; ++ i ){
    pdev_t = vdev_list[i];
    if ( strcmp(pdev_t->devname, devname) == 0 )
      break;
  }
  if ( pdev_t == NULL )
    panic("vfs_mount: Cannot find the device entry!\n");

  // 2. get the device struct pointer to the device
  struct device * pdevice = pdev_t->dev;

  // 3. mount the specific file system to the device with mountfunc
  if ( ( ret = mountfunc(pdevice, &(pdev_t->fs)) ) == 0 )
    sprint("VFS: file system successfully mounted to %s\n", pdev_t->devname);

  return 0;
}

//
// vfs_get_root
//
int vfs_get_root(const char *devname, struct inode **root_store){
  int ret;
  struct vfs_dev_t * pdev_t = NULL;
  // 1. find the device entry in vfs_device_list(vdev_list) named devname
  for ( int i = 0; i < MAX_DEV; ++ i ){
    pdev_t = vdev_list[i];
    if ( strcmp(pdev_t->devname, devname) == 0 )
      break;
  }
  if ( pdev_t == NULL )
    panic("vfs_get_root: Cannot find the device entry!\n");

  // 2. call fs.fs_get_root()
  struct inode * rootdir = fsop_get_root(pdev_t->fs);
  if ( rootdir == NULL )
    panic("vfs_get_root: failed to get root dir inode!\n");
  *root_store = rootdir;
  return 0;
}

//
// vfs_open
//
int vfs_open(char *path, int flags, struct inode **inode_store){
  // get flags
  int writable = 0;
  int creatable = flags & O_CREATE;
  switch ( flags & MASK_FILEMODE ){ // 获取flags的低2位
    case O_RDONLY: break;
    case O_WRONLY:
    case O_RDWR:
      writable = 1; break;
    default:
      panic("fs_open: invalid open flags!\n");
  }

  // lookup the path, and create an related inode
  int ret;
  struct inode * node;
  ret = vfs_lookup(path, &node);

  // Case 1: if the path belongs to the host device
  if ( ret == -1 ){ // use host device
    int kfd = host_open(path, flags);
    return kfd;
  }
  // Case 2.1: if the path belongs to the PKE device and the file already exists
  //    continue
  // Case 2.2: if the file doesn't exists
  if ( ret == 1 ){
    // Case 2.2.1: the file cannot be created, exit
    if ( creatable == 0 )
      panic("vfs_open: open a non-existent-uncreatable file!\n");
    
    // Case 2.2.2: create a file, and get its inode
    char * filename;    // file name
    struct inode * dir; // dir inode
    // find the directory of the path
    if ( (ret = vfs_lookup_parent(path, &dir, &filename)) != 0 )
      panic("vfs_open: failed to lookup parent!\n");

    // create a file in the directory
    if ( (ret = vop_create(dir, filename, &node)) != 0 )
      panic("vfs_open: failed to create file!\n");
  }

  ++ node->ref;
  sprint("vfs_open: inode ref: %d\n", node->ref);

  *inode_store = node;
  return 0;
}

/*
 * get_device: 根据路径，选择目录的inode存入node_store
 * path format (absolute path):
 *    Case 1: device:path
 *      <e.g.> ramdisk0:/fileinram.txt
 *    Case 2: path
 *      <e.g.> fileinhost.txt
 * @param     subpath:    filename
 * @param     node_store: directory inode
 * @return
 *    -1:     host device
 *    else:   save device root-dir-inode into node_store
 * TODO:      CAN ONLY RESOLVE ONE-LEVEL DIRECTORY
 */
int get_device(char *path, char **subpath, struct inode **node_store) {
  int colon = -1;
  for ( int i = 0; path[i] != '\0'; ++ i ){
    if ( path[i] == ':' ){
      colon = i; break;
    }
  }
  // Case 2: the host device is used by default
  if ( colon < 0 ){
    *subpath    = path;
    *node_store = NULL;
    return -1;
  }
  // Case 1: find the root node of the device in the vdev_list
  // get device name
  path[colon] = '\0';
  char devname[32];
  strcpy(devname, path);
  path[colon] = ':';
  // get file name
  *subpath = path + colon + 2;  // device:/filename
  // get the root dir-inode of [the device named "path"]
  return vfs_get_root(devname, node_store);
}

//
// lookup the path
//
int vfs_lookup(char *path, struct inode **node_store){
  int ret;
  struct inode * dir;
  char * fn;
  ret = get_device(path, &fn, &dir);
  // Case 1: use host device file system
  if ( ret == -1 ){
    * node_store = NULL;
    return -1;
  }
  // Case 2: use PKE file system, dir: device root-dir-inode
  // device:/.../...
  // given root-dir-inode, find the file-inode in $path
  ret = vop_lookup(dir, fn, node_store);
  return ret;
}

/*
 * lookup the directory of the given path
 * @param path:       the path must be in the format of device:path
 * @param node_store: store the directory inode
 * @param fn:         file name
 */
int vfs_lookup_parent(char *path, struct inode **node_store, char **fn){
  int ret;
  struct inode * dir;
  ret = get_device(path, fn, &dir); // get file name

  if ( ret == -1 )
    panic("vfs_lookup_parent: unexpectedly lead to host device!\n");
  
  *node_store = dir;  // get dir inode
  return 0;
}
