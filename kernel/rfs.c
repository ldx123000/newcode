#include "vfs.h"
#include "rfs.h"
#include "dev.h"
#include "pmm.h"
#include "util/string.h"
#include "spike_interface/spike_utils.h"

// global RAMDISK0 BASE ADDRESS
extern void * RAMDISK0_BASE_ADDR;

//
// rfs_init: called by fs_init
//
void rfs_init(void){
  int ret;
  if ( (ret = rfs_mount("ramdisk0")) != 0 )
    panic("failed: rfs: rfs_mount: %d.\n", ret);
}

int rfs_mount(const char * devname) {
  return vfs_mount(devname, rfs_do_mount);
}

/*
 * Mount VFS(struct fs)-RFS(struct rfs_fs)-RAM Device(struct device)
 *
 * ******** RFS MEM LAYOUT (112 BLOCKS) ****************
 *   superblock  |  inodes  |  bitmap  |  free blocks  *
 *     1 block   |    10    |     1    |     100       *
 * *****************************************************
 */
int rfs_do_mount(struct device * dev, struct fs ** vfs_fs){
  /*
   * 1. alloc fs structure
   * struct fs (vfs.h):
   *        union { struct rfs_fs rfs_info } fs_info: rfs_fs
   *        fs_type:  rfs (=0)
   *    function:
   *        fs_sync
   *        fs_get_root
   *        fs_unmount
   *        fs_cleanup
   */
  struct fs * fs = alloc_fs(RFS_TYPE); // set fs_type
  /*
   * 2. alloc rfs_fs structure
   * struct rfs_fs (rfs.h):
   *      super:    rfs_superblock
   *      dev:      the pointer to the device (struct device * in dev.h)
   *      freemap:  a block of bitmap (free: 0; used: 1)
   *      dirty:    true if super/freemap modified
   *      buffer:   buffer for non-block aligned io
   */
  struct rfs_fs * prfs = fsop_info(fs, RFS_TYPE);

  // 2.1. set [prfs->dev] & [prfs->dirty]
  prfs->dev   = dev;
  prfs->dirty = 0;

  // 2.2. alloc [io buffer] (1 block) for rfs
  prfs->buffer = alloc_page();

  // 2.3. usually read [superblock] (1 block) from device to prfs->buffer
  //      BUT for volatile RAM Disk, there is no superblock remaining on disk
  int ret;

  //      build a new superblock
  prfs->super.magic   = RFS_MAGIC;
  prfs->super.size    = 1 + RFS_MAX_INODE_NUM + 1 + RFS_MAX_INODE_NUM*RFS_NDIRECT;
  prfs->super.nblocks = RFS_MAX_INODE_NUM * RFS_NDIRECT;  // only direct index blocks
  prfs->super.ninodes = RFS_MAX_INODE_NUM;

  //      write the superblock to RAM Disk0
  memcpy(prfs->buffer, &(prfs->super), RFS_BLKSIZE);      // set io buffer
  if ( (ret = rfs_w1block(prfs, RFS_BLKN_SUPER)) != 0 )   // write to device
    panic("RFS: failed to write superblock!\n");

  // 2.4. similarly, build an empty [bitmap] and write to RAM Disk0
  prfs->freemap = alloc_page();
  memset(prfs->freemap, 0, RFS_BLKSIZE);
  prfs->freemap[0] = 1;   // the first data block is used for root directory

  //      write the bitmap to RAM Disk0
  memcpy(prfs->buffer, prfs->freemap, RFS_BLKSIZE);       // set io buffer
  if ( (ret = rfs_w1block(prfs, RFS_BLKN_BITMAP)) != 0 )  // write to device
    panic("RFS: failed to write bitmap!\n");

  // 2.5. build inodes (inode -> prfs->buffer -> RAM Disk0)
  struct rfs_dinode * pinode = (struct rfs_dinode *)prfs->buffer;
  pinode->size   = 0;
  pinode->type   = T_FREE;
  pinode->nlinks = 0;
  pinode->blocks = 0;

  //      write disk inodes to RAM Disk0
  for ( int i = 1; i < prfs->super.ninodes; ++ i )
    if ( (ret = rfs_w1block(prfs, RFS_BLKN_INODE + i)) != 0 )
      panic("RFS: failed to write inodes!\n");

  //      build root directory inode (ino = 0)
  pinode->size     = sizeof(struct rfs_direntry);
  pinode->type     = T_DIR;
  pinode->nlinks   = 1;
  pinode->blocks   = 1;
  pinode->addrs[0] = RFS_BLKN_FREE;

  //      write root directory inode to RAM Disk0
  if ( (ret = rfs_w1block(prfs, RFS_BLKN_INODE)) != 0 )
    panic("RFS: failed to write root inode!\n");
  
  // 2.6. write root directory block
  rfs_create_dirblock(prfs, RFS_BLKN_INODE, "/");

  //      write root directory block to RAM Disk0
  if ( (ret = rfs_w1block(prfs, RFS_BLKN_FREE)) != 0 )
    panic("RFS: failed to write root inode!\n");
  
  // 3. mount functions
  fs->fs_sync     = rfs_sync;
  fs->fs_get_root = rfs_get_root;
  fs->fs_unmount  = rfs_unmount;
  fs->fs_cleanup  = rfs_cleanup;

  *vfs_fs = fs;

  return 0;
}

