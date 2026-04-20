#include "userprog/syscall.h"
#include <console.h>
#include <stdio.h>
#include <syscall-nr.h>
#include "devices/shutdown.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
check_user_vaddr (const void *vaddr)
{
  if (vaddr == NULL || !is_user_vaddr (vaddr))
    sys_exit (-1);
}

static void
syscall_handler (struct intr_frame *f)
{
  uint32_t *esp = (uint32_t *) f->esp;
  check_user_vaddr (esp);

  switch (esp[0])
    {
    case SYS_EXIT:
      check_user_vaddr (esp + 1);
      sys_exit ((int) esp[1]);
      break;

    case SYS_HALT:
      shutdown_power_off ();
      break;

    case SYS_WRITE:
      {
        int fd;
        const char *buffer;
        unsigned size;

        check_user_vaddr (esp + 1); //fd
        check_user_vaddr (esp + 2); //buffer
        check_user_vaddr (esp + 3); //size

        fd = (int) esp[1];
        buffer = (const char *) esp[2];
        size = (unsigned) esp[3];

        if (size > 0)
          {
            check_user_vaddr (buffer);
            check_user_vaddr (buffer + size - 1);
          }

        if (fd == 1)
          {
            putbuf (buffer, size);
            f->eax = size;
          }
        else
          f->eax = -1;
      }
      break;

    default:
      sys_exit (-1);
      break;
    }
}

void
sys_exit (int status)
{
  struct thread *cur = thread_current ();
  cur->exit_status = status;
  printf ("%s: exit(%d)\n", cur->name, status);
  thread_exit ();
}
