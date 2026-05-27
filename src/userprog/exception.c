#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "userprog/syscall.h"
#include "threads/interrupt.h"
#include "threads/thread.h"

/* LAB3A starts */
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "threads/vaddr.h"
#include <string.h>
#ifdef VM
#include "vm/frame.h"
#include "vm/spt.h"
#include "vm/swap.h"
#endif
/* lAB3A ends */

/** Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

//LAB3A
#ifdef VM
static bool vm_load_page (struct spt_entry *spte);
#endif

/** Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void
exception_init (void) 
{
  /* These exceptions can be raised explicitly by a user program,
     e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
     we set DPL==3, meaning that user programs are allowed to
     invoke them via these instructions. */
  intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int (5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

  /* These exceptions have DPL==0, preventing user processes from
     invoking them via the INT instruction.  They can still be
     caused indirectly, e.g. #DE can be caused by dividing by
     0.  */
  intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int (7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
  intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int (19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");

  /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
  intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/** Prints exception statistics. */
void
exception_print_stats (void) 
{
  printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/** Handler for an exception (probably) caused by a user process. */
static void
kill (struct intr_frame *f) 
{
  /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */
     
  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs)
    {
    case SEL_UCSEG:
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
      // LAB 2.4: 把异常杀进程路径改成统一走 sys_exit(-1)了
      sys_exit (-1);

    case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      intr_dump_frame (f);
      PANIC ("Kernel bug - unexpected interrupt in kernel"); 

    default:
      /* Some other code segment?  Shouldn't happen.  Panic the
         kernel. */
      printf ("Interrupt %#04x (%s) in unknown segment %04x\n",
             f->vec_no, intr_name (f->vec_no), f->cs);
      thread_exit ();
    }
}

/** Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to project 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void
page_fault (struct intr_frame *f) // f:exception发生时 CPU 状态的快照。主要存储f->error_code
{
  bool not_present;  /**< True: not-present page, false: writing r/o page. */
  bool write;        /**< True: access was write, false: access was read. */
  bool user;         /**< True: access by user, false: access by kernel. */
  void *fault_addr;  /**< Fault address. */

  /* Obtain faulting address, the virtual address that was
     accessed to cause the fault.  It may point to code or to
     data.  It is not necessarily the address of the instruction
     that caused the fault (that's f->eip).
     See [IA32-v2a] "MOV--Move to/from Control Registers" and
     [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
     (#PF)". */
  asm ("movl %%cr2, %0" : "=r" (fault_addr));

  /* Turn interrupts back on (they were only off so that we could
     be assured of reading CR2 before it changed). */
  intr_enable ();

  /* Count page faults. */
  page_fault_cnt++;

  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0; // not_present->lazy-loading
  write = (f->error_code & PF_W) != 0;// write还是read导致的page fault, 判断权限
  user = (f->error_code & PF_U) != 0;//fault在user mode还是kernel mode

/*LAB 3A: If the fault was a not-present page, try to load the page from disk.*/ 
#ifdef VM
  if (not_present && is_user_vaddr (fault_addr))
    {
      struct thread *cur = thread_current ();
      struct spt_entry *spte = cur->pagedir != NULL
                               ? spt_find (&cur->spt, fault_addr)
                               : NULL;

      // Call vm_load_page().                     
      if (spte != NULL && (!write || spte->writable) && vm_load_page (spte))
        return;
    }
#endif
/* LAB3A: if no successful load, fall through to old code.*/

  // LAB 2.3
  /* Let get_user()/put_user() turn a bad user-memory probe into
     a normal -1 return instead of panicking the kernel. */
  if (!user)
    {
      f->eip = (void (*) (void)) f->eax;
      f->eax = 0xffffffff;
      return;
    }

  /* To implement virtual memory, delete the rest of the function
     body, and replace it with code that brings in the page to
     which fault_addr refers. */
  printf ("Page fault at %p: %s error %s page in %s context.\n",
          fault_addr,
          not_present ? "not present" : "rights violation",
          write ? "writing" : "reading",
          user ? "user" : "kernel");
  kill (f);
}

#ifdef VM
/** LAB3A: Loads one SPT entry into memory and installs it in the page table. */
static bool
vm_load_page (struct spt_entry *spte)
{
  struct frame_entry *frame;
  void *kpage;
  struct thread *cur = thread_current ();

  // already loaded
  if (spte->state == VM_PAGE_LOADED)
    return true;
    
  frame = frame_allocate (PAL_USER, spte->upage);
  if (frame == NULL)
    return false;
  kpage = frame->kpage;
  frame_pin (kpage); // prevent eviction during loading

  /* Load Start. */
  switch (spte->type)
    {
    case VM_PAGE_FILE: // 从文件加载
      lock_acquire (&filesys_lock); // filesystem lock
      if (file_read_at (spte->file, kpage, spte->read_bytes, spte->ofs)
          != (int) spte->read_bytes)
        {
          lock_release (&filesys_lock);
          frame_unpin (kpage);
          frame_free (kpage);
          return false;
        }
      lock_release (&filesys_lock);
      memset ((uint8_t *) kpage + spte->read_bytes, 0, spte->zero_bytes); // 文件末尾剩余部分置0
      break;

    case VM_PAGE_ZERO:
      memset (kpage, 0, PGSIZE);
      break;

    case VM_PAGE_SWAP:// load from swap slot
      if (!swap_in (spte->swap_slot, kpage))
        {
          frame_unpin (kpage);
          frame_free (kpage);
          return false;
        }
        
      // After a successful swap-in, the SPT entry should stop referring to the slot
      spte->swap_slot = (size_t) -1; // spte->swap_slot 设回 -1。
      break;

    default:
      frame_unpin (kpage);
      frame_free (kpage);
      return false;
    }

  // Call pagedir_get_page() to check that another thread hasn't loaded a page at spte->upage since.
  // Call pagedir_set_page() to add a mapping from upage to frame.
  if (pagedir_get_page (cur->pagedir, spte->upage) != NULL
      || !pagedir_set_page (cur->pagedir, spte->upage, kpage,
                            spte->writable))
    {
      frame_unpin (kpage);
      frame_free (kpage);
      return false;
    }

  spte->kpage = kpage;
  spte->state = VM_PAGE_LOADED;
  /* Load Finished. */
  frame_unpin (kpage);
  return true;
}
#endif
