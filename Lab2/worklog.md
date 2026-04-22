## Order of Implementation! 
TA session PPT

### 0418
**termination message**
modified:
- process.c
- syscall.c
- thread.h(`exit_status`)

**argument parsing**
requirements:
- extend `process_execute()` ok
- multiple spaces = single  TODO: 检查一下strtok_r怎么实现的
- limit on length: 4KB  TODO

modified: 
- `process_execute()`
- `load()`

design:
- 把`palloc_free_page (file_name_copy)`放进done分支: 防止printf 里 prog_name 所在页面已经被释放


### 0420
- stack setup in argument parsing
    - process.c `setup_stack()`

**syscall** 
- syscall 框架
- syscall halt
- syscall exit
    - 把打印退出信息从process_exit挪到了sys_exit
    - sys_exit() -> thread_exit() -> process_exit()
- syscall write ( TODO: check )
```
System Call: int write (int fd, const void *buffer, unsigned size)

Writes size bytes from buffer to the open file fd. Returns the number of bytes actually written, which may be less than size if some bytes could not be written.

Writing past end-of-file would normally extend the file, but file growth is not implemented by the basic file system. The expected behavior is to write as many bytes as possible up to end-of-file and return the actual number written, or 0 if no bytes could be written at all.

Fd 1 writes to the console. Your code to write to the console should write all of buffer in one call to putbuf(), at least as long as size is not bigger than a few hundred bytes. (It is reasonable to break up larger buffers.) Otherwise, lines of text output by different processes may end up interleaved on the console, confusing both human readers and our grading scripts.

```
目前没有一个测例能跑通??

- changed process_wait() to a sleep

**BUG**
为什么 &status 是错的？
因为 status 是你在 syscall_handler 这个内核函数里定义的局部变量，它存储在内核栈上。is_user_vaddr(&status) 永远会返回 false，导致你的系统调用还没开始跑就把自己杀了。

- Fixed Bug above.
- Implemented process_wait, 改为轮询等待目标 tid 结束
```
while (tid_is_alive(child_tid)) 
    thread_yield();
```
modified: 
- process.c `process_wait`
- syscall.c `check_user_vaddr`, `syscall_handler`

codex神力, passed 28.
```pass tests/userprog/args-none
pass tests/userprog/args-single
pass tests/userprog/args-multiple
pass tests/userprog/args-many
pass tests/userprog/args-dbl-space
FAIL tests/userprog/sc-bad-sp
pass tests/userprog/sc-bad-arg
pass tests/userprog/sc-boundary
pass tests/userprog/sc-boundary-2
FAIL tests/userprog/sc-boundary-3
pass tests/userprog/halt
pass tests/userprog/exit
FAIL tests/userprog/create-normal
pass tests/userprog/create-empty
pass tests/userprog/create-null
pass tests/userprog/create-bad-ptr
FAIL tests/userprog/create-long
FAIL tests/userprog/create-exists
FAIL tests/userprog/create-bound
FAIL tests/userprog/open-normal
FAIL tests/userprog/open-missing
FAIL tests/userprog/open-boundary
FAIL tests/userprog/open-empty
pass tests/userprog/open-null
pass tests/userprog/open-bad-ptr
FAIL tests/userprog/open-twice
FAIL tests/userprog/close-normal
FAIL tests/userprog/close-twice
pass tests/userprog/close-stdin
pass tests/userprog/close-stdout
pass tests/userprog/close-bad-fd
FAIL tests/userprog/read-normal
pass tests/userprog/read-bad-ptr
FAIL tests/userprog/read-boundary
FAIL tests/userprog/read-zero
pass tests/userprog/read-stdout
pass tests/userprog/read-bad-fd
FAIL tests/userprog/write-normal
pass tests/userprog/write-bad-ptr
FAIL tests/userprog/write-boundary
FAIL tests/userprog/write-zero
pass tests/userprog/write-stdin
pass tests/userprog/write-bad-fd
FAIL tests/userprog/exec-once
FAIL tests/userprog/exec-arg
FAIL tests/userprog/exec-bound
pass tests/userprog/exec-bound-2
pass tests/userprog/exec-bound-3
FAIL tests/userprog/exec-multiple
FAIL tests/userprog/exec-missing
pass tests/userprog/exec-bad-ptr
FAIL tests/userprog/wait-simple
FAIL tests/userprog/wait-twice
FAIL tests/userprog/wait-killed
pass tests/userprog/wait-bad-pid
FAIL tests/userprog/multi-recurse
FAIL tests/userprog/multi-child-fd
FAIL tests/userprog/rox-simple
FAIL tests/userprog/rox-child
FAIL tests/userprog/rox-multichild
FAIL tests/userprog/bad-read
FAIL tests/userprog/bad-write
FAIL tests/userprog/bad-read2
FAIL tests/userprog/bad-write2
FAIL tests/userprog/bad-jump
FAIL tests/userprog/bad-jump2
FAIL tests/userprog/no-vm/multi-oom
FAIL tests/filesys/base/lg-create
FAIL tests/filesys/base/lg-full
FAIL tests/filesys/base/lg-random
FAIL tests/filesys/base/lg-seq-block
FAIL tests/filesys/base/lg-seq-random
FAIL tests/filesys/base/sm-create
FAIL tests/filesys/base/sm-full
FAIL tests/filesys/base/sm-random
FAIL tests/filesys/base/sm-seq-block
FAIL tests/filesys/base/sm-seq-random
FAIL tests/filesys/base/syn-read
FAIL tests/filesys/base/syn-remove
FAIL tests/filesys/base/syn-write
52 of 80 tests failed.
```


