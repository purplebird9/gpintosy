**Segment 方案**
按 ELF segment 或 mmap 区间记录，比如：

```c
[0x8048000, 0x8049000) from file ofs 0, writable=false
[0x8049000, 0x804b000) from file ofs 4096, writable=true
```

优点是条目少，和 ELF loader 的概念接近。缺点是 page fault 时还要在区间里查找、计算该页的具体 file offset/read_bytes/zero_bytes；swap、mmap、dirty write-back、逐页 eviction 都会变麻烦。

**Page 方案**
每个用户虚拟页一个 SPT entry，比如：

```c
upage = 0x8048000
type = VM_PAGE_FILE
file = executable
ofs = 0
read_bytes = 4096
zero_bytes = 0
writable = false
state = VM_PAGE_NOT_LOADED
```

优点是 page fault 时直接查 `upage`，eviction/swap/mmap/stack growth 都天然是“逐页”处理。缺点是 entry 多一点，但 Pintos 项目里这点开销很值得。

我建议你用 **page-based SPT**。你仓库里 [src/vm/spt.h](/d:/Desktop/pintos/src/vm/spt.h) 和 [src/vm/spt.c](/d:/Desktop/pintos/src/vm/spt.c) 已经正是这个方向：用 `struct hash`，key 是 page-aligned `upage`。

**推荐 SPT 结构**

你现在的 `struct spt_entry` 已经基本合理：

```c
struct spt_entry {
  void *upage;
  bool writable;

  enum vm_page_type type;
  enum vm_page_state state;

  struct file *file;
  off_t ofs;
  uint32_t read_bytes;
  uint32_t zero_bytes;

  size_t swap_slot;
  void *kpage;

  int mapid;
  struct hash_elem elem;
};
```

每个进程应该有自己的 SPT，所以需要在 `struct thread` 里加：

```c
#ifdef VM
  struct hash spt;
#endif
```

也就是改 [src/threads/thread.h](/d:/Desktop/pintos/src/threads/thread.h)。

**SPT 接口该在哪里用**

主要接入点如下。

1. `process.c / load()`

在创建用户进程页目录后初始化 SPT：

文件：[src/userprog/process.c](/d:/Desktop/pintos/src/userprog/process.c)

函数：`load`

位置大概在：

```c
t->pagedir = pagedir_create ();
if (t->pagedir == NULL)
  goto done;
process_activate ();
```

后面加：

```c
#ifdef VM
if (!spt_init (&t->spt))
  goto done;
#endif
```

需要 include：

```c
#ifdef VM
#include "vm/spt.h"
#endif
```

2. `process.c / load_segment()`

这里是最关键的地方。

现在你的 `load_segment()` 会立刻：

```c
process_alloc_user_page()
file_read()
install_page()
```

这是 eager loading。VM 项目应该改成 lazy loading：不立刻分配 frame、不立刻读文件，只给每个页创建一个 `spt_entry`，记录将来 page fault 时怎么加载。

也就是每轮循环里创建：

```c
spte->upage = upage;
spte->type = page_read_bytes > 0 ? VM_PAGE_FILE : VM_PAGE_ZERO;
spte->state = VM_PAGE_NOT_LOADED;
spte->file = file;
spte->ofs = ofs;
spte->read_bytes = page_read_bytes;
spte->zero_bytes = page_zero_bytes;
spte->writable = writable;
spte->kpage = NULL;
spte->mapid = -1;
spt_insert (&thread_current ()->spt, spte);
```

然后推进：

```c
read_bytes -= page_read_bytes;
zero_bytes -= page_zero_bytes;
upage += PGSIZE;
ofs += PGSIZE;
```

注意：如果你做 lazy load，`file` 需要保持打开，不能在 load 成功后关掉。你现在 `t->exec_file = file` 已经保持 executable 打开了，这是好的。

3. `process.c / setup_stack()`

有两种做法。

