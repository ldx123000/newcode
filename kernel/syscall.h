/*
 * define the syscall numbers of PKE OS kernel.
 */
#ifndef _SYSCALL_H_
#define _SYSCALL_H_

// syscalls of PKE OS kernel. append below if adding new syscalls.
#define SYS_user_base 64
#define SYS_user_print (SYS_user_base + 0)
#define SYS_user_exit (SYS_user_base + 1)
#define SYS_user_allocate_page (SYS_user_base + 2)
#define SYS_user_free_page (SYS_user_base + 3)
#define SYS_user_fork (SYS_user_base + 4)
#define SYS_user_yield (SYS_user_base + 5)

#define SYS_user_wait (SYS_user_base + 14)
#define SYS_user_getline (SYS_user_base + 15)
#define SYS_user_exec (SYS_user_base + 16)

#define SYS_user_open (SYS_user_base + 17)
// #define SYS_user_create (SYS_user_base + 22)
#define SYS_user_read (SYS_user_base + 18)
#define SYS_user_write (SYS_user_base + 19)
#define SYS_user_close (SYS_user_base + 20)

#define SYS_user_getinfo (SYS_user_base + 21)

long do_syscall(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7);

#endif
