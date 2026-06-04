/** Global Frame Table

   Frame table only tracks frames from the user pool.  
   Kernel pool allocations remain non-pageable so VM metadata can safely live there. */

#include "vm/frame.h"
#include <debug.h>
#include <list.h>
#include <string.h>
#include "userprog/pagedir.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/spt.h"
#include "vm/swap.h"

static struct list frame_table;          /**< Global list of user frames. */
static struct lock frame_lock;           /**< Protects the GLOBAL frame_table. */
static struct list_elem *clock_hand;     /**< Next frame considered by clock. */

static struct frame_entry *frame_evict (void);
static struct frame_entry *frame_select_victim_clock_locked (void);
static bool frame_evict_page (struct frame_entry *victim);

/** Initializes the global frame table.*/
void
frame_table_init (void)
{
  list_init (&frame_table);
  lock_init (&frame_lock);
  clock_hand = NULL;
}

/** Allocates one user frame and records it in the frame table.

   FLAGS must include PAL_USER.  
   If palloc cannot find a free user frame, eviction tries to reuse
   an unpinned frame selected by the clock hand. */
struct frame_entry *
frame_allocate (enum palloc_flags flags, void *upage)
{
  struct frame_entry *frame;
  void *kpage;
  void *round_upage;

  ASSERT ((flags & PAL_USER) != 0); // must be from user pool
  round_upage = upage != NULL ? pg_round_down (upage) : NULL; //LAB3A Fix bug: argument 'upage' may not be page-aligned, so round it.

  kpage = palloc_get_page (flags); // try to obtain a free user frame
  if (kpage == NULL) // no available frame
    {
      /* ~~~Eviction~~~ */
      frame = frame_evict ();
      if (frame == NULL)
        return NULL;

      if ((flags & PAL_ZERO) != 0) 
        memset (frame->kpage, 0, PGSIZE);

      // frame: the frame entry for reuse, returned from frame_evict().
      frame->owner = thread_current (); 
      frame->upage = round_upage;
      frame->pinned = true;// SYNC: avoid eviction during frame initialization
      return frame;
    }

  frame = malloc (sizeof *frame); // allocate frame entry
  if (frame == NULL) // if malloc fails
    {
      palloc_free_page (kpage);
      return NULL;
    }

  frame->kpage = kpage;
  frame->owner = thread_current ();
  frame->upage = round_upage;
  frame->pinned = true; // SYNC: avoid eviction during frame initialization

  lock_acquire (&frame_lock); //sync
  list_push_back (&frame_table, &frame->elem);
  lock_release (&frame_lock);
  return frame;
}

/** Evicts one frame and returns its frame table entry for reuse.

   Victims are selected by a clock algorithm that approximates LRU. */
static struct frame_entry *
frame_evict (void)
{
  struct frame_entry *victim;

  lock_acquire (&frame_lock);
  victim = frame_select_victim_clock_locked (); // EVICTION
  if (victim == NULL)
    {
      lock_release (&frame_lock);
      return NULL;
    }

  victim->pinned = true;// SYNC: pin the victim frame so it won't be evicted by concurrent frame_evict() calls before evict finish.
  if (!frame_evict_page (victim))// EVICT happens
    {
      victim->pinned = false;
      lock_release (&frame_lock);
      return NULL;
    }

  lock_release (&frame_lock);
  return victim;
}

/** Selects an eviction victim with a clock algorithm.

   Caller must hold frame_lock.  
   Pinned frames are skipped because they may be in the middle of disk or syscall buffer I/O.
   Accessed frames get a second chance: clear their accessed bit and keep scanning. */
static struct frame_entry *
frame_select_victim_clock_locked (void)
{
  struct list_elem *e;
  size_t frame_cnt;
  size_t scanned;

  ASSERT (lock_held_by_current_thread (&frame_lock));

  if (list_empty (&frame_table))
    return NULL;

  if (clock_hand == NULL || clock_hand == list_end (&frame_table))
    clock_hand = list_begin (&frame_table);

  frame_cnt = list_size (&frame_table);
  for (scanned = 0; scanned < frame_cnt * 2; scanned++)
    {
      struct frame_entry *frame;
      void *upage;

      e = clock_hand;
      clock_hand = list_next (clock_hand);
      if (clock_hand == list_end (&frame_table))
        clock_hand = list_begin (&frame_table);

      frame = list_entry (e, struct frame_entry, elem);

      // Skip pinned frames
      // LAB3B: zero pages are not skipped, so Stack Pages are candidates.

      // no owner: does not belong to any process, just evicted.
      // owner has no pagedir: process is exiting, so its pagedir is already destroyed.
      // no upage: frame has onwer process but not mapped to any user page, just evicted or being initialized.
      if (frame->pinned || frame->owner == NULL
          || frame->owner->pagedir == NULL || frame->upage == NULL)
        continue;

      upage = pg_round_down (frame->upage);
      if (pagedir_is_accessed (frame->owner->pagedir, upage))
        {
          pagedir_set_accessed (frame->owner->pagedir, upage, false);
          continue;
        }

      return frame;
    }

  return NULL;
}

