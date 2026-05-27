/** Global Frame Table

   Frame table only tracks frames from the user pool.  
   Kernel pool allocations remain non-pageable so VM metadata can safely live there. */

#include "vm/frame.h"
#include <debug.h>
#include <list.h>
#include <random.h>
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

static struct frame_entry *frame_evict (void);
static struct frame_entry *frame_select_victim_random_locked (void);
static bool frame_evict_page (struct frame_entry *victim);

/** Initializes the global frame table.*/
void
frame_table_init (void)
{
  list_init (&frame_table);
  lock_init (&frame_lock);
}

/** Allocates one user frame and records it in the frame table.

   FLAGS must include PAL_USER.  
   Now eviction is not yet implemented, so this function 
   returns NULL when palloc cannot find a free user frame.   */
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
      frame = frame_evict (); // TODO: optimize algorithm in frame_evict().
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

   For now the algorithm is a random placeholder in frame_select_victim_random_locked() */
static struct frame_entry *
frame_evict (void)
{
  struct frame_entry *victim;

  lock_acquire (&frame_lock);
  victim = frame_select_victim_random_locked (); // EVICTION
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

/** Random placeholder alg.

   Caller must hold frame_lock.  
   Pinned frames are skipped because they may be in the middle of disk or syscall buffer I/O. */
static struct frame_entry *
frame_select_victim_random_locked (void)
{
  struct list_elem *e;
  size_t unpinned_cnt = 0;
  size_t target;

  ASSERT (lock_held_by_current_thread (&frame_lock));

  for (e = list_begin (&frame_table); e != list_end (&frame_table);
       e = list_next (e))
    {
      struct frame_entry *frame = list_entry (e, struct frame_entry, elem);
      if (!frame->pinned)
        unpinned_cnt++;
    }

  if (unpinned_cnt == 0)
    return NULL;

  target = random_ulong () % unpinned_cnt;
  for (e = list_begin (&frame_table); e != list_end (&frame_table);
       e = list_next (e))
    {
      struct frame_entry *frame = list_entry (e, struct frame_entry, elem);
      if (!frame->pinned)
        {
          if (target == 0)
            return frame;
          target--;
        }
    }

  NOT_REACHED ();
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

  spte->state = VM_PAGE_EVICTING; //LAB3A-B6: faulting threads must wait.
  if (victim->owner->pagedir != NULL)
    //LAB3A-B6: stop owner from writing the frame during eviction.
    pagedir_clear_page (victim->owner->pagedir, upage);

  // dirty is read-only through USER.
  dirty = victim->owner->pagedir != NULL
          && pagedir_is_dirty (victim->owner->pagedir, upage);
  must_swap = spte->type == VM_PAGE_SWAP || dirty; // swap case: swap-backed || dirty

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

  list_remove (&frame->elem);
  lock_release (&frame_lock);

  palloc_free_page (kpage);
  free (frame);
}

/** Prevents KPAGE from being evicted

   Pin pages while the kernel is copying user buffers or doing disk
   I/O into a frame.  Evicting such a page in the middle of I/O can
   corrupt data or deadlock the VM path. */
void
frame_pin (void *kpage)
{
  struct frame_entry *frame = frame_lookup (kpage);

  if (frame != NULL)
    frame->pinned = true;
}

/** Allows KPAGE to be evicted again. */
void
frame_unpin (void *kpage)
{
  struct frame_entry *frame = frame_lookup (kpage);

  if (frame != NULL)
    frame->pinned = false;
}
