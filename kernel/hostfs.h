#ifndef _HOSTFS_H_
#define _HOSTFS_H_

#include "util/types.h"
#include "spike_interface/spike_file.h"


// ///////////////////////////////////
// Access to the host file system
// ///////////////////////////////////

int host_open(char *pathname, int flags);
int host_read(int fd, char *buf, uint64 count);
int host_write(int fd, char *buf, uint64 count);
int host_close(int fd);

#endif
