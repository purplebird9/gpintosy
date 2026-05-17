/** Global Frame Table

   Frame table only tracks frames from the user pool.  
   Kernel pool allocations remain non-pageable so VM metadata can safely live there. */

#include "vm/frame.h"
#include <debug.h>
#include <list.h>
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static struct list frame_table;          /**< Global list of user frames. */
static struct lock frame_lock;           /**< Protects the GLOBAL frame_table. */

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

  ASSERT ((flags & PAL_USER) != 0); // must be from user pool
  ASSERT (upage == NULL || pg_ofs (upage) == 0); // upage must be aligned

  kpage = palloc_get_page (flags); // try to obtain a free user frame
  if (kpage == NULL)
    return NULL;                // TODO: eviction alg.

  frame = malloc (sizeof *frame); // allocate frame entry
  if (frame == NULL) // if malloc fails
    {
      palloc_free_page (kpage);
      return NULL;
    }

  frame->kpage = kpage;
  frame->owner = thread_current ();
  frame->upage = upage;
  frame->pinned = false;

  lock_acquire (&frame_lock); //sync
  list_push_back (&frame_table, &frame->elem);
  lock_release (&frame_lock);
  return frame;
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
  struct frame_entry *frame;

  if (kpage == NULL)
    return;

  frame = frame_lookup (kpage);
  if (frame == NULL)
    return;

  lock_acquire (&frame_lock);
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
