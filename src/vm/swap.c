#include "vm/swap.h"
#include <bitmap.h>
#include <debug.h>
#include <stdint.h>
#include "devices/block.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

static struct block *swap_device;        /**< Pintos swap block device: BLOCK_SWAP. */
static struct bitmap *swap_map;          /**< True when slot is in use. */
static struct lock swap_lock;            /**< Protects swap_map/device I/O. */
static size_t swap_slots;                /**< Number of page-sized slots. */

static bool swap_slot_valid (size_t slot);

/** Initializes global swap bookkeeping.

   Swap is lazily allocated: no page owns a swap slot until eviction
   actually writes that page out.  
   A swap slot = a page = 8 sectors = 4096 bytes*/
void
swap_table_init (void)
{
  swap_device = block_get_role (BLOCK_SWAP);
  lock_init (&swap_lock);

  // error check
  if (swap_device == NULL)
    {
      swap_map = NULL;
      swap_slots = 0;
      return;
    }

  swap_slots = block_size (swap_device) / SECTORS_PER_PAGE;
  swap_map = bitmap_create (swap_slots);
}

/** Returns the number of page-sized swap slots available. */
size_t
swap_slot_count (void)
{
  return swap_slots;
}

/** Writes KPAGE to a free swap slot.

   Returns false when no swap device or no free slot exists.  A caller
   that cannot evict without swap should panic at a higher level.
   Called by frame_evict()->frame_swap_out() */
bool
swap_out (void *kpage, size_t *slot)
{
  size_t free_slot;
  block_sector_t sector;
  size_t i;

  ASSERT (kpage != NULL);
  ASSERT (slot != NULL);
  ASSERT (pg_ofs (kpage) == 0);

  if (swap_device == NULL || swap_map == NULL)
    return false;

  lock_acquire (&swap_lock);
  free_slot = bitmap_scan_and_flip (swap_map, 0, 1, false);// find a free slot and mark it as used
  if (free_slot == BITMAP_ERROR)
    {
      lock_release (&swap_lock);
      return false;
    }

  // SECTORS_PER_PAGE = 4096/512 = 8.
  sector = free_slot * SECTORS_PER_PAGE;// calculate the starting sector of the slot
  for (i = 0; i < SECTORS_PER_PAGE; i++) // write 8 sectors for 1 page
    block_write (swap_device, sector + i,
                 (uint8_t *) kpage + i * BLOCK_SECTOR_SIZE);

  lock_release (&swap_lock);

  *slot = free_slot;// return the slot index to caller
  return true;
}

/** Reads SLOT into KPAGE and frees the slot.

   Caller:
   After a successful swap-in, the SPT entry should stop referring to
   SLOT because the only authoritative copy is now the resident frame. */
bool
swap_in (size_t slot, void *kpage)
{
  block_sector_t sector;
  size_t i;

  ASSERT (kpage != NULL);
  ASSERT (pg_ofs (kpage) == 0);

  if (!swap_slot_valid (slot))
    return false;

  lock_acquire (&swap_lock);
  if (!bitmap_test (swap_map, slot))
    {
      lock_release (&swap_lock);
      return false;
    }

  sector = slot * SECTORS_PER_PAGE;// calculate the starting sector of the slot
  for (i = 0; i < SECTORS_PER_PAGE; i++)
    block_read (swap_device, sector + i,
                (uint8_t *) kpage + i * BLOCK_SECTOR_SIZE);

  bitmap_reset (swap_map, slot);// mark the slot as free
  lock_release (&swap_lock);

  return true;
}

/** Frees SLOT without reading it.

   Use this when a process exits while one of its pages is still swapped
   out.  Resident pages should be released through frame_free() instead. */
void
swap_free (size_t slot)
{
  if (!swap_slot_valid (slot))
    return;

  lock_acquire (&swap_lock);
  if (bitmap_test (swap_map, slot))
    bitmap_reset (swap_map, slot);
  lock_release (&swap_lock);
}

static bool
swap_slot_valid (size_t slot)
{
  return swap_device != NULL && swap_map != NULL && slot < swap_slots;
}
