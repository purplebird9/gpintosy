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
- [x] eviction

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

**Check**

FAIL tests/vm/page-parallel --- 逻辑问题

```text
Putting 'page-parallel' into the file system...
Putting 'child-linear' into the file system...
Erasing ustar archive...
Executing 'page-parallel':
(page-parallel) begin
(page-parallel) exec "child-linear"
(page-parallel) exec "child-linear"
(page-parallel) exec "child-linear"
child-linear: exit(66)
(page-parallel) exec "child-linear"
(page-parallel) wait for child 0
(page-parallel) wait for child 1
child-linear: exit(66)
child-linear: exit(66)
```

FAIL tests/filesys/base/sm-seq-random ---old problem(?)

```text
Putting 'sm-seq-random' into the file system...
Erasing ustar archive...
Executing 'sm-seq-random':
(sm-seq-random) begin
(sm-seq-random) create "nibble"
(sm-seq-random) open "nibble"
(sm-seq-random) writing "nibble"
(sm-seq-random) close "nibble"
(sm-seq-random) open "nibble" for verification
(sm-seq-random) verified contents of "nibble"
(sm-seq-random) close "nibble"
(sm-seq-random) end
sm-seq-random: exit(0)
Execution oExecution of 'sm-seq-random' complete.
```

FAIL tests/userprog/close-normal ---old problem!!
```diff
Acceptable output:
  (close-normal) begin
  (close-normal) open "sample.txt"
  (close-normal) close "sample.txt"
  (close-normal) end
  close-normal: exit(0)
Differences in `diff -u' format:
  (close-normal) begin
  (close-normal) open "sample.txt"
  (close-normal) close "sample.txt"
  (close-normal) end
