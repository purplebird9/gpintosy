#include "vm/spt.h"
#include <debug.h>
#include <hash.h>
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/swap.h"

static unsigned spt_hash (const struct hash_elem *e, void *aux);
static bool spt_less (const struct hash_elem *a,
                      const struct hash_elem *b,
                      void *aux);
static void spt_destroy_entry (struct hash_elem *e, void *aux);

/** Initializes one process's supplemental page table.

   This should be called when a user process is created.  A kernel-only
   thread does not need an SPT. */
bool
spt_init (struct hash *spt)
{
  ASSERT (spt != NULL);
  return hash_init (spt, spt_hash, spt_less, NULL);
}

/** Destroys SPT and frees every entry in spt_destroy_entry(). */
void
spt_destroy (struct hash *spt)
{
  ASSERT (spt != NULL);
  hash_destroy (spt, spt_destroy_entry);
}

/** Inserts SPTE into SPT.

   Returns false if another entry already owns the same UPAGE.  SPTE is
   not copied, so the caller should allocate it from kernel memory and
   stop using it directly after insertion except through SPT lookup. */
bool
spt_insert (struct hash *spt, struct spt_entry *spte)
{
  ASSERT (spt != NULL);
  ASSERT (spte != NULL);
  ASSERT (spte->upage != NULL);
  ASSERT (pg_ofs (spte->upage) == 0);

  return hash_insert (spt, &spte->elem) == NULL;
}

/** Finds the SPT entry for UPAGE.

   UPAGE may be any address inside the page; it is rounded down before
   lookup because page_fault() receives the exact faulting byte address. */
struct spt_entry *
spt_find (struct hash *spt, const void *upage)
{
  //hash_find 要求传入一个 struct hash_elem * 指针。但现在只有一个upage，并没有一个包含这个upage的 hash_elem。
  //为了解决这个问题，必须临时“伪造”一个包含该地址的结构体--probe.
  struct spt_entry probe;
  struct hash_elem *e;

  ASSERT (spt != NULL);

  probe.upage = pg_round_down (upage);
  // Hash_find触发"回调",自动调用hash_init时传的spt_hash函数,计算出hash code,找到对应bucket,遍历bucket中的链表.
  e = hash_find (spt, &probe.elem);
  return e != NULL ? hash_entry (e, struct spt_entry, elem) : NULL;
}

/** Removes and returns the entry for UPAGE, or NULL if absent.

   The caller owns the returned entry and must eventually call
   spt_free_entry() after releasing any frame/swap/file resources tied
   to it. */
struct spt_entry *
spt_delete (struct hash *spt, const void *upage)
{
  struct spt_entry probe;
  struct hash_elem *e;

  ASSERT (spt != NULL);

  probe.upage = pg_round_down (upage);
  e = hash_delete (spt, &probe.elem);
  return e != NULL ? hash_entry (e, struct spt_entry, elem) : NULL;
}

/** Frees one SPT entry allocated from kernel heap memory. */
void
spt_free_entry (struct spt_entry *spte)
{
  free (spte);
}

/* spte's UPAGE -> unsigned Hash Code. 
   Hash table will use this code to find the entry quickly. */
static unsigned
spt_hash (const struct hash_elem *e, void *aux UNUSED) 
{
  const struct spt_entry *spte = hash_entry (e, struct spt_entry, elem);
  return hash_bytes (&spte->upage, sizeof spte->upage); 
}

/* Order SPT entries by user page address. */
static bool
spt_less (const struct hash_elem *a,
          const struct hash_elem *b,
          void *aux UNUSED)
{
  const struct spt_entry *sa = hash_entry (a, struct spt_entry, elem);
  const struct spt_entry *sb = hash_entry (b, struct spt_entry, elem);
  return (uintptr_t) sa->upage < (uintptr_t) sb->upage;
}

/* Frees an SPT entry during hash destruction. */
static void
spt_destroy_entry (struct hash_elem *e, void *aux UNUSED)
{
  struct spt_entry *spte = hash_entry (e, struct spt_entry, elem);
  struct thread *cur = thread_current ();
  void *kpage = NULL;
  bool free_swap = false;
  size_t swap_slot = (size_t) -1;

  lock_acquire (&spte->lock);
  //LAB3A-B6: don't free an entry while eviction/loading uses it.
  while (spte->state == VM_PAGE_EVICTING
         || spte->state == VM_PAGE_LOADING)
    cond_wait (&spte->cv, &spte->lock);

  // LAB3A: free the resident frame && pagedir if the page is still loaded; swap slot 
  if (spte->state == VM_PAGE_LOADED && spte->kpage != NULL)
    {
      if (cur->pagedir != NULL
          && pagedir_get_page (cur->pagedir, spte->upage) != NULL)
        // Also clear the page in pagedir.
        pagedir_clear_page (cur->pagedir, spte->upage);
      kpage = spte->kpage;
      spte->state = VM_PAGE_NOT_LOADED;
      spte->kpage = NULL;
    } 
  // LAB3A: free the swap slot if the page is still in swap. 
  // (If the page is loaded, its swap slot should have been freed when it was loaded.)
  else if (spte->type == VM_PAGE_SWAP && spte->swap_slot != (size_t) -1)
    { 
      free_swap = true;
      swap_slot = spte->swap_slot;
      spte->swap_slot = (size_t) -1;
    }
  lock_release (&spte->lock);

  if (kpage != NULL)
    frame_free (kpage);
  if (free_swap)
    swap_free (swap_slot);

  spt_free_entry (spte);
}
