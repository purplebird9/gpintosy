#include "userprog/syscall.h"
#include <console.h>
#include <debug.h>
#include <round.h>
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"


// LAB 2.4
/* 全局 filesys_lock，把文件系统访问串行化 */
struct lock filesys_lock;

// LAB 2.4: fd结构体
struct file_descriptor
  {
    int fd;
    struct file *file;
    struct list_elem elem;
  };


static void syscall_handler (struct intr_frame *);
static void check_user_vaddr (const void *uaddr);
static void check_user_buffer (const void *buffer, size_t size);
static void copy_in (void *dst, const void *usrc, size_t size);
static void copy_out (void *udst, const void *src, size_t size);
static char *copy_in_string (const char *us);
static uint32_t read_u32 (const uint32_t *uaddr);
static int get_user (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);
static struct file_descriptor *lookup_fd (int fd);
static struct file *lookup_file (int fd);
static int fd_allocate (struct file *file);
static void fd_close (int fd);
static bool sys_create (const char *file, unsigned initial_size);
static bool sys_remove (const char *file);
static int sys_open (const char *file);
static int sys_filesize (int fd);
static int sys_read (int fd, void *buffer, unsigned size);
static int sys_write (int fd, const void *buffer, unsigned size);
static void sys_seek (int fd, unsigned position);
static unsigned sys_tell (int fd);
static void sys_close (int fd);
static tid_t sys_exec (const char *cmd_line);
static int sys_wait (tid_t pid);

