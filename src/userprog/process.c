#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
// LAB3A
#ifdef VM
#include "vm/frame.h"
#include "vm/spt.h"
#endif

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
static bool tid_is_alive (tid_t tid) UNUSED;
static void find_tid_action (struct thread *t, void *aux);

/* LAB3A funcs start */

static void *process_alloc_user_page (enum palloc_flags flags, void *upage);
static void process_free_user_page (void *kpage);
#ifdef VM
static bool process_spt_insert (void *upage, bool writable,
                                 enum vm_page_type type, struct file *file,
                                 off_t ofs, uint32_t read_bytes,
                                 uint32_t zero_bytes, void *kpage);
#endif

/* LAB3A funcs end */

/* LAB 2.4 Start */
/* Shared execution status between parent and child. */
struct child_process_status
  {
    tid_t tid;                         /**< Child tid. */
    int exit_status;                   /**< Child exit status. */
    bool load_success;                 /**< Whether child loaded successfully. */
    bool exited;                       /**< Whether child has exited. */
    int ref_cnt;                       /**< Reference count shared by parent and child. */
    struct semaphore load_sema;        /**< Parent waits for exec load result. */
    struct semaphore exit_sema;        /**< Parent waits for child exit. */
    struct list_elem elem;             /**< Element in parent's child list. */
  };

/* Startup bundle passed to the child thread.*/
struct process_start_info
  {
    char *file_name;                           /**< Command line page copy. */
    struct child_process_status *child_info;   /**< Shared parent-child status. */
  };

/* Helpers for child status management. */
static struct child_process_status *find_child_process (tid_t child_tid);
static void release_child_process (struct child_process_status *child);

/* LAB 2.4 End */




/** Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  char *fn_copy_for_name;
  char *prog_name;
  char *save_ptr;

  struct process_start_info *start_info;
  struct child_process_status *child_info;
  struct thread *cur = thread_current ();


  tid_t tid;

  //DEBUG
  //printf("DEBUG: Entering process_execute, cmd: %s\n", file_name);


  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  // LAB 2.4
  /* Allocate parent-child shared status and child start bundle on HEAP */
  child_info = malloc (sizeof *child_info);
  if (child_info == NULL)
    {
      palloc_free_page (fn_copy);
      return TID_ERROR;
    }
  child_info->tid = TID_ERROR;
  child_info->exit_status = -1;
  child_info->load_success = false;
  child_info->exited = false;
  child_info->ref_cnt = 2;
  sema_init (&child_info->load_sema, 0);
  sema_init (&child_info->exit_sema, 0);
  list_push_back (&cur->child_processes, &child_info->elem);

  start_info = malloc (sizeof *start_info);
  if (start_info == NULL)
    {
      list_remove (&child_info->elem);
      free (child_info);
      palloc_free_page (fn_copy);
      return TID_ERROR;
    }
  start_info->file_name = fn_copy;
  start_info->child_info = child_info;

  // LAB2.2
  /* Make another copy for name only, cuz "strtok_r" will change the string*/
  fn_copy_for_name = palloc_get_page (0);
  if (fn_copy_for_name == NULL)
    {
      free (start_info);
      list_remove (&child_info->elem);
      free (child_info);
      palloc_free_page (fn_copy);
      return TID_ERROR;
    }
  strlcpy (fn_copy_for_name, file_name, PGSIZE);
  /* Extract the program name (the first token) from the command line. */
  prog_name = strtok_r (fn_copy_for_name, " ", &save_ptr);
  if (prog_name == NULL)
    {
      free (start_info);
      list_remove (&child_info->elem);
      free (child_info);
      palloc_free_page (fn_copy);
      palloc_free_page (fn_copy_for_name);
      return TID_ERROR;
    }
  
  // LAB 2.2: file_name -> prog_name
  /* Create a new thread to execute PROG_NAME. */
  tid = thread_create (prog_name, PRI_DEFAULT, start_process, start_info);
  if (tid == TID_ERROR)
    {
      free (start_info);
      list_remove (&child_info->elem);
      free (child_info);
      palloc_free_page (fn_copy);
    }
  else
    {
      // LAB 2.4: Parent waits until child reports load success or failure.
      child_info->tid = tid;
      sema_down (&child_info->load_sema);
      if (!child_info->load_success)
        {
          list_remove (&child_info->elem);
          release_child_process (child_info);
          tid = TID_ERROR;
        }
    }
  palloc_free_page (fn_copy_for_name);
  return tid;
}



