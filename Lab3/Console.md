下面是你目前为了消除输出紊乱做过的修改整理，可以直接拿去和老师沟通。

**已做修改**

1. **把子进程退出时唤醒父进程的时机延后**

   原因：`wait/exec` 场景里，子进程退出时如果过早 `sema_up()` 唤醒父进程，父子进程的输出可能交错。

   修改思路：

   - 子进程先完成自己的退出信息打印，例如：
     ```c
     child-simple: exit(81)
     ```
   - 然后再更新 `child_info->exit_status`
   - 最后再 `sema_up(&child_info->exit_sema)` 唤醒父进程

   目标是避免父进程 `(exec-once) end` 抢在子进程 exit 输出中间。

2. **`write(fd=1)` 改成一次性 `putbuf()`**

   原因：用户测试库 `tests/lib.c::msg()` 本来就是把整行格式化到一个 buffer，然后一次 `write(STDOUT_FILENO, buf, strlen(buf))`。  
   如果 kernel 端再逐字节输出，多个线程的 console 输出可能交错。

   修改思路：

   - `sys_write(fd == 1)` 时先把用户 buffer copy 到 kernel buffer。
   - 然后调用一次：
     ```c
     putbuf(kbuffer, size);
     ```
   - 不在循环里逐字节 `putchar()`。

   目标是利用 console lock 保证一整行输出原子化。

3. **`sys_read()` / `sys_write()` 期间 pin 用户 buffer**

   原因：VM 开启后，syscall 正在 `copy_in/copy_out` 或文件 I/O 时，用户 buffer 所在页可能被 eviction 路径换出或状态改变，导致读到错位/污染的数据。

   修改思路：

   - `check_user_buffer(buffer, size)` 后：
     ```c
     pin_user_buffer(buffer, size);
     ```
   - syscall 完成后：
     ```c
     unpin_user_buffer(buffer, size);
     ```
   - 覆盖所有 buffer 跨过的用户页。
   - 若页还未加载，先 fault-in，再 pin。

   目标是保证 syscall 使用用户 buffer 期间，对应 frame 不会被 eviction。

4. **`frame_pin()` / `frame_unpin()` 改为持锁查找并修改**

   原因：之前 `frame_lookup()` 内部加锁查找，但返回后锁已经释放，然后再修改 `frame->pinned`。  
   中间存在竞态窗口：frame 可能已经被 eviction/free/reuse。

   修改思路：

   - 在 `frame_pin()` / `frame_unpin()` 内部直接持有 `frame_lock`
   - 在同一个临界区里完成：
     ```c
     find frame -> modify pinned
     ```

   目标是消除 lookup 和修改 pin 状态之间的 race。

5. **随机 eviction placeholder 改成 clock / second-chance**

   原因：random eviction 可能频繁踢掉刚刚访问过的用户输出 buffer、用户栈页等。  
   虽然不是根因，但会放大输出紊乱概率。

   修改思路：

   - 增加 `clock_hand`
   - eviction 时跳过 pinned frame
   - 如果 accessed bit 为 1：
     ```c
     pagedir_set_accessed(..., false);
     ```
     给 second chance
   - 如果 accessed bit 为 0，选作 victim

   目标是近似 LRU，降低刚使用过的页被换出的概率。

6. **后来尝试扩大 pin 范围：pin syscall 参数页**

   观察：`write-zero` 中 `write(handle, &buf, 0)` 本身不 copy buffer，但后续输出仍出现：
   ```text
   (w(write-zero) end
   ```

   说明问题可能不只在 read/write data buffer，也可能在 syscall 参数读取期间。

   修改思路：

   - 在 `syscall_handler()` 读取 `esp`, `esp+1`, `esp+2`, `esp+3` 前 pin 用户栈参数页。
   - syscall 结束后 unpin。

   目标是防止 syscall 参数页在读取参数期间被 eviction 或状态变化。

7. **后来尝试 pin 用户字符串页**

   涉及 syscall：

   - `open`
   - `create`
   - `remove`
   - `exec`

   原因：这些 syscall 会从用户地址读取字符串。`copy_in_string()` 期间字符串所在页也可能被 eviction。

   修改思路：

   - `copy_in_string()` 遍历用户字符串时，遇到新页就 pin。
   - copy 完或出错时 unpin。

   目标是保证字符串 copy 期间源字符串页稳定。

8. **把 `pinned` bool 改成 `pin_count`**

   原因：同一页可能被多处嵌套 pin，例如：

   - syscall 参数页 pin
   - write buffer pin
   - 字符串页 pin
   - frame 初始化 pin

   如果只用 bool，某一路提前 unpin 会把页放开，导致另一路仍在使用时被 eviction。

   修改思路：

   ```c
   bool pinned;
   ```

   改成：

   ```c
   unsigned pin_count;
   ```

   eviction 判断：

   ```c
   frame->pin_count > 0
   ```

   目标是支持嵌套 pin/unpin。

**目前现象**

这些修改后，输出紊乱的频率降低了，但没有完全消失。  
典型残留现象包括：

```text
(lg-r(lg-random) close "bazzle"
(w(write-zero) end
```

这说明：

- `putbuf()` 原子输出已经做了；
- read/write buffer pin 也做了；
- clock replacement 降低了概率；
- 但仍可能存在更底层的内存破坏、frame/SPT 状态竞态、console/serial 路径问题，或者某些 syscall 退出路径没有正确 unpin/保护。