- close-normal: exit(0)
+ close-normal: exit(0close-normal: exit(0)
```

FAIL tests/userprog/bad-jump ---old prob?

```text
Putting 'bad-jump' into the file system...
Erasing ustar archive...
Executing 'bad-jump':
(bad-jump) begin
Page fault at 0: not present error reading page in user context.
bad-jump: exit(-1)
EExecution of 'bad-jump' complete.
Timer: 62 ticks
Thread: 35 idle ticks, 25 kernel ticks, 2 user ticks
hda2 (filesys): 57 reads, 148 writes
hda3 (scratch): 71 reads, 2 writes
hda4 (swap): 0 reads, 0 writes
```

**15:00-20:00**
猜测: some synchronization problem.

- page-parallel: upage is aligned ASSERTION fail. --> round upage instead of ASSERT.


并发场景:
线程 A: eviction 选中 victim frame
线程 A: 释放 frame_lock，准备驱逐
线程 B: victim 的 owner 退出，spt_destroy -> frame_free -> free(frame_entry)
线程 A: 继续使用已经被 free 的 victim，frame->kpage 变成野值


## Sync:
对照doc修改同步设计.
- [x]eviction 在 swap_out() 之前没有先从 owner pagedir 取消映射，victim owner可能在 eviction期间继续修改该页。
- [x]page load的时候另一个进程不能interfere(e.g. evict).
- [ ]缺少 per-frame / per-SPT entry 级别的同步设计.

## Console Disorder:
- fix `process_exit()`: child prinf "xxx: exit(n)" -> sema_up()唤醒父进程 -> parent printf:" Execution of 'xxx' complete."

exit处不再出现紊乱, 但是执行过程中还是有格式紊乱.

```diff
  Differences in `diff -u' format:
  (sm-seq-random) begin
  (sm-seq-random) create "nibble"
  (sm-seq-random) open "nibble"
  (sm-seq-random) writing "nibble"
  (sm-seq-random) close "nibble"
  (sm-seq-random) open "nibble" for verification
- (sm-seq-random) verified contents of "nibble"
+ (sm-seq-random) verifi(sm-seq-random) verified contents of "nibble"
  (sm-seq-random) close "nibble"
  (sm-seq-random) end
```

```diff
Differences in `diff -u' format:
- (sc-bad-arg) begin
+ ((sc-bad-arg) begin
  sc-bad-arg: exit(-1)
```


TODO:
1. Replacement Algorithm
2. Fix Synchronization.


20:30 all passed?记得复现一下.

**21:00-22:00**
fix:
- pinned=true during frame initialization(SPTE, PTE)
- always hold lock until eviction ends.---so that other proc does not interfere?x
  - frame_lock 只保护frame table.
  - Q user instruction -> MMU -> Q.pagedir/PTE -> physical frame, 这条路径不会拿frame_lock.
- Solve design doc-B6: 
  - P evicts Q's frame->I/O(swap_out), very slow!->Q scheduled->Q shouldn't modify the frame!
  - 现在 eviction 会先把被驱逐页的 SPT 状态设为 VM_PAGE_EVICTING，立刻清掉 owner 的 PTE，阻止 Q 继续访问/修改该页，然后再做 swap I/O。Q 如果在这期间 fault 同一页，会在 spte->lock/cv 上等待，直到 eviction 完成。


# 5.28
**10:00-12:00**
猜想: 
1. syscall()期间没有保护user buffer, 导致putbuf()期间被____打断, 恢复后又从buffer开头重新输出.
2. 随机eviction placeholder导致的偶发紊乱?改成LRU?

Solution:
1. Replacement Algorithm: clock.
2. Pins all pages covered by buffer during sys_read(), sys_write().
  - this prevents the VM eviction path from moving or changing user buffer pages while the kernel is doing copy_in/copy_out or file/console I/O, which could corrupt output.

Result:紊乱频率下降但是仍然存在, 可能还有一条path.

**15:00-17:00**
- 消除pin_user_buffer里get_page()和frame_pin()之间的竞态窗口:
 frame_pin() ---> frame_pin_user_page(). 后者把lookup和pin用一个临界区包裹

Result: passed!!!!

- **隐患:** pin 仍然是 bool pinned，不是 pin_count。如果同一 frame 被嵌套 pin，两次 pin 后一次 unpin 可能提前解除保护。

---
 

# 6.4
**10:00-12:00**
Implement stack growth
- [x] First stack page not loaded lazily
- [x] Obtain the current value of user program's stack ptr. `esp` in `struct intr_frame`, in`syscall_handler` and `page_fault()`
- [x] Distinguish stack accesses. Allocate additional pages only if they "appear" to be stack accesses.
- [x] Limit stack size 8MB
- [x] Stack pages ARE eviction candidates, written to SWAP.

check:
```text
FAIL tests/vm/pt-grow-stk-sc
FAIL tests/vm/page-merge-stk
```

**13:00-15:00**
- per-thread mmap list.

Note:
`init_thread()`:初始化一个新的 struct thread 里的字段.
```c
list_init (&t->mmap_list);
t->next_md = 1;
```
(spt也是per-thread状态, 但是spt_init放在process.c的load()里, 因为它只对用户进程有意义.)

`init.c`:初始化 kernel 启动时的全局模块,比如
```c
frame_table_init ();
swap_table_init ();
filesys_init ();
```
- mmap load: lazy
- mmap eviction: write back to file

**18:30-19:30**
- syscall: mmap, munmap
  - mmap: find open base_file->create separate mapping_file->new struct mapping->insert to SPT->add to thread::mmap_list
  - munmap:remove from SPT->clear page table-> write back->free frame->remove mmap list_elem->close mapping file->free struct mapping.
- implicit unmap all in process_exit().

Note:
```markdown
 `mmap_insert_page()` 做的是 **lazy mapping**，不是立刻把文件内容装进内存。

`mmap` 成功时只需要告诉内核：

> “这个用户虚拟页 `upage` 将来如果被访问，它应该从 `file + ofs` 这个位置读 `read_bytes`，剩下 `zero_bytes` 补 0，并且它属于 mmap mapping `md`。”

此时没有分配 frame，也没有实际读文件，所以 **page table 里不应该建立映射**。真正的 page table 映射是在第一次访问该 mmap 地址、发生 page fault 时建立的：

1. 用户访问 `addr`
2. CPU 查 page table，发现没有 present 映射
3. 触发 page fault
4. `page_fault()` 查 SPT，找到 `VM_PAGE_MMAP`
5. `vm_load_page()` 分配 frame、读文件、补零
6. `pagedir_set_page()` 把 `upage -> kpage` 装进 page table

所以分工是：

- `mmap_insert_page()`：登记“未来怎么加载这个页”
- `page_fault()` / `vm_load_page()`：真正加载 page，并更新 page table
- eviction：必要时清 page table，并根据类型决定 swap / write back / discard
- `munmap()`：删除 SPT entry，必要时写回 dirty page

如果 `mmap_insert_page()` 现在就操作 page table，就变成 eager loading 了：必须分配 frame、读文件、建立映射。这和 Pintos 对 mmap 的要求不符，因为 mmaped regions 应该是 lazy-loaded。
```

make check: 2/113
```text
FAIL tests/vm/pt-grow-stk-sc
FAIL tests/vm/page-merge-stk
```


# 6.5
**14:00-15:00**

**tests/vm/pt-grow-stk-sc:** 要求stack growth可以发生在syscall内部.
但是sys_read()中没有放行将要增长的stack page, 直接exit(-1)

```c
sys_read()
  -> check_user_buffer(buffer, size)
      -> check_user_vaddr(each page)
          -> pagedir_get_page(...) != NULL ? OK
          -> spt_find(...) != NULL ? OK
          -> else sys_exit(-1)
```
Sol: 在check_user_buffer()里放行stack access && 完成stack growth.
Pass.



