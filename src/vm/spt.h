/** SPT: Supplemental Page Table.
   The hardware page table only remembers whether a page is present
   and which frame it maps to.  The supplemental page table records
   the missing information needed by page_fault(): file location,
   swap slot, zero-fill policy, and write permission. */

#ifndef VM_SPT_H
#define VM_SPT_H

#include <hash.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "filesys/off_t.h"
#include "threads/synch.h"

struct file;

/** Where the data for a user page can be loaded from.*/
enum vm_page_type
  {
    VM_PAGE_FILE,              /**< Backed by an executable/file page. */
    VM_PAGE_ZERO,              /**< Demand-zero page, e.g. stack growth. */
    VM_PAGE_SWAP,              /**< Contents currently live in swap. */
    VM_PAGE_MMAP               /**< Backed by an mmap() mapping. */
  };


/**  Whether the page currently has a frame. */
enum vm_page_state 
  {
    VM_PAGE_NOT_LOADED,        /**< No frame is installed in pagedir. */
    VM_PAGE_LOADED,            /**< KPAGE is installed for UPAGE. */
    VM_PAGE_LOADING,           /**< LAB3A-B6: fault is reading this page in. */
    VM_PAGE_EVICTING           /**< LAB3A-B6: eviction owns page state update. */
  };

/** One supplemental page table entry.

   UPAGE(VA) is the hash key and must be page-aligned.  
   The rest of the fields tells the kernel how to bring the page into memory when UPAGE faults.
   File-backed pages use FILE/OFS/READ_BYTES/ZERO_BYTES.  
   Swapped pages use SWAP_SLOT.  
   KPAGE is only meaningful while STATE is LOADED. */
struct spt_entry
  {
    void *upage;               /**< User virtual page, page-aligned key. */
    bool writable;             /**< Whether user code may write this page. */

    enum vm_page_type type;    /**< Backing store kind. */
    enum vm_page_state state;  /**< Whether the page currently has a frame. */

    /* How to bring a page back from a file */
    struct file *file;         /**< File backing, if any. */
    off_t ofs;                 /**< Page-aligned file offset. */
    uint32_t read_bytes;       /**< Bytes to read from FILE into the page. */
    uint32_t zero_bytes;       /**< Bytes to zero after READ_BYTES. */

    size_t swap_slot;          /**< Swap slot index for VM_PAGE_SWAP. */
    void *kpage;               /**< Kernel VA of resident user frame. */

    int md;                 /**< mmap id; -1 for non-mmap pages. */
    struct lock lock;          /**< LAB3A-B6: protects page state transitions. */
    struct condition cv;       /**< LAB3A-B6: wait for loading/eviction finish. */
    struct hash_elem elem;     /**< Element in the per-process SPT hash. */
  };

bool spt_init (struct hash *spt);
void spt_destroy (struct hash *spt);

bool spt_insert (struct hash *spt, struct spt_entry *spte);
struct spt_entry *spt_find (struct hash *spt, const void *upage);
struct spt_entry *spt_delete (struct hash *spt, const void *upage);
void spt_free_entry (struct spt_entry *spte);

#endif /**< vm/spt.h */
