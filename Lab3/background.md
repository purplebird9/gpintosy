# Pintos Lab 3 Outline

## 1. Environment & Source Files
*   **工作目录**：`vm/`。
*   **编译宏**：开启 `-DVM` 选项。
*   **新增关键文件**：
    *   `devices/block.h` & `devices/block.c`：用于扇区级别的块设备访问（主要用于交换分区 Swap Partition）。
*   **已有代码重用**：在 `userprog/` 和 `threads/` 基础上进行扩展。

## 2. Terminology
### 2.1 页面 (Pages - 虚拟内存)
*   **定义**：4,096 字节（4KB）的连续虚拟内存区域。
*   **对齐**：起始地址能被 4096 整除。
*   **地址结构(32 bit)**：
    * 32 位地址 = 20 位Page Number + 12 位Offset。
    * 2^12 = 4KB = 4096, 2^20=1MB, 即4GB的虚拟地址空间被划分成1M个4KB页, offset覆盖一个页内的4096个字节
*   **区分**：
    *   user page：`PHYS_BASE` (0xc0000000) 以下。
        * 用户空间（User Space）：虚拟地址在 0x00000000 到 0xBFFFFFFF（0～3 GB − 1）之间。
        * 内核空间（Kernel Space）：虚拟地址在 0xC0000000 到 0xFFFFFFFF（3 GB～4 GB − 1）之间。
        * 也就是说，整个 4 GB 虚拟地址空间被分为 用户占 3 GB，内核占 1 GB。
    *   kernel page：全局共享。内核可访问所有页，用户只能访问自己的页。
* **Useful Functions**: APPENDIX -> Code Guide -> Virtual Address


### 2.2 帧 (Frames - 物理内存)
*   **定义**：4,096 字节, continuous region of physical memory
*   **地址结构**：20 位Frame Number + 12 位Offset。
*   **映射机制**：Pintos 把**kernel**虚拟地址直接映射到物理地址来访问帧。

### 2.3 页表 (Page Tables)
*   **作用**：CPU 用的数据结构，translate from page -> frame.
```ascii
                          +----------+
         .--------------->|Page Table|---------.
        /                 +----------+          |
   31   |   12 11    0                    31    V   12 11    0
  +-----------+-------+                  +------------+-------+
  |  Page Nr  |  Ofs  |                  |  Frame Nr  |  Ofs  |
  +-----------+-------+                  +------------+-------+
   Virt Addr      |                       Phys Addr       ^
                   \_____________________________________/
```
*   **code**：`pagedir.c`。

### 2.4 交换槽 (Swap Slots)
*   **定义**：a continuous, page-size region of disk space in the swap partition. 建议页对齐。

## 3. Resource Management Overview
我需要设计以下三种核心数据结构:

### 3.1 Supplemental Page Table, SPT

supplements the page table with additional data about each page.

**作用**：
- **page fault**：kernel查SPT得知virtual page需要的数据在哪里.
    
```markdown
        CPU: 访问虚拟地址VA, 但是页表项说它不在物理内存里.page fault,陷入内核.
        kernel: 用这个缺页的VA,查SPT:
            索引-VA
            内容-数据来源+元数据(prog.exe,偏移量0x100)
```

- **资源释放**：进程终止时，kernel问SPT释放哪些资源。

**Design**：
*   范围：通常为**每个进程一个**（Local）。
*   organization：段（Segments）/页（Pages）。
*   实现建议：哈希表（Hash Table）。


**Page Fault Handler:**
lab2: page fault=bug; lab3:page fault=needs bring in page from a file / swap

- Locate the page's data by SPT (in file sys/swap slot/0-page)
    - 全零页:写时复制的思想
    - invalid access: terminate process & free all resource
- Obtain a frame
- data -> frame
- faulted VA's PTE -> frame

\* sharing


### 3.2 帧表 (Frame Table) ???不懂

by deepseek:
```text
┌─────────────────────────────────────────────────────────────────────────────┐
│                           虚拟内存管理的三角架构                              │
└─────────────────────────────────────────────────────────────────────────────┘

  用户进程的视角                  内核的管理视角                   物理硬件视角
 (Virtual Address)             (Data Structures)              (Physical Memory)
┌───────────┐              ┌─────────────────────┐           ┌───────────────┐
│ 虚拟页 #A │──────────────►  补充页表 (SPT)      │           │               │
│ (缺页中断) │              │  页 #A 应包含：     │           │               │
└───────────┘              │   文件 prog.exe     │           │  Kernel Pool  │
                           │   偏移 0x1000       │           │  (内核专用)    │
                           └──────────┬──────────┘           │               │
                                      │分配物理页             │               │
                                      ▼                      │               │
┌───────────┐              ┌─────────────────────┐           ├───────────────┤
│ 虚拟页 #B │◄───────────── │    帧表 (FT)        │◄─────────►│  User Pool    │
│ (已映射)  │    (指向)     │                     │ (占用/空闲)│  (用户页面)    │
└─────┬─────┘              │ 帧 #0: 空闲          │           │               │
      │                    │ 帧 #1: 占用 ← 页 #B  │           │  ┌─帧 #1────┐ │
      │                    │ 帧 #2: 占用 ← 页 #A  │           │  │ 数据...  │ │
      │                    └─────────────────────┘           │  ├──────────┤ │
      │                                                      │  │ 帧 #4────│ │
      │                                                      │  │(空闲)    │ │
      │              ┌─────────────────────┐                 │  └──────────┘ │
      └──────────────►   硬件页表 (PT)      │                 └───────────────┘
                     │                     │
                     │ 页 #A: 不存在        │
                     │ 页 #B: 存在 → 帧 #1  │
                     └─────────────────────┘
```

