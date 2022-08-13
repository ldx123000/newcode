#ifndef _FILE_H_
#define _FILE_H_

#include "process.h"
#include "util/types.h"
#include "spike_interface/spike_file.h"

#define MASK_FILEMODE 0x003

// //////////////////////////////////////////////////
// File operation interfaces provided to the process
// //////////////////////////////////////////////////

int do_open(char *pathname, int flags);
int do_read(int fd, char *buf, uint64 count);
int do_write(int fd, char *buf, uint64 count);
int do_close(int fd);

// ///////////////////////////////////
// Access to the RAM Disk
// ///////////////////////////////////

void fs_init(void);

struct file {
  enum {
    FD_HOST, FD_NONE, FD_OPENED, FD_CLOSED,
  } status;
  int readable;
  int writable;
  int fd;             // file descriptor
  int off;            // offset
  int ref;            // reference count
  struct inode *node; // inode
};

struct fstat {
  int st_mode;        // protection mode and file type
  int st_nlinks;      // # of hard links
  int st_blocks;      // # of blocks file is using
  int st_size;        // file size (bytes)
};

// void fd_array_init(struct file *fd_array);
// void fd_array_open(struct file *file);
// void fd_array_close(struct file *file);
// void fd_array_dup(struct file *to, struct file *from);
// bool file_testfd(int fd, bool readable, bool writable);

// ///////////////////////////////////
// Files struct in PCB
// ///////////////////////////////////

struct files_struct {
  struct inode * cwd;           // inode of current working directory
  struct file ofile[MAX_FILES]; // opened files array
  int nfile;                    // the number of opened files
};

struct files_struct * files_create(void);
void files_destroy(struct files_struct * pfiles);

// current running process
extern process* current;

#endif
