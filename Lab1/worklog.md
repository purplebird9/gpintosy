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

**14:00-16:30**
- all scenarios? consider lock first
- donation: 
    - if (lock_try_acquire fail): check lock-holder's priority; **donate_priority()**
- donate_priority(donee):
    - nested: donate_priority()需要递归. check donee's lock
    - when lock released, recall_donation(doner){recover donee's old priority}
    - (donor's priority unchanged.)

- struct thread
- 递归函数donate, 递归回溯recall
- 写进lock_acquire
- 写进lock_release, 只移除这个lock对应的donor