/** A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  struct process_start_info *start_info = file_name_;
  char *file_name = start_info->file_name;
  struct child_process_status *child_info = start_info->child_info;
  struct intr_frame if_;
  struct thread *cur = thread_current ();
  bool success;

  // LAB 2.4: Attach shared child status to the child thread before loading.
  cur->child_info = child_info;
  free (start_info);

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);

  // LAB 2.4: Wake parent once the child knows whether load succeeded.
  child_info->load_success = success;
  sema_up (&child_info->load_sema);

  /* If load failed, quit. */
  palloc_free_page (file_name);
  if (!success) 
    thread_exit ();

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/** Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */


/** LAB 2 Implementation of process_wait.
 * syscall wait() is based on this function, and implemented in syscall.c.
 */
// LAB 2.2: this implementation is temporary just to let it run
// Now it is complete.
int
process_wait (tid_t child_tid)
{
  struct child_process_status *child;
  int exit_status;

  /* if invalid TID, return -1 */
  if (child_tid == TID_ERROR)
    return -1;

  /* Only direct children can be waited on, and at most once.*/
  child = find_child_process (child_tid);
  if (child == NULL)
    return -1;

  list_remove (&child->elem);

  /* Wait until the child actually exits, if needed. */
  if (!child->exited)
    sema_down (&child->exit_sema);

  exit_status = child->exit_status;
  release_child_process (child);
  return exit_status;
}

/*  LAB 2 辅助函数 */


struct find_tid_aux
  {
    tid_t target;
    bool found;
  };

/* 在thread_foreach中被调用，检查是否存在tid为target且状态不为THREAD_DYING的线程 */
static void
find_tid_action (struct thread *t, void *aux)
{
  struct find_tid_aux *find = aux;
  if (t->tid == find->target && t->status != THREAD_DYING)
    find->found = true;
}

/* 检查tid是否存在且状态不为THREAD_DYING */
static bool
tid_is_alive (tid_t tid)
{
  struct find_tid_aux find;
  enum intr_level old_level;

  find.target = tid;
  find.found = false;

  old_level = intr_disable ();
  thread_foreach (find_tid_action, &find);
  intr_set_level (old_level);

  return find.found;
}

/* LAB 2.4: Find a direct child process status by tid. */
static struct child_process_status *
find_child_process (tid_t child_tid)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;

  for (e = list_begin (&cur->child_processes);
       e != list_end (&cur->child_processes);
       e = list_next (e))
    {
      struct child_process_status *child =
        list_entry (e, struct child_process_status, elem);
      if (child->tid == child_tid)
        return child;
    }
  return NULL;
}

/* LAB 2.4: Shared child status is freed by whichever side exits last. */
static void
release_child_process (struct child_process_status *child)
{
  child->ref_cnt--;
  if (child->ref_cnt == 0)
    free (child);
}

/* LAB 2: Implementation of process_wait ends. */




/** Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
  struct list_elem *e;

// LAB 2.4: 退出时关闭所有打开文件
  lock_acquire (&filesys_lock);
  syscall_close_all_files ();
  if (cur->exec_file != NULL)
    {
      file_allow_write (cur->exec_file);
      file_close (cur->exec_file);
      cur->exec_file = NULL;
    }
  lock_release (&filesys_lock);

#ifdef USERPROG
  /* 打印退出信息，仅针对用户进程，且不是halt */
 // LAB 2 DEBUG: 把打印退出信息挪回process_exit()
  if (cur->pagedir != NULL && strcmp(cur->name, "main") != 0 && strcmp(cur->name, "idle") != 0) {
    char proc_name[16];
    strlcpy(proc_name, cur->name, sizeof(proc_name));
    char *space = strchr(proc_name, ' ');
    if (space) *space = '\0';
    printf("%s: exit(%d)\n", proc_name, cur->exit_status);
  } 
    
#endif

  // LAB 2.4: Publish exit status to parent and wake any waiter.
  if (cur->child_info != NULL)
    {
      cur->child_info->exit_status = cur->exit_status;
      cur->child_info->exited = true;
      sema_up (&cur->child_info->exit_sema);
      release_child_process (cur->child_info);
      cur->child_info = NULL;
    }

  // LAB 2.4: If parent exits first, detach all remaining children.
  while (!list_empty (&cur->child_processes))
    {
      e = list_pop_front (&cur->child_processes);
      release_child_process (list_entry (e, struct child_process_status, elem));
    }

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
// LAB3A : destroy SPT before destroying pagedir, 
// cuz SPT may need to free user pages, which in turn needs the pagedir to be still active.
#ifdef VM
      spt_destroy (&cur->spt);
#endif

      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/** Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/** We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/** ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/** For use with ELF types in printf(). */
#define PE32Wx PRIx32   /**< Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /**< Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /**< Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /**< Print Elf32_Half in hexadecimal. */

/** Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/** Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/** Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /**< Ignore. */
#define PT_LOAD    1            /**< Loadable segment. */
#define PT_DYNAMIC 2            /**< Dynamic linking info. */
#define PT_INTERP  3            /**< Name of dynamic loader. */
#define PT_NOTE    4            /**< Auxiliary info. */
#define PT_SHLIB   5            /**< Reserved. */
#define PT_PHDR    6            /**< Program header table. */
#define PT_STACK   0x6474e551   /**< Stack segment. */

/** Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /**< Executable. */
#define PF_W 2          /**< Writable. */
#define PF_R 4          /**< Readable. */

static bool setup_stack (void **esp, const char *cmdline);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/** Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  // LAB 2.2
  char *file_name_copy = NULL;
  char *save_ptr;
  char *prog_name;


  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  // LAB 2.4: 全局 filesys_lock
  bool fs_locked = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;

    /* lAB3A: init SPT (& error check) after load() creates pagedir. */
#ifdef VM
  if (!spt_init (&t->spt))
    {
      pagedir_destroy (t->pagedir);
      t->pagedir = NULL;
      goto done;
    }
#endif
  process_activate ();

  // LAB 2.2: Extract the program name (the first token) from the command line.
  file_name_copy = palloc_get_page(0);
  if (file_name_copy == NULL)
    goto done; //done: close file, return false
  strlcpy(file_name_copy, file_name, PGSIZE);

  prog_name = strtok_r(file_name_copy, " ", &save_ptr);
  if (prog_name == NULL)
  {
    goto done;
  }

  // LAB 2.4: 全局 filesys_lock，把文件系统访问串行化
  lock_acquire (&filesys_lock);
  fs_locked = true;
  /* Open executable file. */
  file = filesys_open (prog_name);

  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", prog_name);
      goto done; 
    }

  // LAB 2.5:运行中可执行文件的 deny_write / 退出时 allow_write，避免被改写
  file_deny_write (file);
  t->exec_file = file;

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp, file_name))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
 // lab 2.4
  if (fs_locked)
    lock_release (&filesys_lock);
  /* We arrive here whether the load is successful or not. */
  // LAB 2.2: free the page allocated for file_name_copy
  if (file_name_copy != NULL)
    palloc_free_page (file_name_copy);
  if (!success && file != NULL)
    {
      file_close (file);
      t->exec_file = NULL;
    }
  return success;
}

/** load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/** Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/** Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      // LAB3A-If VM, just insert an SPT entry. Lazy-load in page fault handler.
#ifdef VM
      if (!process_spt_insert (upage, writable,
                               page_read_bytes > 0
                               ? VM_PAGE_FILE : VM_PAGE_ZERO,
                               page_read_bytes > 0 ? file : NULL,
                               page_read_bytes > 0 ? ofs : 0,
                               page_read_bytes, page_zero_bytes, NULL))
        return false;
#else // If not VM, still load the page immediately.
      /* Get a user frame.  
        In VM builds this records the frame in the
         global frame table; without VM it falls back to the old allocator. */
      uint8_t *kpage = process_alloc_user_page (PAL_USER, upage); // LAB3A new allocator
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          process_free_user_page (kpage);// LAB3A new deallocator
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          process_free_user_page (kpage);// LAB3A new deallocator
          return false; 
        }

#endif

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
      ofs += PGSIZE; //LAB3A
    }
  return true;
}


// LAB 2.2: set up the stack with command line arguments
/** Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp, const char *cmdline) 
{
  #define MAX_ARGS 128
  uint8_t *kpage;
  bool success = false;
  char *cmdline_copy = NULL;
  char *argv[MAX_ARGS];
  int argc = 0;
  char *token;
  char *save_ptr;
  uint8_t *sp;
  int i;

  // LAB3A: new allocator and record: stack's upage = PHYS_BASE-4096 (top of user VM).
  kpage = process_alloc_user_page (PAL_USER | PAL_ZERO,
                                   ((uint8_t *) PHYS_BASE) - PGSIZE); 
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        {
#ifdef VM
          /* LAB3A: insert a VM_PAGE_ZERO SPT entry after setup_stack() installs the stack page*/
          success = process_spt_insert (((uint8_t *) PHYS_BASE) - PGSIZE,
                                         true, VM_PAGE_ZERO, NULL, 0, 0,
                                         PGSIZE, kpage);
          if (!success)
            {
              pagedir_clear_page (thread_current ()->pagedir,
                                  ((uint8_t *) PHYS_BASE) - PGSIZE);
              process_free_user_page (kpage);
            }
          else
