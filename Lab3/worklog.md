# 5.17
**15:00-18:00**, **21:00-22:00**
- Doc reading.
- Overview VA-PA translation procedure.
- Create vm data structures files:
    - vm/spt (per-process)
    - vm/frame (global)
    - vm/swap (global)
- [x] modify `struct thread` :`struct hash spt`;

# 5.18
**10:00-11:00**
- use frame allocator in load&stack-setup in `process.c`
- frame init&free path in `init.c` and `pagedir.c`

check: all lab2 tests still passed(except the old output disorder?????)

- [x] SPT, and record info whe load&stack-setup in `process.c`.
- [ ] spt-hash, frame-list, swap-bitmap? partly merge into unified data structure?

# 5.19
**15:30-17:00**
- SPT design
    -`vm/spt.h` and `vm/spt.c`

# 5.21
- Add spt in `struct thread`

# 5.22 
**18:30-20:30**
- spt_init,spt_destroy,spt_insert in `process.c`
    - [x]still eager-loading!
```text
load()
  -> spt_init()

load_segment()
  -> process_spt_insert(file-backed page entries)

setup_stack()
  -> process_spt_insert(initial stack page)
```
- `process.c`: change to lazy-loading.
    - load_segment() 在 VM 下不再分配 frame、不再 file_read()、不再 install_page()，只为每个 ELF 页插入 SPT entry，状态是 VM_PAGE_NOT_LOADED，kpage = NULL。
    - stack page: still eager-load. 
- `page_fault()` in `exception.c` is now responsible for loading via SPT.
    
- [ ] page reclamation in `process_exit()`. `process_exit()/munmap()/eviction` 负责根据 SPT 清理或写回。


# 5.24
**11:00-12:00**
- Per-page resource free in `process_exit()`->`spt_destroy_entry()`.
- [x] swap management.
- [ ] eviction

check: `process.c`的所有改动都写在`#ifdef VM`编译宏之内.应该不会影响LAB2.


# 5.26
**15:15-17:00**
- Add swap table
- Naive eviction.
- [ ]accessed&dirty bit?  --- after a naive eviction implementation
- [ ]eviction victim frame的去处, 当前是简化实现: 统一写到swap.


