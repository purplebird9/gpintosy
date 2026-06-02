# Project 3a: Virtual Memory

## Preliminaries

>Fill in your name and email address.

Yuqi Su <iyukisu@stu.pku.edu.cn>

>If you have any preliminary comments on your submission, notes for the TAs, please give them here.


>Please cite any offline or online sources you consulted while preparing your submission, other than the Pintos documentation, course text, lecture notes, and course staff.


## Page Table Management

#### DATA STRUCTURES

>A1: Copy here the declaration of each new or changed struct or struct member, global or static variable, typedef, or enumeration.  Identify the purpose of each in 25 words or less.

```c
enum vm_page_type { VM_PAGE_FILE, VM_PAGE_ZERO, VM_PAGE_SWAP, VM_PAGE_MMAP };
```
Page backing source.

```c
enum vm_page_state {
  VM_PAGE_NOT_LOADED, VM_PAGE_LOADED, VM_PAGE_LOADING, VM_PAGE_EVICTING
};
```
Current residency or transition state.

```c
struct spt_entry {
  void *upage;
  bool writable;
  enum vm_page_type type;
  enum vm_page_state state;
  struct file *file;
  off_t ofs;
  uint32_t read_bytes, zero_bytes;
  size_t swap_slot;
  void *kpage;
  int mapid;
  struct lock lock;
  struct condition cv;
  struct hash_elem elem;
};
```
Per-page metadata for lazy loading, swap, mmap, and page-state synchronization.

```c
struct thread {
#ifdef VM
  struct hash spt;
#endif
};
```
Per-process supplemental page table.

#### ALGORITHMS

>A2: In a few paragraphs, describe your code for accessing the data
>stored in the SPT about a given page.

Each process owns a hash-table SPT keyed by page-aligned `upage`.  `spt_find()`
rounds an arbitrary fault address down to its page base, builds a temporary
probe entry, and uses `hash_find()` to retrieve the corresponding
`struct spt_entry`.

`load_segment()` registers file-backed or zero pages in the SPT instead of
loading them immediately.  On a not-present page fault, `page_fault()` looks up
the address in the SPT and calls `vm_load_page()`, which allocates a frame,
loads from file/swap/zero fill, installs the PTE, and marks the SPTE loaded.

>A3: How does your code coordinate accessed and dirty bits between
>kernel and user virtual addresses that alias a single frame, or
>alternatively how do you avoid the issue?

The code avoids depending on the kernel alias for accessed/dirty state.
Replacement and eviction inspect only the user mapping: `pagedir_is_accessed`,
`pagedir_set_accessed`, and `pagedir_is_dirty` are called on `owner->pagedir`
and `frame->upage`.  Kernel `kpage` is used for copying data, not for
accessed/dirty decisions.

#### SYNCHRONIZATION

>A4: When two user processes both need a new frame at the same time,
>how are races avoided?

`frame_lock` serializes frame-table allocation, victim selection, eviction, and
frame ownership updates.  New and reused frames are pinned until their page
contents and page-table/SPT state are stable, so concurrent eviction skips
frames still being initialized.

#### RATIONALE

>A5: Why did you choose the data structure(s) that you did for
>representing virtual-to-physical mappings?

A page-based hash table matches the unit of page faults and eviction.  It gives
direct lookup by fault address and avoids scanning segment records.  The
hardware page directory still records resident mappings; the SPT records how
to make absent pages present again.

## Paging To And From Disk

#### DATA STRUCTURES

>B1: Copy here the declaration of each new or changed struct or struct member, global or static variable, typedef, or enumeration.  Identify the purpose of each in 25 words or less.

```c
struct frame_entry {
  void *kpage;
  struct thread *owner;
  void *upage;
  bool pinned;
  struct list_elem elem;
};
```
One user frame and its current owner/page.

```c
static struct list frame_table;
static struct lock frame_lock;
static struct list_elem *clock_hand;
```
Global frame list, lock, and clock replacement cursor.

```c
static struct block *swap_device;
static struct bitmap *swap_map;
static struct lock swap_lock;
static size_t swap_slots;
```
Swap device, slot bitmap, lock, and slot count.

#### ALGORITHMS

>B2: When a frame is required but none is free, some frame must be
>evicted.  Describe your code for choosing a frame to evict.

Eviction uses a clock algorithm over `frame_table`.  It skips pinned or invalid
frames.  If a candidate page's accessed bit is set, the bit is cleared and the
page gets a second chance.  A candidate with accessed bit clear is evicted.

>B3: When a process P obtains a frame that was previously used by a
>process Q, how do you adjust the page table (and any other data
>structures) to reflect the frame Q no longer has?

Eviction finds Q's SPTE using the victim frame's `owner` and `upage`, marks it
`VM_PAGE_EVICTING`, and clears Q's PTE.  Dirty or swap-backed pages are written
to swap; clean file-backed pages are discarded.  The SPTE is then updated to
not-loaded with `kpage = NULL`, and the frame entry is reassigned to P.

#### SYNCHRONIZATION

>B5: Explain the basics of your VM synchronization design.  In
>particular, explain how it prevents deadlock.  (Refer to the
>textbook for an explanation of the necessary conditions for
>deadlock.)

The design uses `frame_lock` for the global frame table, `swap_lock` for swap,
and one lock/condition variable per SPTE.  Page loading and eviction publish
temporary states (`VM_PAGE_LOADING`, `VM_PAGE_EVICTING`) so other faults wait
on that page only.  Waits use `cond_wait`, which releases the SPTE lock while
sleeping, and the code avoids holding multiple SPTE locks at once.

>B6: A page fault in process P can cause another process Q's frame
>to be evicted.  How do you ensure that Q cannot access or modify
>the page during the eviction process?  How do you avoid a race
>between P evicting Q's frame and Q faulting the page back in?

Before eviction I/O, the victim SPTE is marked `VM_PAGE_EVICTING` and Q's PTE
is cleared, so Q can no longer access the frame.  If Q faults on the same page,
it waits on the SPTE condition variable until eviction records the new backing
store and broadcasts.

>B7: Suppose a page fault in process P causes a page to be read from
>the file system or swap.  How do you ensure that a second process Q
>cannot interfere by e.g. attempting to evict the frame while it is
>still being read in?

During fault handling the SPTE is marked `VM_PAGE_LOADING`, and the allocated
frame is pinned.  Other faults wait on the SPTE, and eviction skips pinned
frames.  The frame is unpinned only after the page is installed and the SPTE is
marked loaded.

>B8: Explain how you handle access to paged-out pages that occur
>during system calls.  Do you use page faults to bring in pages (as
>in user programs), or do you have a mechanism for "locking" frames
>into physical memory, or do you use some other design?  How do you
>gracefully handle attempted accesses to invalid virtual addresses?

Syscalls allow valid lazy pages to fault in normally: an address is valid if it
is present or has an SPT entry.  `get_user()`/`put_user()` then trigger the
fault if needed.  `read()` and `write()` pin all user-buffer pages during I/O
and unpin them afterward.  Null, kernel, wrapped, or unknown addresses cause
`sys_exit(-1)`.

#### RATIONALE

>B9: A single lock for the whole VM system would make
>synchronization easy, but limit parallelism.  On the other hand,
>using many locks complicates synchronization and raises the
>possibility for deadlock but allows for high parallelism.  Explain
>where your design falls along this continuum and why you chose to
>design it this way.

The design is between one global VM lock and fully fine-grained locking.
Global shared structures have simple global locks, while each page has its own
SPTE lock/condition variable.  This preserves parallelism for unrelated pages
without making the locking hierarchy too complicated.
