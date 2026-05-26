#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdbool.h>
#include <stddef.h>

/** Initializes the global swap table from the BLOCK_SWAP device. */
void swap_table_init (void);

/** Returns the number of page-sized swap slots. */
size_t swap_slot_count (void);

/** Writes one page to swap and stores the allocated slot in SLOT. */
bool swap_out (void *kpage, size_t *slot);

/** Reads SLOT back into KPAGE and releases the slot. */
bool swap_in (size_t slot, void *kpage);

/** Releases SLOT without reading it, used when a process exits. */
void swap_free (size_t slot);

#endif /**< vm/swap.h */
