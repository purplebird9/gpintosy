### 3/25
**10:00~16:00**
- overview task1
- read list.h
- task1 1st implement

### 3/29
**13:30~14:50**
- read task2.1
- install rg
- formed detailed implementation steps 
- implement sorted ready list

**19:20~20:30**
- implement yield logic in thread_create & thread_set_priority
- TODO: synch.c; "To yield the CPU in the interrupt context, you can take a look at functions in threads/interrupt.c".
- try task 3?
- first provide float arithmatic support
- Then: add *full support* for priority scheduling(all scenarios, primitives)

### 4/1
**10:00-11:30**
- start working on synch.c
- new func test_yield, used in thread_create & thread_set_priority & sema_up & cond_wait & timer_interrupt

```
FAIL tests/threads/alarm-single
pass tests/threads/alarm-multiple
pass tests/threads/alarm-simultaneous
pass tests/threads/alarm-priority
pass tests/threads/alarm-zero
pass tests/threads/alarm-negative
pass tests/threads/priority-change
FAIL tests/threads/priority-donate-one
FAIL tests/threads/priority-donate-multiple
FAIL tests/threads/priority-donate-multiple2
FAIL tests/threads/priority-donate-nest
FAIL tests/threads/priority-donate-sema
FAIL tests/threads/priority-donate-lower
pass tests/threads/priority-fifo
pass tests/threads/priority-preempt
pass tests/threads/priority-sema
FAIL tests/threads/priority-condvar
FAIL tests/threads/priority-donate-chain
FAIL tests/threads/mlfqs-load-1
FAIL tests/threads/mlfqs-load-60
FAIL tests/threads/mlfqs-load-avg
FAIL tests/threads/mlfqs-recent-1
pass tests/threads/mlfqs-fair-2
pass tests/threads/mlfqs-fair-20
FAIL tests/threads/mlfqs-nice-2
FAIL tests/threads/mlfqs-nice-10
FAIL tests/threads/mlfqs-block
16 of 27 tests failed.
```

- alarm-single??
- TODO: task1, optimization
- I think I can begin 2.2 priority donation

**14:00-20:00**
- all scenarios? consider lock first
- donation: 
    - if (lock_try_acquire fail): check lock-holder's priority; **donate_priority()**
- donate_priority(donee):
    - nested: donate_priority()需要递归. check donee's lock
    - when lock released, recall_donation(doner){recover donee's old priority}
    - (donor's priority unchanged.)

- struct thread
- 递归函数donate, 递归回溯recall
    - 本来想在donate函数中加入donors_list, 但是有重复添加的问题, 所以把这一步放进lock_acquire的实现中.
    - 总之不要在递归/循环捐赠函数里反复调用 list_insert。插入动作交给 lock_acquire（被阻塞前入队），递归函数只负责顺着 waiting_on_lock -> holder 这条线把 priority 数值改掉。
- 写进lock_acquire
- 写进lock_release, 只移除这个lock对应的donor

```
pass tests/threads/alarm-single
pass tests/threads/alarm-multiple
pass tests/threads/alarm-simultaneous
pass tests/threads/alarm-priority
pass tests/threads/alarm-zero
pass tests/threads/alarm-negative
pass tests/threads/priority-change
pass tests/threads/priority-donate-one
pass tests/threads/priority-donate-multiple
pass tests/threads/priority-donate-multiple2
pass tests/threads/priority-donate-nest
//
FAIL tests/threads/priority-donate-sema
FAIL tests/threads/priority-donate-lower
FAIL tests/threads/priority-fifo
pass tests/threads/priority-preempt
FAIL tests/threads/priority-sema
FAIL tests/threads/priority-condvar
FAIL tests/threads/priority-donate-chain
//
FAIL tests/threads/mlfqs-load-1
FAIL tests/threads/mlfqs-load-60
FAIL tests/threads/mlfqs-load-avg
FAIL tests/threads/mlfqs-recent-1
pass tests/threads/mlfqs-fair-2
pass tests/threads/mlfqs-fair-20
FAIL tests/threads/mlfqs-nice-2
FAIL tests/threads/mlfqs-nice-10
FAIL tests/threads/mlfqs-block
```

- test1 不知道为啥过了
- 在lock函数里关中断避免了race, 但是结果没变
- compare逻辑的问题, 需要写两个函数, 区分donor_elem和elem!
- 结果没变
- synch.c: blocked的线程priority也会变化, 所以唤醒前必须重新排序!
- 加了一堆list_sort
- 以及lock_acquire中,哪怕你现在优先级低，你也等在这个锁上，以后你的优先级一旦被别人提升，锁的持有者就该跟着提升。所以只要等在锁上，就必须无条件加入 donors_list。

```
pass tests/threads/alarm-single
pass tests/threads/alarm-multiple
pass tests/threads/alarm-simultaneous
pass tests/threads/alarm-priority
pass tests/threads/alarm-zero
pass tests/threads/alarm-negative
FAIL tests/threads/priority-change
pass tests/threads/priority-donate-one
pass tests/threads/priority-donate-multiple
pass tests/threads/priority-donate-multiple2
pass tests/threads/priority-donate-nest
pass tests/threads/priority-donate-sema
pass tests/threads/priority-donate-lower
FAIL tests/threads/priority-fifo
pass tests/threads/priority-preempt
pass tests/threads/priority-sema
pass tests/threads/priority-condvar
pass tests/threads/priority-donate-chain
```
- 1.2.2问题: fifo timeout(page_fault), "doubled test names"


**20:00-20:30**
- start 1.3
- struct thread增加nice,recent_cpu,load_avg并初始化
- TODO: list_sort不稳定排序,会破坏fifo! 要全替换成手写的稳定排序?






