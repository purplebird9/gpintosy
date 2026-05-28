#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include <stdbool.h>
#include "threads/palloc.h"

struct thread;

/** One physical user frame tracked by the VM system.

   KPAGE is the kernel virtual address returned by palloc_get_page().
   It represents a physical frame from the user pool.  OWNER/UPAGE let
   eviction update the correct process page table and SPT entry. */
struct frame_entry
  {
    void *kpage;               /**< Kernel VA of the physical frame. */
    struct thread *owner;      /**< Process that currently owns KPAGE. */
    void *upage;               /**< Owner's user virtual page. */
    bool pinned;               /**< True while disk/syscall code uses it. */
    struct list_elem elem;     /**< Element in global frame table. */
  };

void frame_table_init (void);

struct frame_entry *frame_allocate (enum palloc_flags flags, void *upage);
struct frame_entry *frame_lookup (void *kpage);
void frame_free (void *kpage);

void frame_pin (void *kpage);// deprecated
void frame_unpin (void *kpage);// deprecated
bool frame_pin_user_page (struct thread *owner, const void *upage);
void frame_unpin_user_page (struct thread *owner, const void *upage);

#endif /**< vm/frame.h */