int rfs_r1block(struct rfs_fs *rfs, int blkno){
  // dop_output: ((dev)->d_output(buffer, blkno))
  return dop_output(rfs->dev, rfs->buffer, blkno);
}

int rfs_w1block(struct rfs_fs *rfs, int blkno){
  // dop_input:  ((dev)->d_input(buffer, blkno))
  return dop_input(rfs->dev, rfs->buffer, blkno);
}

int rfs_sync(struct fs *fs){
  panic("RFS: rfs_sync unimplemented!\n");
  return 0;
}

//
// Return root inode of filesystem.
//
struct inode * rfs_get_root(struct fs * fs){
  struct inode * node;
  // get rfs pointer
  struct rfs_fs * prfs = fsop_info(fs, RFS_TYPE);
  // load the root inode
  int ret;
  if ( (ret = rfs_load_dinode(prfs, RFS_BLKN_INODE, &node)) != 0 )
    panic("RFS: failed to load root inode!\n");
  return node;
}

//
// RFS: load inode from disk (ino: inode number)
//
int rfs_load_dinode(struct rfs_fs *prfs, int ino, struct inode **node_store){
  struct inode * node;
  int ret;
  // load inode[ino] from disk -> rfs buffer -> dnode
  if ( (ret = rfs_r1block(prfs, RFS_BLKN_INODE)) != 0 )
    panic("RFS: failed to read inode!\n");
  struct rfs_dinode * dnode = (struct rfs_dinode *)prfs->buffer;
  
  // build inode according to dinode
  if ( (ret = rfs_create_inode(prfs, dnode, ino, &node)) != 0 )
    panic("RFS: failed to create inode from dinode!\n");
  *node_store = node;
  return 0;
}

/*
 * Create an inode in kernel according to din
 * 
 */
int rfs_create_inode(struct rfs_fs *prfs, struct rfs_dinode * din, int ino, struct inode **node_store){
  // 1. alloc an vfs-inode in memory
  struct inode * node = alloc_inode(RFS_TYPE);
  // 2. copy disk-inode data
  struct rfs_dinode * dnode = vop_info(node, RFS_TYPE);
  dnode->size = din->size;
  dnode->type = din->type;
  dnode->nlinks = din->nlinks;
  dnode->blocks = din->blocks;
  for ( int i = 0; i < RFS_NDIRECT; ++ i )
    dnode->addrs[i] = din->addrs[i];

  // 3. set inum, ref, in_fs, in_ops
  node->inum   = ino;
  node->ref    = 0;
  node->in_fs  = (struct fs *)prfs;
  node->in_ops = rfs_get_ops(dnode->type);

  *node_store = node;
  return 0;
}

//
// create a directory block
//
int rfs_create_dirblock(struct rfs_fs *prfs, int ino, char * name){
  struct rfs_direntry * de = (struct rfs_direntry *)prfs->buffer;
  de->inum = ino;
  strcpy(de->name, name);
  return 0;
}

int rfs_unmount(struct fs *fs){
  return 0;
}

void rfs_cleanup(struct fs *fs){
  return;
}

int rfs_opendir(struct inode *node, int open_flags){
  int fd = 0;
  return fd;
}

int rfs_openfile(struct inode *node, int open_flags){
  return 0;
}

int rfs_close(struct inode *node){
  int fd = 0;
  return fd;
}

