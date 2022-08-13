/*
 * header file to be used by applications.
 */

#include "util/types.h"

int printu(const char *s, ...);
int exit(int code);
void* naive_malloc();
void naive_free(void* va);
int fork();
int wait(int pid);
void yield();
int getlineu(char * dst, int size);
int exec(char * path, char ** argv);
int getinfo();

// file
int open(const char *pathname, int flags);
int create(const char *pathname);
int read(int fd, void *buf, uint64 count);
int write(int fd, void *buf, uint64 count);
int close(int fd);