#include "hostfs.h"

// ///////////////////////////////////
// Access to the host file system
// ///////////////////////////////////

int host_open(char *pathname, int flags) {
  spike_file_t *f = spike_file_open(pathname, flags, 0);
  if ( (int64)f < 0 )
    return -1;
  int fd = spike_file_dup(f);
  if (fd < 0) {
    spike_file_decref(f);
    return -1;
  }
  return fd;
}

int host_read(int fd, char *buf, uint64 count) {
  spike_file_t *f = spike_file_get(fd);
  return spike_file_read(f, buf, count);
}

int host_write(int fd, char *buf, uint64 count) {
  spike_file_t *f = spike_file_get(fd);
  return spike_file_write(f, buf, count);
}

int host_close(int fd) {
  spike_file_t *f = spike_file_get(fd);
  return spike_file_close(f);
}