/** Evicts VICTIM(discard/write to swap) and updates its owner's pagedir/SPT state.

   Alias sol: Always use UPAGE, not KPAGE, as frame access path. 
   Dirty bits are read only through the user virtual page---> no alias prob.

   Dirty pages and pages whose only copy is already swap-backed must be written to swap;
   Clean file-backed pages are discarded instead of swapped. */
static bool
frame_evict_page (struct frame_entry *victim)
{
  struct spt_entry *spte;
  void *upage;
  bool dirty;
  bool must_swap;
  size_t slot;

  ASSERT (victim != NULL);
  ASSERT (lock_held_by_current_thread (&frame_lock));
  ASSERT (victim->owner != NULL);
  ASSERT (victim->upage != NULL);
  ASSERT (victim->kpage != NULL);
  ASSERT (pg_ofs (victim->kpage) == 0);// SYNC check: frame's kpage must be page-aligned

  upage = pg_round_down (victim->upage);
  victim->upage = upage;

  spte = spt_find (&victim->owner->spt, upage);
  if (spte == NULL)
    return false;
  
  lock_acquire (&spte->lock);
  if (spte->state != VM_PAGE_LOADED || spte->kpage != victim->kpage)
    {
      lock_release (&spte->lock);
      return false;
    }

  // dirty is read-only through USER.
  dirty = victim->owner->pagedir != NULL
          && pagedir_is_dirty (victim->owner->pagedir, upage);
  // swap case: swap_backed || dirty
  // LAB3B: also swap case: zero page(new stack page)
  must_swap = spte->type == VM_PAGE_SWAP
              || spte->type == VM_PAGE_ZERO
              || dirty;

  spte->state = VM_PAGE_EVICTING; //LAB3A-B6: faulting threads must wait.
  if (victim->owner->pagedir != NULL)
    //LAB3A-B6: stop owner from writing the frame during eviction.
    pagedir_clear_page (victim->owner->pagedir, upage);

  if (must_swap)
    {
      if (!swap_out (victim->kpage, &slot))
        {
          //LAB3A-B6: restore access if eviction cannot finish.
          spte->state = VM_PAGE_LOADED;
          if (victim->owner->pagedir != NULL
              && pagedir_get_page (victim->owner->pagedir, upage) == NULL)
            pagedir_set_page (victim->owner->pagedir, upage,
                              victim->kpage, spte->writable);
          cond_broadcast (&spte->cv, &spte->lock);
          lock_release (&spte->lock);
          return false;
        }
    }

  if (must_swap)
    {
      //LAB3A-B6: faulting owner will reload from this swap slot.
      spte->type = VM_PAGE_SWAP;
      spte->swap_slot = slot;
    }
  else
    spte->swap_slot = (size_t) -1;

  spte->state = VM_PAGE_NOT_LOADED;
  spte->kpage = NULL;
  // LAB3A Fix Race: clear owner,upage after eviciton
  // So that frame_pin_user_page() will notice.
  victim->owner = NULL;
  victim->upage = NULL;
  //LAB3A-B6: page can now be faulted back from swap/file.
  cond_broadcast (&spte->cv, &spte->lock);
  lock_release (&spte->lock);

  return true;
}

/** Finds the frame table entry for KPAGE, or NULL if it is untracked. */
struct frame_entry *
frame_lookup (void *kpage) 
{
  struct list_elem *e;

  ASSERT (kpage == NULL || pg_ofs (kpage) == 0);

  lock_acquire (&frame_lock);
  for (e = list_begin (&frame_table); e != list_end (&frame_table);
       e = list_next (e))
    {
      struct frame_entry *frame = list_entry (e, struct frame_entry, elem);
      if (frame->kpage == kpage) 
        {
          lock_release (&frame_lock);
          return frame;
        }
    }
  lock_release (&frame_lock);

  return NULL; // not found
}