简单版：初始栈第一页仍然立刻分配并 `install_page()`，同时插入一个 `VM_PAGE_ZERO` 或 loaded 的 SPT entry。

更 VM 风格的版本：只把 `PHYS_BASE - PGSIZE` 注册成 zero page，等第一次访问栈时 page fault 再分配。但因为 `setup_stack()` 马上要往栈里写 argv，所以通常保留“初始栈页立即加载”更方便。

建议你在 `setup_stack()` 成功 install 后补一个 SPT entry：

```c
spte->upage = PHYS_BASE - PGSIZE;
spte->type = VM_PAGE_ZERO;
spte->state = VM_PAGE_LOADED;
spte->writable = true;
spte->kpage = kpage;
spte->mapid = -1;
spt_insert (&thread_current ()->spt, spte);
```

后续 stack growth 生成的新页也是 `VM_PAGE_ZERO`。

4. `exception.c / page_fault()`

文件：[src/userprog/exception.c](/d:/Desktop/pintos/src/userprog/exception.c)

函数：`page_fault`

这是 SPT 的核心使用点。

逻辑应该是：

```c
if (!not_present)
  kill; // 写只读页，权限错误

if (!is_user_vaddr (fault_addr))
  kill;

spte = spt_find (&thread_current ()->spt, fault_addr);

if (spte == NULL) {
  if (is_stack_growth_access (fault_addr, f->esp))
    create zero-page SPT entry;
  else
    kill;
}

load_page_from_spte (spte);
```

`load_page_from_spte()` 做：

```c
frame = frame_allocate (PAL_USER, spte->upage);
kpage = frame->kpage;

switch (spte->type) {
  case VM_PAGE_FILE:
  case VM_PAGE_MMAP:
    file_seek (spte->file, spte->ofs);
    file_read (spte->file, kpage, spte->read_bytes);
    memset (kpage + spte->read_bytes, 0, spte->zero_bytes);
    break;

  case VM_PAGE_ZERO:
    memset (kpage, 0, PGSIZE);
    break;

  case VM_PAGE_SWAP:
    swap_in (spte->swap_slot, kpage);
    break;
}

pagedir_set_page (cur->pagedir, spte->upage, kpage, spte->writable);
spte->kpage = kpage;
spte->state = VM_PAGE_LOADED;
```

5. `process.c / process_exit()`

进程退出时要销毁 SPT。

文件：[src/userprog/process.c](/d:/Desktop/pintos/src/userprog/process.c)

函数：`process_exit`

在销毁 pagedir 前调用：

```c
#ifdef VM
spt_destroy (&cur->spt);
#endif
```

但你现在的 `spt_destroy()` 只 `free(spte)`，还没释放 frame/swap/mmap write-back。最终版本应该在 `spt_destroy_entry()` 里：

- 如果 `spte->state == VM_PAGE_LOADED`，释放 frame；
- 如果 `spte->type == VM_PAGE_SWAP`，释放 swap slot；
- 如果 `spte->type == VM_PAGE_MMAP` 且 dirty，写回文件；
- 最后 `free(spte)`。

6. `frame.c / eviction`

文件：[src/vm/frame.c](/d:/Desktop/pintos/src/vm/frame.c)

函数：未来的 `frame_evict()` 或你自己实现的 eviction 函数。

evict 一个 frame 时，需要通过 frame entry 找到：

```c
owner thread
upage
```

然后：

```c
spte = spt_find (&owner->spt, upage);
```

根据页面类型决定写到 swap 还是写回 mmap/file，并更新：

```c
spte->state = VM_PAGE_NOT_LOADED;
spte->kpage = NULL;
spte->type = VM_PAGE_SWAP;
spte->swap_slot = slot;
pagedir_clear_page (owner->pagedir, upage);
```

所以 frame table 和 SPT 是配合关系：frame table 管“物理帧现在属于谁”，SPT 管“这个用户虚拟页的来源和状态”。

7. `syscall.c / 用户指针检查`