*   记录每个物理帧的使用情况(是否被占用、被哪个进程的哪个页占用)
    entry - a ptr to the page 
*   **核心功能**：支持Eviction Policy (when no frame is free)
*   **内存分配**：必须使用 `palloc_get_page(PAL_USER)` 从用户池分配, avoid allocating from the "kernel pool"!

**Key Operation: Obtain an unused frame**
- has free frame:^~^
- no free frame:
    - evict
    - [x]evict && [x]swap full: KERNEL PANIC

**Eviction**
- choose a victim: *page replacement alg.*
- remove refs
- write to fs/swap

**Accessed & Dirty Bit** for alg.
- r/w : accessed bit=1
- w: dirty bit=1
- CPU never reset to 0. Kernel may.
- **别名alias**: 2 pages -> 1 frame, 只更新一个page的bit

别名导致的状态不同步问题:
```
场景： 假设物理内存地址 0x123。
- 用户进程通过虚拟地址 U_vaddr 访问它。
- 内核通过虚拟地址 K_vaddr 访问它。
问题： 如果内核通过 K_vaddr 修改了内存（即发生了写入），CPU 的硬件逻辑只会将 K_vaddr 对应页表项的 Dirty bit 置为 1。用户态那一侧的页表项（U_vaddr 的 PTE）依然显示为 Dirty=0。
```
Pintos解决方案:
- a. 手动同步
- b. 只用一个统一的访问入口


### 3.3 交换表 (Swap Table)

tracks in-use and free swap slots.
- pick an unused swap slot: frame --evicted page-->swap partition
- free a swap slot: page read back/*process* of the swapped page terminates

use `BLOCK_SWAP`  block device
- vm/build directory, `pintos-mkdisk swap.dsk --swap-size=n` to create an disk named swap.dsk that contains a n-MB swap partition.

Lazy.(not *reserved* for any page)


## Notes
![alt text](image.png)

## Key Mechanisms

### 4.1 缺页处理 (Page Fault Handler)
当触发缺页异常时（修改 `userprog/exception.c` 中的 `page_fault()`）：
1.  **定位**：在 SPT 中查找触发故障的虚拟页。
2.  **验证**：检查访问是否合法（地址有效性、是否越界进入内核、是否试图写入只读页）。若非法，终止进程。
3.  **获取帧**：从帧表获取一个物理帧（若无空闲则执行置换）。
4.  **读取数据**：将数据从文件系统或交换分区载入帧，或清零（Zero-page）。
5.  **更新页表**：修改硬件页表（PTE），指向物理帧。

### 4.2 页面置换与驱逐 (Eviction)
当物理内存不足时：
1.  **选择牺牲者**：使用页面置换算法（利用 Accessed/Dirty 位）。
2.  **移除引用**：从所有引用该帧的硬件页表中删除该映射。
3.  **写回磁盘**：如果该页是“脏”的（Dirty），则必须将其写入交换分区或原始文件。
4.  **重用**：将该帧分配给新页。

### 4.3 交换管理 (Swap Management)
*   **设备获取**：调用 `block_get_role(BLOCK_SWAP)`。
*   **延迟分配 (Lazy Allocation)**：只有在发生置换且确实需要写入磁盘时才分配交换槽。
*   **释放**：当数据被读回内存或进程结束时，立即释放交换槽。

## 5. 硬件辅助与别名问题 (Accessed and Dirty Bits)
*   **硬件更新**：CPU 自动设置 `Accessed`（访问过）和 `Dirty`（写入过）位。
*   **软件重置**：操作系统负责定期将这些位清零（如在置换算法中）。
*   **别名 (Aliases)**：
    *   同一个物理帧可能有多个虚拟地址指向它（如用户虚拟地址和内核虚拟地址）。
    *   **处理方法**：内核访问用户数据时，通过用户虚拟地址访问，或者同时检查/更新所有相关页表项。

## 6. 设计建议与注意事项
*   **数据结构选择**：
    *   **哈希表** (lib/kernel/hash.h)：高效，适合 SPT。
    *   **位图** (lib/kernel/bitmap.h)：适合管理固定大小的资源（如交换槽）。
    *   **数组/链表**：简单，但可能存在性能问题。
*   **非分页内存**：建议将这些管理数据结构存储在内核非分页内存中，确保指针始终有效。
*   **Panic 情况**：如果交换分区也满了且无法驱逐页面，系统 Panic。

---
**提示**：在编写设计文档时，请详细说明你将如何同步这些数据结构（加锁策略），以及你选择的具体置换算法（如 Clock 算法）。