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
    
- [x] page reclamation in `process_exit()`. `process_exit()/munmap()/eviction` 负责根据 SPT 清理或写回。


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
- [x]accessed&dirty bit?  --- after a naive eviction implementation
- [x]eviction victim frame的去处, 当前是简化实现: 统一写到swap.
  - now: dirty || swap-backed -> swap; clean && file-backed -> discard.

### 当前Aliasing 策略:
kernel和process都用 USER_VA 作为物理内存访问入口

**upage vs kpage**
```text
user VA upage  ──pagedir──> physical frame <──direct map── kernel VA kpage
```

- uaddr：任意用户虚拟地址，可以不页对齐。
- upage：页对齐后的用户虚拟页起始地址。
- kaddr：任意内核虚拟地址。
- kpage：页对齐后的内核虚拟页起始地址，通常对应一个物理 frame。


# 5.27
**13:00-15:00**
```
1. syscall 访问 lazy page 会失败。
syscall.c (line 98) 的 check_user_vaddr() 要求 pagedir_get_page(...) != NULL，这会把尚未加载但 SPT 合法的 lazy page 直接判死，导致系统调用里的用户 buffer 不能靠 page fault 装入。这里需要改成允许 SPT 中存在的 not-present page，或者直接用 get_user/put_user 触发 fault。

2. Exercise 1.2 的同步/并行性还不够。
不只是 replacement algorithm 随机的问题。当前 eviction 在 swap_out() 之前没有先从 owner pagedir 取消映射，victim owner 理论上可能在 eviction I/O 期间继续访问/修改该页。见 frame.c (line 181)。也缺少 per-frame / per-SPT entry 级别的同步设计。
```

- syscall modification: `check_user_vaddr()`放行未加载的page.
  现在的合法地址: 页已经在内存里/页虽然不在内存里，但 SPT 知道怎么把它加载进来。

```text
copy_in()
  -> check_user_vaddr()
       地址 present? OK
       或 SPT 有记录? OK
       否则非法，exit(-1)
  -> get_user()
       页已在内存：直接读成功
       页不在内存但合法：触发 page fault
            -> page_fault()
            -> spt_find()
            -> vm_load_page()
            -> pagedir_set_page()
       page fault 返回后，get_user 重新读成功
```