int rfs_fstat(struct inode *node, struct fstat *stat){
  struct rfs_dinode * dnode = vop_info(node, RFS_TYPE);
  stat->st_mode   = dnode->type;
  stat->st_nlinks = dnode->nlinks;
  stat->st_blocks = dnode->blocks;
  stat->st_size   = dnode->size;
  return 0;
}

/*
 * lookup the path in the given directory
 * @param node: directory node
 * @param path: filename
 * @param node_store: store the file inode
 * @return
 *    0: the file is found
 *    1: the file is not found, need to be created
 */
int rfs_lookup(struct inode *node, char *path, struct inode **node_store){
  struct rfs_dinode * dnode = vop_info(node, RFS_TYPE);
  struct rfs_fs * prfs = fsop_info(node->in_fs, RFS_TYPE);

  // 读入一个dir block，遍历direntry，查找filename
  struct rfs_direntry * de = NULL;

  int nde = dnode->size / sizeof(struct rfs_direntry);
  int maxde = RFS_BLKSIZE / sizeof(struct rfs_direntry);

  for ( int i = 0; i < nde; ++ i ){
    if ( i % maxde == 0 ){  // 需要切换block基地址
      rfs_r1block(prfs, dnode->addrs[i/maxde]); // 读入第i/maxde个block
      de = (struct rfs_direntry *)prfs->buffer;
    }
    if ( strcmp(de->name, path) == 0 ){ // 找到文件
      rfs_r1block(prfs, de->inum);      // 读入文件的dinode块到buffer
      rfs_create_inode(prfs, (struct rfs_dinode *)prfs->buffer, de->inum, node_store);
      return 0;
    }
    ++ de;
  }
  return 1; // no file inode, need to be created
}

int rfs_alloc_block(struct rfs_fs * rfs){
  int freeblkno = -1;
  for ( int i = 0; i < rfs->super.nblocks; ++ i ){
    if ( rfs->freemap[i] == 0 ){  // find a free block
      rfs->freemap[i] = 1;
      freeblkno = RFS_BLKN_FREE + i;
      // sprint("rfs_alloc_block: blkno: %d\n", freeblkno);
      break;
    }
  }
  if ( freeblkno == -1 )
    panic("rfs_alloc_block: no free block!\n");
  return freeblkno;
}

/*
 * create a file named "name" in the given directory (node)
 * @param node: dir node
 * @param name: file name
 * @param node_store: store the file inode
 */
int rfs_create(struct inode *dir, const char *name, struct inode **node_store){
  // 1. build dinode, inode for a new file
  // alloc a disk-inode on disk
  struct fs * fs = dir->in_fs;
  struct rfs_fs * rfs = fsop_info(fs, RFS_TYPE);

  int blkno = 0;  // inode blkno = ino
  struct rfs_dinode * din;

  for ( int i = 0; i < RFS_MAX_INODE_NUM; ++ i ){
    blkno = RFS_BLKN_INODE + i;
    rfs_r1block(rfs, blkno);      // read dinode block
    din = (struct rfs_dinode *)rfs->buffer;

    if ( din->type == T_FREE )    // find a free inode block
      break;
  }
  int ino = blkno;

  // build a dinode for the file
  din->size     = 0;
  din->type     = T_FILE;
  din->nlinks   = 1;
  din->blocks   = 1;
  din->addrs[0] = rfs_alloc_block(rfs);// find a free block

  // write the dinode to disk
  rfs_w1block(rfs, blkno);

  // build inode according to dinode
  int ret;
  if ( (ret = rfs_create_inode(rfs, din, ino, node_store)) != 0 )
    panic("rfs_create: failed to create inode from dinode!\n");
  
  struct inode * rino = *node_store;
  struct rfs_dinode * rfs_din = vop_info(rino, RFS_TYPE);

  // 2. add a directory entry in the dir file
  // 2.1. 修改dir-inode-block on disk
  rfs_r1block(rfs, dir->inum);
  din = (struct rfs_dinode *)rfs->buffer;
  din->size += sizeof(struct rfs_direntry);
  rfs_w1block(rfs, dir->inum);
  
  // 2.2. 修改dir-data-block
  din = vop_info(dir, RFS_TYPE);
  // din->size += sizeof(struct rfs_direntry);

  // 接在din->size后面
  blkno = din->addrs[din->size / RFS_BLKSIZE];

  // 读入数据块
  rfs_r1block(rfs, blkno);

  // 添加内容(ino, name)
  uint64 addr = (uint64)rfs->buffer + din->size % RFS_BLKSIZE;
  struct rfs_direntry * de = (struct rfs_direntry *)addr;
  de->inum = ino;
  strcpy(de->name, name);

  // 写回设备
  rfs_w1block(rfs, blkno);

  return 0;
}

