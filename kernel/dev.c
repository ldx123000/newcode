#include "dev.h"
#include "vfs.h"
#include "pmm.h"
#include "riscv.h"
#include "util/types.h"
#include "util/string.h"
#include "spike_interface/spike_utils.h"

// global RAMDISK0 BASE ADDRESS
void * RAMDISK0_BASE_ADDR;

//
// 从 buffer 中获取数据，写入第 blkno 块
// buffer -> RAM Disk0[blkno] (data size: 1 block)
//
int ramdisk0_input(void * buffer, int blkno){
  if ( blkno < 0 || blkno >= RAMDISK0_BLOCK )
    panic("RAM Disk0: input block No %d out of range!\n", blkno);
  void * dst = (void *)((uint64)RAMDISK0_BASE_ADDR + blkno * RAMDISK0_BSIZE);
  memcpy(dst, buffer, RAMDISK0_BSIZE);
  return 0;
}

//
// 从第 blkno 块获取数据，写入 buffer 中
// RAM Disk0[blkno] -> buffer (data size: 1 block)
//
int ramdisk0_output(void * buffer, int blkno){
  if ( blkno < 0 || blkno >= RAMDISK0_BLOCK )
    panic("RAM Disk0: input block No out of range!\n");
  void * src = (void *)((uint64)RAMDISK0_BASE_ADDR + blkno * RAMDISK0_BSIZE);
  memcpy(buffer, src, RAMDISK0_BSIZE);
  return 0;
}

/*
  * Initialize the structure of the device in the vfs device list
  * 初始化设备在虚拟文件系统设备列表里的结点 pdev
  * struct vfs_dev_t (vfs.h):
  *      devname:  the name of the device
  *      listidx:  the index for the device in the vfs device list
  *      dev:      the pointer to the device (struct device * in dev.h)
  *      fs:       the file system mounted to the device, initialized by vfs_mount (vfs.h)
  */
void dev_init_ramdisk0(void) {
  struct vfs_dev_t * pdev = (struct vfs_dev_t *)alloc_page();
  // 1. set the device name and index
  pdev->devname   = "ramdisk0";
  pdev->listidx   = RAMDISK0;
  // 2. set struct device
  /*
   * Initialize the device struct
   * 初始化设备信息结构体
   * struct device (dev.h):
   *        d_blocks:     the number of blocks of the device
   *        d_blocksize:  the blocksize (bytes) per block
   *    function:
   *        d_input:      device input funtion
   *        d_output:     device output funtion
   */
  struct device * pd = (struct device *)alloc_page();
  pd->d_blocks    = RAMDISK0_BLOCK;
  pd->d_blocksize = RAMDISK0_BSIZE;
  pd->d_input     = ramdisk0_input;
  pd->d_output    = ramdisk0_output;
  pdev->dev = pd;

  // 3. add the device node pointer to the vfs_device_list
  vdev_list[RAMDISK0] = pdev;
  sprint("RAM Disk0: base address of RAM Disk0 is: %p\n", RAMDISK0_BASE_ADDR);
}

//
// alloc space (RAMDISK0_BLOCK*RAMDISK0_BSIZE Bytes) for the RAM Disk
//
void init_ramdisk0(void){
  int alloc_times = (RAMDISK0_BLOCK*RAMDISK0_BSIZE-1) / PGSIZE + 1;
  for ( int i = 0; i < alloc_times; ++ i ){
    RAMDISK0_BASE_ADDR = alloc_page();
  }
}

//
// Initialize devices
//
void dev_init(void) {
  init_ramdisk0();      // alloc space for RAM Disk0
  dev_init_ramdisk0();  // add the device entry to vfs_dev_list
}

//
// Function table for device inodes.
//
// static const struct inode_ops dev_node_ops = {
//   .vfs_open                       = dev_open,
//   .vfs_close                      = dev_close,
//   .vfs_read                       = dev_read,
//   .vfs_write                      = dev_write,
//   .vfs_fstat                      = dev_fstat,
//   .vfs_ioctl                      = dev_ioctl,
//   .vfs_gettype                    = dev_gettype,
//   .vfs_tryseek                    = dev_tryseek,
//   .vfs_lookup                     = dev_lookup,
// };

// //
// // Create an inode for device
// //
// struct inode *dev_create_inode(void){
//   struct inode * node = alloc_inode(RAMDISK0, T_DEV, &dev_node_ops, NULL);
//   return node;
// }