### 0421

actually 0422

- codex added accessing user memory

 passed 30 of 80

```markdown
pass tests/userprog/args-none
pass tests/userprog/args-single
pass tests/userprog/args-multiple
pass tests/userprog/args-many
pass tests/userprog/args-dbl-space
pass tests/userprog/sc-bad-sp
pass tests/userprog/sc-bad-arg
pass tests/userprog/sc-boundary
pass tests/userprog/sc-boundary-2
pass tests/userprog/sc-boundary-3
pass tests/userprog/halt
pass tests/userprog/exit
FAIL tests/userprog/create-normal
pass tests/userprog/create-empty
pass tests/userprog/create-null
pass tests/userprog/create-bad-ptr
FAIL tests/userprog/create-long
FAIL tests/userprog/create-exists
FAIL tests/userprog/create-bound
FAIL tests/userprog/open-normal
FAIL tests/userprog/open-missing
FAIL tests/userprog/open-boundary
FAIL tests/userprog/open-empty
pass tests/userprog/open-null
pass tests/userprog/open-bad-ptr
FAIL tests/userprog/open-twice
FAIL tests/userprog/close-normal
FAIL tests/userprog/close-twice
pass tests/userprog/close-stdin
pass tests/userprog/close-stdout
pass tests/userprog/close-bad-fd
FAIL tests/userprog/read-normal
pass tests/userprog/read-bad-ptr
FAIL tests/userprog/read-boundary
FAIL tests/userprog/read-zero
pass tests/userprog/read-stdout
pass tests/userprog/read-bad-fd
FAIL tests/userprog/write-normal
pass tests/userprog/write-bad-ptr
FAIL tests/userprog/write-boundary
FAIL tests/userprog/write-zero
pass tests/userprog/write-stdin
pass tests/userprog/write-bad-fd
FAIL tests/userprog/exec-once
FAIL tests/userprog/exec-arg
FAIL tests/userprog/exec-bound
pass tests/userprog/exec-bound-2
pass tests/userprog/exec-bound-3
FAIL tests/userprog/exec-multiple
FAIL tests/userprog/exec-missing
pass tests/userprog/exec-bad-ptr
FAIL tests/userprog/wait-simple
FAIL tests/userprog/wait-twice
FAIL tests/userprog/wait-killed
pass tests/userprog/wait-bad-pid
FAIL tests/userprog/multi-recurse
FAIL tests/userprog/multi-child-fd
FAIL tests/userprog/rox-simple
FAIL tests/userprog/rox-child
FAIL tests/userprog/rox-multichild
FAIL tests/userprog/bad-read
FAIL tests/userprog/bad-write
FAIL tests/userprog/bad-read2
FAIL tests/userprog/bad-write2
FAIL tests/userprog/bad-jump
FAIL tests/userprog/bad-jump2
FAIL tests/userprog/no-vm/multi-oom
FAIL tests/filesys/base/lg-create
FAIL tests/filesys/base/lg-full
FAIL tests/filesys/base/lg-random
FAIL tests/filesys/base/lg-seq-block
FAIL tests/filesys/base/lg-seq-random
FAIL tests/filesys/base/sm-create
FAIL tests/filesys/base/sm-full
FAIL tests/filesys/base/sm-random
FAIL tests/filesys/base/sm-seq-block
FAIL tests/filesys/base/sm-seq-random
FAIL tests/filesys/base/syn-read
FAIL tests/filesys/base/syn-remove
FAIL tests/filesys/base/syn-write
 ```


### 0422
checklist:
- 文件描述符表 + 基础文件系统调用 (create, open, read, write, close)：这能让你通过很多基础测试。记得加全局锁。(done)
- exec 的同步机制：确保父进程能拿到子进程的 pid。
- wait 的完整逻辑：这是最折磨人的部分，建议画一画父子进程同步的状态图。
- file_deny_write：最后加这个，比较简单。