int rfs_read(struct inode *node, char *buf, uint64 len){
  
  struct rfs_dinode * din = vop_info(node, RFS_TYPE);

  if ( din->size < len )
    len = din->size;

  sprint("rfs_read: len: %d\n", len);
  char buffer[len+1];

  struct fs * fs = node->in_fs;
  struct rfs_fs * rfs = fsop_info(fs, RFS_TYPE);

  int readtime = len / RFS_BLKSIZE;
  int remain   = len % RFS_BLKSIZE;
  if ( remain > 0 )
    ++ readtime;
  sprint("rfs_read: read %d times from disk. \n", readtime);

  int offset = 0;
  int i = 0;
  // 读入完整的block
  for ( ; i < readtime - 1; ++ i ){
    rfs_r1block(rfs, din->addrs[i]);
    memcpy(buffer+offset, rfs->buffer, RFS_BLKSIZE);
    offset += RFS_BLKSIZE;
  }
  // 读入最后的block
  rfs_r1block(rfs, din->addrs[i]);
  memcpy(buffer+offset, rfs->buffer, remain);

  buffer[len] = '\0';

  strcpy(buf, buffer);
  return 0;
}

int rfs_write(struct inode *node, char *buf, uint64 len){
  struct fs * fs = node->in_fs;
  struct rfs_fs * rfs = fsop_info(fs, RFS_TYPE);

  // write inode
  struct rfs_dinode * din = vop_info(node, RFS_TYPE);
  din->size = (strlen(buf)+1) * sizeof(char);

  // write data
  int writetime = len / RFS_BLKSIZE;
  int remain    = len % RFS_BLKSIZE;
  if ( remain > 0 )
    ++ writetime;
  sprint("rfs_write: write %d times from disk. \n", writetime);

  int offset = 0;
  int i = 0;
  for ( ; i < writetime; ++ i ){
    // find a block
    if ( i == din->blocks ){
      din->addrs[i] = rfs_alloc_block(rfs);
      ++ din->blocks;
    }
    if ( i == writetime - 1 )
      memcpy(rfs->buffer, buf+offset, RFS_BLKSIZE);
    else
      memcpy(rfs->buffer, buf+offset, remain);
    rfs_w1block(rfs, din->addrs[i]);
    offset += RFS_BLKSIZE;
  }

  int nblocks = din->blocks;
  // write disk-inode (size, blocks)
  rfs_r1block(rfs, node->inum);
  din = (struct rfs_dinode *)rfs->buffer; // dinode
  din->size   = (strlen(buf)+1) * sizeof(char);
  din->blocks = nblocks;
  rfs_w1block(rfs, node->inum);
  return 0;
}

// The sfs specific DIR operations correspond to the abstract operations on a inode.
static const struct inode_ops rfs_node_dirops = {
  .vop_open               = rfs_opendir,
  .vop_close              = rfs_close,
  .vop_fstat              = rfs_fstat,
  // .vop_fsync                      = sfs_fsync,
  // .vop_namefile                   = sfs_namefile,
  // .vop_getdirentry                = sfs_getdirentry,
  // .vop_reclaim                    = sfs_reclaim,
  // .vop_gettype                    = sfs_gettype,
  .vop_lookup             = rfs_lookup,
  .vop_create             = rfs_create,
};

// The sfs specific FILE operations correspond to the abstract operations on a inode.
static const struct inode_ops rfs_node_fileops = {
  .vop_open               = rfs_openfile,
  .vop_close              = rfs_close,
  .vop_read               = rfs_read,
  .vop_write              = rfs_write,
  .vop_fstat              = rfs_fstat,
  // .vop_fsync                      = sfs_fsync,
  // .vop_reclaim                    = sfs_reclaim,
  // .vop_gettype                    = sfs_gettype,
  // .vop_tryseek                    = sfs_tryseek,
  // .vop_truncate                   = sfs_truncfile,
};

const struct inode_ops * rfs_get_ops(int type){
  switch (type) {
    case T_DIR:
      return &rfs_node_dirops;
    case T_FILE:
      return &rfs_node_fileops;
  }
  panic("RFS: invalid file type: %d\n", type);
}