文件：[src/userprog/syscall.c](/d:/Desktop/pintos/src/userprog/syscall.c)

你现在可能有类似：

```c
pagedir_get_page (thread_current ()->pagedir, uaddr) == NULL
```

VM 后不能简单地用 `pagedir_get_page()` 判断非法，因为合法页面可能还没加载。应该改成：

```c
spt_find (&cur->spt, uaddr) != NULL
```

或者更稳妥：访问用户内存时允许 page fault 把页加载进来。对于 buffer 跨页的 syscall，比如 `read(fd, buffer, size)`、`write(fd, buffer, size)`，需要逐页验证：

```c
for each page in [buffer, buffer + size):
  spt_find(...) 或 pagedir_get_page(...) 或 stack growth 合法
```

8. `syscall.c / mmap 和 munmap`

VM 后如果实现 mmap：

- `mmap()`：为文件范围内每一页创建 `VM_PAGE_MMAP` 的 SPT entry；
- 检查目标地址页对齐；
- 检查地址范围不和已有 SPT entry 重叠；
- `munmap()`：遍历该 `mapid` 的所有 SPT entries；
- 如果页已加载且 dirty，写回文件；
- 清 pagedir，释放 frame；
- 从 SPT 删除 entry。

你现在的 `spt_entry.mapid` 字段已经是在为 mmap 准备。

**一句话总结设计**

你的 SPT 应该是：

> 每个进程一个 hash table，key 是用户虚拟页 `upage`，value 是这个页的来源、权限、加载状态、frame/swap/mmap 信息。`load_segment()` 负责登记页，`page_fault()` 负责按 SPT 把页真正加载进内存，`process_exit()/munmap()/eviction` 负责根据 SPT 清理或写回。

最重要的调用链是：

```text
load()
  -> spt_init()

load_segment()
  -> spt_insert(file-backed page entries)

setup_stack()
  -> spt_insert(initial stack page)

page_fault()
  -> spt_find()
  -> load page from file / zero / swap
  -> pagedir_set_page()

frame eviction
  -> spt_find()
  -> update spte to swapped/not-loaded

process_exit()
  -> spt_destroy()

mmap()/munmap()
  -> spt_insert(), spt_find(), spt_delete()
```

你现在已经有了一个不错的 `spt.h/spt.c` 骨架；下一步主要不是重新设计，而是把它接到 `thread.h`、`process.c`、`exception.c`、`frame.c`、`syscall.c` 这些路径里。

# Lazy-loading notice
因为 lazy loading 之后，页面的生命周期变了。

以前 eager loading 时，`load_segment()` 立刻：

```c
frame_allocate()
file_read()
pagedir_set_page()
```

如果中途失败，`load_segment()` 自己可以马上 `process_free_user_page(kpage)`。但 lazy loading 后，`load_segment()` 只登记 SPT：

```c
spte->state = VM_PAGE_NOT_LOADED;
spte->kpage = NULL;
```

真正的 frame 是之后在 `page_fault()` 里分配的：

```c
frame = frame_allocate(PAL_USER, spte->upage);
spte->kpage = frame->kpage;
spte->state = VM_PAGE_LOADED;
```

所以进程退出时，可能有很多页已经因为 page fault 被加载进内存了。这些 frame 不再由 `load_segment()` 管，而是挂在对应的 `spte` 上。

如果 `spt_destroy()` 只做：

```c
free(spte);
```

那就会丢掉 `spte->kpage` 这个指针，导致：

- frame table 里还留着这个 frame
- user pool 里的物理页没有归还
- `struct frame_entry` 没有释放
- 之后内存越来越少

所以在 `spt_destroy_entry()` 里需要：

```c
if (spte->state == VM_PAGE_LOADED && spte->kpage != NULL)
  frame_free(spte->kpage);
```

意思是：如果这个虚拟页现在真的占着一个物理 frame，进程退出时就把这个 frame 还回去。