codex神力:
- create/remove/open/filesize/read/write/seek/tell/close 的完整分发与实现
- 用户指针、用户缓冲区、用户字符串的校验与安全拷贝，非法地址会 exit(-1)
- 每进程独立 fd 表，fd 从 2 开始分配，退出时自动关闭全部打开文件
- 全局 filesys_lock，把文件系统访问串行化
- 运行中可执行文件的 deny_write / 退出时 allow_write，避免被改写
- 另外我顺手把异常杀进程路径改成统一走 sys_exit(-1) 了，在 exception.c 

modified:
**LAB 2.4**
- syscall.c (refactor)
- syscall.h
- process.c
    - `process_exit()`
    - `bool load()`
- thread.c (fd_list each process)
- thread.h 
- exception.c (exception: `kill()`)

reult:12 failed
```markdown
FAIL tests/userprog/read-normal
FAIL tests/userprog/read-boundary
FAIL tests/userprog/exec-missing
FAIL tests/userprog/wait-simple
FAIL tests/userprog/wait-twice
FAIL tests/userprog/multi-recurse
FAIL tests/userprog/multi-child-fd
FAIL tests/userprog/rox-child
FAIL tests/userprog/rox-multichild
FAIL tests/userprog/no-vm/multi-oom
FAIL tests/filesys/base/syn-read
FAIL tests/filesys/base/syn-write
```


#### TASK: Synchronization in syscall exec/wait

**Requirements**
exec: 
- "the parent process cannot return from the exec until it knows whether the child process successfully loaded its executable. You must use appropriate synchronization to ensure this."

wait:
- malloc()创建父子进程共享的数据结构, 保存child的execution information
    - pid, exit_status, sema阻塞父进程直到子进程退出, has_called_wait, is_exit, ref_count有没有进程还在用这个结构
- direct child
    - 父进程struct thread中添加child_list
    - 初始化必须在 thread_create 之后、process_execute 返回前完成，或者在子进程启动函数中处理好同步，防止子进程跑得太快在父进程还没登记完就退出了。
- Semaphore 父子进程的同步
    - down() - 子进程退出并写入exit_status - up()
-  whichever process that dies last isresponsible for freeing the information
    - 实现: ref_count
- Cases to return -1:
    - not direct child
    - wait过再wait
    - child被kernel杀死

codex神力
modified:
- process.c
    process_execute() 现在会先创建共享状态，再创建子线程，并在返回前通过 load_sema 等到子进程明确汇报“加载成功/失败”
    start_process() 会把这块共享状态挂到子线程上，load() 结束后立刻唤醒父进程，所以 exec 失败时现在会返回 -1
    process_wait() 现在只允许等待直接子进程一次；它会找到对应子项、阻塞到子进程退出、取回退出码，再做引用释放
    process_exit() 现在会把退出码写回共享状态、唤醒等待者；如果父进程先退出，也会把还没 wait 的孩子全部“脱钩”，让最后存活的一方负责释放这块状态.

- thread.c, thread,h
    给每个进程补了 child_processes 和 child_info，用于维护子进程列表和当前进程对应的共享状态。

result:
exec, wait 成功实现, 但是比较上一版出现了新fail.同时有遗留fail
9 of 80 failed
```markdown
FAIL tests/userprog/args-multiple ???
FAIL tests/userprog/create-null ???
FAIL tests/userprog/read-normal
FAIL tests/userprog/read-boundary
FAIL tests/userprog/multi-child-fd
FAIL tests/userprog/no-vm/multi-oom
FAIL tests/filesys/base/lg-random
FAIL tests/filesys/base/lg-seq-block
FAIL tests/filesys/base/sm-random
```

观察到好几个fail原因是输出格式轻微错乱
e.g.
```diff
Differences in `diff -u' format:
  (sm-random) begin
  (sm-random) create "bazzle"
  (sm-random) open "bazzle"
  (sm-random) write "bazzle" in random order
- (sm-random) read "bazzle" in random order
+ ((sm-random) read "bazzle" in random order
  (sm-random) close "bazzle"
  (sm-random) end
```
DEBUG方向:
- 补全 read 逻辑：lg-random 依赖 read 的正确性。如果你还没写文件 read，这个测试是不可能通过的。
- 检查 printf 的来源：在 Pintos 中，所有的 (test-name) ... 输出都是通过 lib/user/syscall.c 里的 printf 打印的，它最终会调用你的 write(fd 1, ...)。
- 打印调试：在你的 syscall_write 入口处打印 fd 和 size，看看在出问题的那一刻，是不是收到了异常的请求。