/** Removes KPAGE from the frame table and returns it to the user pool.

   CALLER should clear the owning process's pagedir/SPT references before
   freeing a mapped frame. */
void
frame_free (void *kpage)
{
  struct frame_entry *frame = NULL;
  struct list_elem *e;

  if (kpage == NULL)
    return;

  // frame = frame_lookup (kpage);
  // SYNC: 在同一次持锁期间完成查找和摘链，去掉“lookup 解锁，再加锁 remove”的竞态窗口。
  lock_acquire (&frame_lock);
  for (e = list_begin (&frame_table); e != list_end (&frame_table);
       e = list_next (e))
    {
      struct frame_entry *candidate = list_entry (e, struct frame_entry, elem);
      if (candidate->kpage == kpage && candidate->owner == thread_current ())
        {
          frame = candidate;
          break;
        }
    }

  if (frame == NULL)
    {
      lock_release (&frame_lock);
      return;
    }

  if (clock_hand == &frame->elem)
    {
      clock_hand = list_next (&frame->elem);
      if (clock_hand == list_end (&frame_table))
        clock_hand = list_begin (&frame_table);
      if (clock_hand == &frame->elem)
        clock_hand = NULL;
    }

  list_remove (&frame->elem);
  if (list_empty (&frame_table))
    clock_hand = NULL;
  lock_release (&frame_lock);

  palloc_free_page (kpage);
  free (frame);
}

/** Prevents KPAGE from being evicted

   Pin pages while the kernel is copying user buffers or doing disk
   I/O into a frame.  Evicting such a page in the middle of I/O can
   corrupt data or deadlock the VM path. */
// LAB3A: deprecated.
void
frame_pin (void *kpage)
{
  struct list_elem *e;

  ASSERT (kpage == NULL || pg_ofs (kpage) == 0);

  lock_acquire (&frame_lock);
  for (e = list_begin (&frame_table); e != list_end (&frame_table);
       e = list_next (e))
    {
      struct frame_entry *frame = list_entry (e, struct frame_entry, elem);
      if (frame->kpage == kpage)
        {
          frame->pinned = true;
          break;
        }
    }
  lock_release (&frame_lock);
}

/** Allows KPAGE to be evicted again. */
// LAB3A: deprecated.
void
frame_unpin (void *kpage)
{
  struct list_elem *e;

  ASSERT (kpage == NULL || pg_ofs (kpage) == 0);

  lock_acquire (&frame_lock);
  for (e = list_begin (&frame_table); e != list_end (&frame_table);
       e = list_next (e))
    {
      struct frame_entry *frame = list_entry (e, struct frame_entry, elem);
      if (frame->kpage == kpage)
        {
          frame->pinned = false;
          break;
        }
    }
  lock_release (&frame_lock);
}

/** Pins the frame currently owned by OWNER for UPAGE.

   The lookup and pin update happen while holding frame_lock, so eviction
   cannot select/reuse the frame between pagedir lookup and frame pinning. */
bool
frame_pin_user_page (struct thread *owner, const void *upage)
{
  struct list_elem *e;
  void *round_upage;
  bool pinned = false;

  if (owner == NULL || upage == NULL)
    return false;

  round_upage = pg_round_down (upage);

  /* critical section */
  lock_acquire (&frame_lock);
  for (e = list_begin (&frame_table); e != list_end (&frame_table);
       e = list_next (e))
    {
      struct frame_entry *frame = list_entry (e, struct frame_entry, elem);
      if (frame->owner == owner && frame->upage == round_upage) // lookup
        {
          frame->pinned = true;// pin
          pinned = true;
          break;
        }
    }
  lock_release (&frame_lock);
  /* critical section */

  return pinned;
}

/** Unpins the frame currently owned by OWNER for UPAGE. */
void
frame_unpin_user_page (struct thread *owner, const void *upage)
{
  struct list_elem *e;
  void *round_upage;

  if (owner == NULL || upage == NULL)
    return;

  round_upage = pg_round_down (upage);

  lock_acquire (&frame_lock);
  for (e = list_begin (&frame_table); e != list_end (&frame_table);
       e = list_next (e))
    {
      struct frame_entry *frame = list_entry (e, struct frame_entry, elem);
      if (frame->owner == owner && frame->upage == round_upage)
        {
          frame->pinned = false;
          break;
        }
    }
  lock_release (&frame_lock);
}
