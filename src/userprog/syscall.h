#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"

extern struct lock filesys_lock;

void syscall_init (void);
void sys_exit (int status);
void syscall_close_all_files (void);
void syscall_munmap_all (void);

#endif /**< userprog/syscall.h */
