#include "user_lib.h"
#include "util/types.h"
#include "util/string.h"

int main(int argc, char *argv[]) {
  int fd;
  int MAXBUF = 512;
  char buf[MAXBUF];
  
  printu("\n======== Case 1 ========\n");
  printu("read: \"hostfile.txt\" (from host device)\n");
  printu("========================\n");

  fd = open("hostfile.txt", 0);
  printu("file descriptor fd: %d\n", fd);
  read(fd, buf, MAXBUF);
  printu("read content: \n%s\n", buf);

  printu("\n======== Case 2 ========\n");
  printu("write: \"ramdisk0:/ramfile\"\n");
  printu("========================\n");

  fd = open("ramdisk0:/ramfile", O_RDWR|O_CREATE);
  printu("file descriptor fd: %d\n", fd);
  write(fd, buf, strlen(buf)+1);
  printu("write content: \n%s\n", buf);

  printu("\n======== Case 3 ========\n");
  printu("read: \"ramdisk0:/ramfile\"\n");
  printu("========================\n");
  
  fd = open("ramdisk0:/ramfile", O_RDWR);
  printu("file descriptor fd: %d\n", fd);
  read(fd, buf, MAXBUF);
  printu("read content: \n%s\n", buf);

  printu("\nAll tests passed!\n\n");

  exit(0);
  return 0;
}
