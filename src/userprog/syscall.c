#include "userprog/syscall.h"
#include <console.h>
#include <debug.h>
#include <stdio.h>
#include <syscall-nr.h>
#include "devices/shutdown.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

static void syscall_handler (struct intr_frame *);
static void check_user_vaddr (const void *vaddr);
static void copy_in (void *dst_, const void *usrc_, size_t size);
static uint32_t read_u32 (const uint32_t *uaddr);
static int get_user (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte) UNUSED;

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
copy_in (void *dst_, const void *usrc_, size_t size)
{
  uint8_t *dst = dst_;
  const uint8_t *usrc = usrc_;
  size_t i;

  for (i = 0; i < size; i++)
    {
      int byte;

      check_user_vaddr (usrc + i);
      byte = get_user (usrc + i);
      if (byte == -1)
        sys_exit (-1);
      dst[i] = (uint8_t) byte;
    }
}

static uint32_t
read_u32 (const uint32_t *uaddr)
{
  uint32_t value;
  copy_in (&value, uaddr, sizeof value);
  return value;
}

static void
syscall_handler (struct intr_frame *f)
{
  uint32_t *esp = (uint32_t *) f->esp;
  uint32_t syscall_nr;

  check_user_vaddr (esp);
  syscall_nr = read_u32 (esp);

  switch (syscall_nr)
    {
    case SYS_EXIT:
      sys_exit ((int) read_u32 (esp + 1));
      break;

    case SYS_HALT:
      shutdown_power_off ();
      break;

    case SYS_WRITE:
      {
        int fd;
        const char *buffer;
        unsigned size;
        unsigned bytes_written;
        uint8_t bounce[256];

        fd = (int) read_u32 (esp + 1);
        buffer = (const char *) read_u32 (esp + 2);
        size = (unsigned) read_u32 (esp + 3);

        if (fd == 1)
          {
            for (bytes_written = 0; bytes_written < size; )
              {
                size_t chunk_size = size - bytes_written;
                if (chunk_size > sizeof bounce)
                  chunk_size = sizeof bounce;

                copy_in (bounce, buffer + bytes_written, chunk_size);
                putbuf ((char *) bounce, chunk_size);
                bytes_written += chunk_size;
              }
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



/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}
