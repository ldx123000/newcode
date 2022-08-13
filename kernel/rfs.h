#ifndef _RFS_H_
#define _RFS_H_

#include "vfs.h"
#include "riscv.h"
#include "util/types.h"

#define RFS_TYPE          0
#define RFS_MAGIC         12345
#define RFS_BLKSIZE       PGSIZE
#define RFS_MAX_INODE_NUM 10
#define RFS_MAX_FNAME_LEN 28
#define RFS_NDIRECT       10

// rfs block number
#define RFS_BLKN_SUPER    0
#define RFS_BLKN_INODE    1
#define RFS_BLKN_BITMAP   11
#define RFS_BLKN_FREE     12

struct fs;
struct inode;
struct file;

// file system super block
struct rfs_superblock {
  int magic;         // magic number of the 
  int size;          // Size of file system image (blocks)
  int nblocks;       // Number of data blocks
  int ninodes;       // Number of inodes.
};

// inode on disk
struct rfs_dinode{
  int size;               // size of the file (in bytes)
  int type;               // one of T_FREE, T_DEV, T_FILE, T_DIR
  int nlinks;             // # of hard links to this file
  int blocks;             // # of blocks
  int addrs[RFS_NDIRECT]; // direct blocks
};

// directory entry
struct rfs_direntry {
  int inum;                     // inode number
  char name[RFS_MAX_FNAME_LEN]; // file name
};

// filesystem for rfs
struct rfs_fs {
  struct rfs_superblock super;  // rfs_superblock
  struct device * dev;          // device mounted on
  int * freemap;                // blocks in use are marked 1
  bool dirty;                   // true if super/freemap modified
  void * buffer;                // buffer for io
};

// /* filesystem for sfs */
// struct sfs_fs {
//   struct sfs_super super;                         /* on-disk superblock */
//   struct device *dev;                             /* device mounted on */
//   struct bitmap *freemap;                         /* blocks in use are mared 0 */
//   bool super_dirty;                               /* true if super/freemap modified */
//   void *sfs_buffer;                               /* buffer for non-block aligned io */
//   semaphore_t fs_sem;                             /* semaphore for fs */
//   semaphore_t io_sem;                             /* semaphore for io */
//   semaphore_t mutex_sem;                          /* semaphore for link/unlink and rename */
//   list_entry_t inode_list;                        /* inode linked-list */
//   list_entry_t *hash_list;                        /* inode hash linked-list */
// };

void rfs_init(void);
int rfs_mount(const char *devname);
int rfs_do_mount(struct device *dev, struct fs **vfs_fs);

int rfs_r1block(struct rfs_fs *rfs, int blkno);
int rfs_w1block(struct rfs_fs *rfs, int blkno);

int rfs_sync(struct fs *fs);
struct inode * rfs_get_root(struct fs *fs);
int rfs_unmount(struct fs *fs);
void rfs_cleanup(struct fs *fs);

// void lock_rfs_fs(struct rfs_fs *rfs);
// void lock_rfs_io(struct rfs_fs *rfs);
// void unlock_rfs_fs(struct rfs_fs *rfs);
// void unlock_rfs_io(struct rfs_fs *rfs);

// int rfs_rblock(struct rfs_fs *rfs, void *buf, int32 blkno, int32 nblks);
// int rfs_wblock(struct rfs_fs *rfs, void *buf, int32 blkno, int32 nblks);
// int rfs_rbuf(struct rfs_fs *rfs, void *buf, size_t len, int32 blkno, off_t offset);
// int rfs_wbuf(struct rfs_fs *rfs, void *buf, size_t len, int32 blkno, off_t offset);
// int rfs_sync_super(struct rfs_fs *rfs);
// int rfs_sync_freemap(struct rfs_fs *rfs);
// int rfs_clear_block(struct rfs_fs *rfs, int32 blkno, int32 nblks);

// int rfs_create_dinode(struct rfs_fs *prfs, )
int rfs_load_dinode(struct rfs_fs *prfs, int ino, struct inode **node_store);
int rfs_create_inode(struct rfs_fs *prfs, struct rfs_dinode * din, int ino, struct inode **node_store);
int rfs_create_dirblock(struct rfs_fs *prfs, int ino, char * name);

const struct inode_ops * rfs_get_ops(int type);

#endif
