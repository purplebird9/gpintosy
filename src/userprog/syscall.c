#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  // 2.1
  /* 获取系统调用号 */
  int syscall_num = 0;
  if (f != NULL && f->esp != NULL)
    syscall_num = *(int *)(f->esp);

  switch (syscall_num) {
    case SYS_EXIT: {
      /* 取参数 */
      int status = *((int *)f->esp + 1);
      thread_current()->exit_status = status;
      thread_exit();
      break;
    }
    default:
      printf ("system call!\n");
      thread_exit ();
      break;
  }
}