void
syscall_init (void)
{
  lock_init (&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void
sys_exit (int status)
{
  struct thread *cur = thread_current ();

  cur->exit_status = status;
  printf ("%s: exit(%d)\n", cur->name, status);
  thread_exit ();
}

void
syscall_close_all_files (void)
{
  struct thread *cur = thread_current ();

  while (!list_empty (&cur->fd_list))
    {
      struct list_elem *e = list_pop_front (&cur->fd_list);
      struct file_descriptor *fd = list_entry (e, struct file_descriptor, elem);
      file_close (fd->file);
      free (fd);
    }
}

static void
check_user_vaddr (const void *uaddr)
{
  if (uaddr == NULL || !is_user_vaddr (uaddr)
      || pagedir_get_page (thread_current ()->pagedir, uaddr) == NULL)
    sys_exit (-1);
}

static void
check_user_buffer (const void *buffer, size_t size)
{
  const uint8_t *uaddr = buffer;
  size_t i;

  if (size == 0)
    return;

  check_user_vaddr (uaddr);
  check_user_vaddr (uaddr + size - 1);

  for (i = 0; i < size; i += PGSIZE)
    check_user_vaddr (uaddr + i);
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

static void
copy_out (void *udst_, const void *src_, size_t size)
{
  uint8_t *udst = udst_;
  const uint8_t *src = src_;
  size_t i;

  for (i = 0; i < size; i++)
    {
      check_user_vaddr (udst + i);
      if (!put_user (udst + i, src[i]))
        sys_exit (-1);
    }
}

static char *
copy_in_string (const char *us)
{
  char *ks;
  size_t i;

  check_user_vaddr (us);

  ks = malloc (PGSIZE);
  if (ks == NULL)
    sys_exit (-1);

  for (i = 0; i < PGSIZE; i++)
    {
      int ch;

      check_user_vaddr (us + i);
      ch = get_user ((const uint8_t *) us + i);
      if (ch == -1)
        {
          free (ks);
          sys_exit (-1);
        }

      ks[i] = (char) ch;
      if (ks[i] == '\0')
        return ks;
    }

  ks[PGSIZE - 1] = '\0';
  return ks;
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
    case SYS_HALT:
      shutdown_power_off ();
      break;

    case SYS_EXIT:
      sys_exit ((int) read_u32 (esp + 1));
      break;

    case SYS_EXEC:
      f->eax = sys_exec ((const char *) read_u32 (esp + 1));
      break;

    case SYS_WAIT:
      f->eax = sys_wait ((tid_t) read_u32 (esp + 1));
      break;

    case SYS_CREATE:
      f->eax = sys_create ((const char *) read_u32 (esp + 1),
                           (unsigned) read_u32 (esp + 2));
      break;

    case SYS_REMOVE:
      f->eax = sys_remove ((const char *) read_u32 (esp + 1));
      break;

    case SYS_OPEN:
      f->eax = sys_open ((const char *) read_u32 (esp + 1));
      break;

    case SYS_FILESIZE:
      f->eax = sys_filesize ((int) read_u32 (esp + 1));
      break;

    case SYS_READ:
      f->eax = sys_read ((int) read_u32 (esp + 1),
                         (void *) read_u32 (esp + 2),
                         (unsigned) read_u32 (esp + 3));
      break;

    case SYS_WRITE:
      f->eax = sys_write ((int) read_u32 (esp + 1),
                          (const void *) read_u32 (esp + 2),
                          (unsigned) read_u32 (esp + 3));
      break;

    case SYS_SEEK:
      sys_seek ((int) read_u32 (esp + 1), (unsigned) read_u32 (esp + 2));
      break;

    case SYS_TELL:
      f->eax = sys_tell ((int) read_u32 (esp + 1));
      break;

    case SYS_CLOSE:
      sys_close ((int) read_u32 (esp + 1));
      break;

    default:
      sys_exit (-1);
      break;
    }
}


// LAB 2.4
static struct file_descriptor *
lookup_fd (int fd)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;

  for (e = list_begin (&cur->fd_list); e != list_end (&cur->fd_list);
       e = list_next (e))
    {
      struct file_descriptor *desc = list_entry (e, struct file_descriptor, elem);
      if (desc->fd == fd)
        return desc;
    }

  return NULL;
}

static struct file *
lookup_file (int fd)
{
  struct file_descriptor *desc = lookup_fd (fd);
  return desc != NULL ? desc->file : NULL;
}

static int
fd_allocate (struct file *file)
{
  struct thread *cur = thread_current ();
  struct file_descriptor *desc = malloc (sizeof *desc);

  if (desc == NULL)
    return -1;

  desc->fd = cur->next_fd++;
  desc->file = file;
  list_push_back (&cur->fd_list, &desc->elem);
  return desc->fd;
}

static void
fd_close (int fd)
{
  struct file_descriptor *desc = lookup_fd (fd);

  if (desc == NULL)
    return;

  list_remove (&desc->elem);
  file_close (desc->file);
  free (desc);
}



// LAB 2.4: 
/* syscall implementations for file operations. 
 * These functions acquire filesys_lock, call the corresponding filesys/file functions, and release filesys_lock. 
 * They also handle copying strings and buffers between user and kernel space, and manage file descriptors.
 */

static bool
sys_create (const char *file, unsigned initial_size)
{
  char *kfile = copy_in_string (file);
  bool ok;

  lock_acquire (&filesys_lock);
  ok = filesys_create (kfile, (off_t) initial_size);
  lock_release (&filesys_lock);

  free (kfile);
  return ok;
}

static bool
sys_remove (const char *file)
{
  char *kfile = copy_in_string (file);
  bool ok;

  lock_acquire (&filesys_lock);
  ok = filesys_remove (kfile);
  lock_release (&filesys_lock);

  free (kfile);
  return ok;
}

static int
sys_open (const char *file)
{
  char *kfile = copy_in_string (file);
  struct file *opened;
  int fd = -1;

  lock_acquire (&filesys_lock);
  opened = filesys_open (kfile);
  if (opened != NULL)
    fd = fd_allocate (opened);
  lock_release (&filesys_lock);

  if (opened != NULL && fd == -1)
    {
      lock_acquire (&filesys_lock);
      file_close (opened);
      lock_release (&filesys_lock);
    }

  free (kfile);
  return fd;
}

static int
sys_filesize (int fd)
{
  struct file *file = lookup_file (fd);
  int length;

  if (file == NULL)
    return -1;

  lock_acquire (&filesys_lock);
  length = (int) file_length (file);
  lock_release (&filesys_lock);
  return length;
}

static int
sys_read (int fd, void *buffer, unsigned size)
{
  uint8_t *ubuffer = buffer;
  unsigned bytes_read = 0;
  uint8_t bounce[256];

  if (size == 0)
    return 0;

  check_user_buffer (buffer, size);

  if (fd == 0)
    {
      for (bytes_read = 0; bytes_read < size; bytes_read++)
        {
          uint8_t ch = input_getc ();
          if (!put_user (ubuffer + bytes_read, ch))
            sys_exit (-1);
        }
      return (int) bytes_read;
    }

  if (fd == 1)
    return -1;

  while (bytes_read < size)
    {
      struct file *file = lookup_file (fd);
      unsigned chunk = size - bytes_read;
      int chunk_read;

      if (file == NULL)
        return -1;

      if (chunk > sizeof bounce)
        chunk = sizeof bounce;

      lock_acquire (&filesys_lock);
      chunk_read = (int) file_read (file, bounce, (off_t) chunk);
      lock_release (&filesys_lock);

      if (chunk_read <= 0)
        break;

      copy_out (ubuffer + bytes_read, bounce, (size_t) chunk_read);
      bytes_read += (unsigned) chunk_read;

      if ((unsigned) chunk_read < chunk)
        break;
    }

  return (int) bytes_read;
}

static int
sys_write (int fd, const void *buffer, unsigned size)
{
  const uint8_t *ubuffer = buffer;
  unsigned bytes_written = 0;
  uint8_t bounce[256];

  if (size == 0)
    return 0;

  check_user_buffer (buffer, size);

  if (fd == 1)
    {
      while (bytes_written < size)
        {
          unsigned chunk = size - bytes_written;

          if (chunk > sizeof bounce)
            chunk = sizeof bounce;

          copy_in (bounce, ubuffer + bytes_written, chunk);
          putbuf ((char *) bounce, chunk);
          bytes_written += chunk;
        }
      return (int) bytes_written;
    }

  if (fd == 0)
    return -1;

  while (bytes_written < size)
    {
      struct file *file = lookup_file (fd);
      unsigned chunk = size - bytes_written;
      int chunk_written;

      if (file == NULL)
        return bytes_written == 0 ? -1 : (int) bytes_written;

      if (chunk > sizeof bounce)
        chunk = sizeof bounce;

      copy_in (bounce, ubuffer + bytes_written, chunk);

      lock_acquire (&filesys_lock);
      chunk_written = (int) file_write (file, bounce, (off_t) chunk);
      lock_release (&filesys_lock);

      if (chunk_written <= 0)
        break;

      bytes_written += (unsigned) chunk_written;

      if ((unsigned) chunk_written < chunk)
        break;
    }

  return (int) bytes_written;
}

static void
sys_seek (int fd, unsigned position)
{
  struct file *file = lookup_file (fd);

  if (file == NULL)
    return;

  lock_acquire (&filesys_lock);
  file_seek (file, (off_t) position);
  lock_release (&filesys_lock);
}

static unsigned
sys_tell (int fd)
{
  struct file *file = lookup_file (fd);
  unsigned position;

  if (file == NULL)
    return 0;

  lock_acquire (&filesys_lock);
  position = (unsigned) file_tell (file);
  lock_release (&filesys_lock);
  return position;
}

static void
sys_close (int fd)
{
  if (fd < 2)
    return;

  lock_acquire (&filesys_lock);
  fd_close (fd);
  lock_release (&filesys_lock);
}

static tid_t
sys_exec (const char *cmd_line)
{
  char *kcmd = copy_in_string (cmd_line);
  tid_t tid = process_execute (kcmd);

  free (kcmd);
  return tid;
}

static int
sys_wait (tid_t pid)
{
  return process_wait (pid);
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