#endif
            *esp = PHYS_BASE;
        }
      else
        process_free_user_page (kpage); // LAB3A: new deallocator
    }

  // LAB 2.2
  if (!success)
    return false;

  cmdline_copy = palloc_get_page (0);
  if (cmdline_copy == NULL)
    return false;
  strlcpy (cmdline_copy, cmdline, PGSIZE);

  for (token = strtok_r (cmdline_copy, " ", &save_ptr);
       token != NULL;
       token = strtok_r (NULL, " ", &save_ptr))
    {
      if (argc >= MAX_ARGS)
        {
          palloc_free_page (cmdline_copy);
          return false;
        }
      argv[argc++] = token;
    }

  sp = *esp;

  /* Push argument strings in reverse order. */
  for (i = argc - 1; i >= 0; i--)
    {
      size_t len = strlen (argv[i]) + 1;
      sp -= len;
      if (sp < (uint8_t *) PHYS_BASE - PGSIZE)
        {
          palloc_free_page (cmdline_copy);
          return false;
        }
      memcpy (sp, argv[i], len);
      argv[i] = (char *) sp;
    }

  /* Word-align stack pointer to a multiple of 4. */
  while ((uintptr_t) sp % 4 != 0)
    *--sp = 0;

  /* Push argv[argc] == NULL sentinel. */
  sp -= sizeof (char *);
  if (sp < (uint8_t *) PHYS_BASE - PGSIZE)
    {
      palloc_free_page (cmdline_copy);
      return false;
    }
  *(char **) sp = NULL;

  /* Push argv pointers in reverse order. */
  for (i = argc - 1; i >= 0; i--)
    {
      sp -= sizeof (char *);
      if (sp < (uint8_t *) PHYS_BASE - PGSIZE)
        {
          palloc_free_page (cmdline_copy);
          return false;
        }
      *(char **) sp = argv[i];
    }

  /* Push argv (address of argv[0]), argc, and fake return address. */
  {
    char **argv_start = (char **) sp;

    sp -= sizeof (char **);
    if (sp < (uint8_t *) PHYS_BASE - PGSIZE)
      {
        palloc_free_page (cmdline_copy);
        return false;
      }
    *(char ***) sp = argv_start;

    sp -= sizeof (int);
    if (sp < (uint8_t *) PHYS_BASE - PGSIZE)
      {
        palloc_free_page (cmdline_copy);
        return false;
      }
    *(int *) sp = argc;

    sp -= sizeof (void *);
    if (sp < (uint8_t *) PHYS_BASE - PGSIZE)
      {
        palloc_free_page (cmdline_copy);
        return false;
      }
    *(void **) sp = NULL;
  }

  *esp = sp;
  palloc_free_page (cmdline_copy);
  #undef MAX_ARGS
  return success;
}

/** Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

/** LAB3A-Allocates one physical page for a user virtual page.

   LAB2 used palloc_get_page(PAL_USER) directly.  In VM builds,
   every user frame must go through frame_allocate() so the frame table
   can later support eviction.  For this intermediate step, frame_allocate()
   simply fails if the user pool is full. */
static void *
process_alloc_user_page (enum palloc_flags flags, void *upage)
{
#ifdef VM
  struct frame_entry *frame;

  ASSERT ((flags & PAL_USER) != 0);
  frame = frame_allocate (flags, upage); //frame_allocate()是VM层对palloc_get_page()的封装,为了支持VM管理
  return frame != NULL ? frame->kpage : NULL;
#else
  (void) upage; //warning eliminate
  return palloc_get_page (flags); // 底层的frame allocator
#endif
}

/** LAB3A-Releases a user page allocated by process_alloc_user_page(). */
static void
process_free_user_page (void *kpage)
{
#ifdef VM
  frame_free (kpage); //封装
#else
  palloc_free_page (kpage);
#endif
}

/** LAB3A-Records info about a user page in current process's SPT entry. */
#ifdef VM
static bool
process_spt_insert (void *upage, bool writable, enum vm_page_type type,
                     struct file *file, off_t ofs, uint32_t read_bytes,
                     uint32_t zero_bytes, void *kpage)
{
  struct spt_entry *spte;

  ASSERT (pg_ofs (upage) == 0);
  ASSERT (read_bytes + zero_bytes == PGSIZE);

  spte = malloc (sizeof *spte);
  if (spte == NULL)
    return false;

  spte->upage = upage; 
  spte->writable = writable; 
  spte->type = type;
  spte->state = kpage != NULL ? VM_PAGE_LOADED : VM_PAGE_NOT_LOADED;
  spte->file = file;
  spte->ofs = ofs;
  spte->read_bytes = read_bytes;
  spte->zero_bytes = zero_bytes;
  spte->swap_slot = (size_t) -1;
  spte->kpage = kpage;
  spte->mapid = -1;

  if (!spt_insert (&thread_current ()->spt, spte))
    {
      free (spte);
      return false;
    }

  return true;
}
#endif


