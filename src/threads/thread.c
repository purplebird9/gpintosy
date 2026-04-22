#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#include "filesys/file.h"
#endif

/* 1.3 */
#include "threads/arithmetic.h"
int load_avg; //系统平均负载, 17.14.1: fixed-point, Q=14



/** Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/** List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/** List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/** Idle thread. */
static struct thread *idle_thread;

/** Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/** Lock used by allocate_tid(). */
static struct lock tid_lock;

/** Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /**< Return address. */
    thread_func *function;      /**< Function to call. */
    void *aux;                  /**< Auxiliary data for function. */
  };

/** Statistics. */
static long long idle_ticks;    /**< # of timer ticks spent idle. */
static long long kernel_ticks;  /**< # of timer ticks in kernel threads. */
static long long user_ticks;    /**< # of timer ticks in user programs. */

/** Scheduling. */
#define TIME_SLICE 4            /**< # of timer ticks to give each thread. */
static unsigned thread_ticks;   /**< # of timer ticks since last yield. */

/** If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);


/** ---LAB1 NEW--- */
/** 1.2.1: list_insert_ordered */
/* 辅助函数:比较优先级 */
bool priority_compare(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  struct thread *ta = list_entry(a, struct thread, elem);
  struct thread *tb = list_entry(b, struct thread, elem);
  return ta->priority > tb->priority; //降序
}
// 1.2.2: 比较donor优先级的函数, 因为elem和donor_elem不能混用
bool donor_priority_compare (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) { 
    return list_entry(a, struct thread, donors_elem)->priority > list_entry(b, struct thread, donors_elem)->priority; 
}

/** 1.2.1: 
 * 辅助函数: 通用检查, 如果cur优先级小于刚刚添加的ready_list首位优,则cur立刻yield
 * When a thread is added to the ready list that has a higher priority than the currently running thread, 
 * the current thread should immediately yield the processor to the new thread. 
 * */
void thread_test_yield(void)
{
  if (!list_empty(&ready_list))
  {
    struct thread *cur = thread_current();
    struct thread *front = list_entry(list_front(&ready_list), struct thread, elem);
    if (cur->priority < front->priority){
      //not in intr
      if (!intr_context())
        thread_yield();
      else
        /* 如果在中断中，标记下次返回时切换 */
        intr_yield_on_return ();
    }
  }
}

/** 1.2.2:
 * priority donation: recursive, for nested donation
 */
void thread_nested_donation(void){
  struct thread *cur = thread_current();
  int depth = 0; //最大递归深度
  while (cur->waiting_on_lock != NULL && depth < 8) 
  {
    struct lock *lock = cur->waiting_on_lock;
    struct thread *holder = lock->holder;
    if (holder == NULL || holder->priority >= cur->priority)//end chain
      break;
    holder->priority = cur->priority; //donate
    //list_insert_ordered(&holder->donors_list, &cur->donors_elem,priority_compare,NULL); //add cur to donee's donors list
    cur = holder; //recursive
    depth++;
  }
}

/** 1.2.2:
 * recall donation: 
 * when releasing a lock, remove all donations related to this lock; update priority
 */
void thread_recall_donation(struct lock *lock){
  struct thread *cur = thread_current();
  struct list_elem *e = list_begin(&cur->donors_list);
  while (e != list_end(&cur->donors_list)) 
  {
    struct thread *t = list_entry(e, struct thread, donors_elem);
    if (t->waiting_on_lock == lock) //donation is related to this lock
    {
      e = list_remove(e); //remove from donors list
    } 
    else 
    {
      e = list_next(e);
    }
  }
  //update priority
  cur->priority = cur->base_priority;
  if (!list_empty(&cur->donors_list)) 
  {
    //强制保证list是有序的,防止变动
    list_sort(&cur->donors_list, donor_priority_compare, NULL);

    struct thread *max_donor = list_entry(list_front(&cur->donors_list), struct thread, donors_elem);
    if (max_donor->priority > cur->priority)
      cur->priority = max_donor->priority;
  }
}

/* 1.3 计算优先级, recent_cpu, load_avg的函数*/
/* 用arithmetic.h的宏 */
static void
mlfqs_calculate_priority (struct thread *t)
{
  if (t == idle_thread) 
    return ;
  t->priority = FP_TO_INT_ZERO (ADD_FP_INT(DIV_FP_INT (t->recent_cpu, -4), PRI_MAX - t->nice * 2));
}

static void
mlfqs_calculate_recent_cpu (struct thread *t)
{
  if (t == idle_thread)
    return ;
  t->recent_cpu = ADD_FP_INT (MUL_FP (DIV_FP (MUL_FP_INT (load_avg, 2), ADD_FP_INT (MUL_FP_INT (load_avg, 2), 1)), t->recent_cpu), t->nice);
}

void 
mlfqs_calculate_load_avg (void) 
{
  int ready_threads;
  
  if (thread_current () == idle_thread)
    ready_threads = list_size (&ready_list);
  else
    ready_threads = list_size (&ready_list) + 1;

  load_avg = ADD_FP (MUL_FP (DIV_FP (INT_TO_FP (59), INT_TO_FP (60)), load_avg), 
                     MUL_FP_INT (DIV_FP (INT_TO_FP (1), INT_TO_FP (60)), ready_threads));
}

/*
每个值发生更新时要执行的函数。
每隔 1 个时钟周期，运行线程的 recent_cpu 值加 1。
每隔 4 个tick重新计算所有线程的优先级
每秒重新计算所有线程的 recent_cpu 和 load_avg 值
*/
void
mlfqs_increment_recent_cpu (void)
{
  if (thread_current () != idle_thread)
    thread_current ()->recent_cpu = ADD_FP_INT (thread_current ()->recent_cpu, 1);
}
void
mlfqs_recalculate_recent_cpu (void)
{
  struct list_elem *e;

  for (e = list_begin (&all_list); e != list_end (&all_list); e = list_next (e)) {
    struct thread *t = list_entry (e, struct thread, allelem);
    mlfqs_calculate_recent_cpu (t);
  }
}
void
mlfqs_recalculate_priority (void)
{
  struct list_elem *e;

  for (e = list_begin (&all_list); e != list_end (&all_list); e = list_next (e)) {
    struct thread *t = list_entry (e, struct thread, allelem);
    mlfqs_calculate_priority (t);
  }
}




/** ---Existing--- */

/** Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/** Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* 1.3 */
  load_avg = 0;
  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/** Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/** Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/** Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;
  
  /* Add to run queue. */
  thread_unblock (t);


//old
//  //If this thread has higher priority than current, current yield()
//  if (priority > thread_get_priority())
//    thread_yield(); 
  
  /* 1.2.1 检查优先级*/
    thread_test_yield ();

 
  return tid;
}

/** Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/** Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */




void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  //在 C 语言中，函数名在作为参数传递时会自动转换为指向该函数的指针。
  // 1.2.1: list_insert_ordered 
  // 不能自己改成RUNNING来切换到运行态!
  // 把检查,让位的逻辑放在调用thread_unblock的地方,比如thread_create和thread_set_priority
  list_insert_ordered (&ready_list, &t->elem, priority_compare, NULL); //降序
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/** Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/** Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/** Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/** Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/** Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level; 
  
  ASSERT (!intr_context ()); //not during an external interruption

  old_level = intr_disable (); //disable intr, 返回值是old_level
  if (cur != idle_thread) 
    // 1.2.1: list_insert_ordered
    list_insert_ordered (&ready_list, &cur->elem, priority_compare, NULL); 
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level); 
}

/** Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/** Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  /* 1.3: mlfqs禁用*/
  if (thread_mlfqs) {
    return ;
  }


  struct thread *cur = thread_current ();
  int old_priority = cur->priority;
  
  /* 1.2.2 */
  // 1. 更新 base_priority
  cur->base_priority = new_priority;
  cur->priority = new_priority;

  // 2. 检查捐赠列表，如果有更高的捐赠者，恢复捐赠的优先级
  if (!list_empty(&cur->donors_list))
  {
    // 确保列表有序，取出最高优先级的捐赠者
    list_sort(&cur->donors_list, donor_priority_compare, NULL);
    struct thread *max_donor = list_entry(list_front(&cur->donors_list), struct thread, donors_elem);
    if (max_donor->priority > cur->priority)
    {
      cur->priority = max_donor->priority;
    }
  }// 1.2.2 这里括号作用域打错了, 改一下全都pass了哈哈哈哈哈哈


  /* 1.2.1: If the thread lowers its priority such that not highest, test yield*/
  if (cur->priority < old_priority)
    thread_test_yield();
  
}

/** Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}


/** 1.3: Implementation */
/** Sets the current thread's nice value to NICE. */
/* 全部关中断 */
void
thread_set_nice (int nice UNUSED) 
{  
  enum intr_level old_level = intr_disable ();
  thread_current ()->nice = nice;
  mlfqs_calculate_priority (thread_current ());
  thread_test_yield ();
  intr_set_level (old_level);
}

/** Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  enum intr_level old_level = intr_disable ();
  int nice = thread_current ()-> nice;
  intr_set_level (old_level);
  return nice;
}

/** Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  enum intr_level old_level = intr_disable ();
  int load_avg_value = FP_TO_INT_ROUND (MUL_FP_INT (load_avg, 100));
  intr_set_level (old_level);
  return load_avg_value;
}

/** Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  enum intr_level old_level = intr_disable ();
  int recent_cpu= FP_TO_INT_ROUND (MUL_FP_INT (thread_current ()->recent_cpu, 100));
  intr_set_level (old_level);
  return recent_cpu;
}

/** ----------- */

/** Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/** Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /**< The scheduler runs with interrupts off. */
  function (aux);       /**< Execute the thread function. */
  thread_exit ();       /**< If function() returns, kill the thread. */
}

/** Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/** Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/** Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;

  /* 1.2.2: init new properties*/
  t->base_priority = priority;
  list_init(&t->donors_list);
  t->waiting_on_lock = NULL;

  /* 1.3: init new properties*/
  t->nice = 0;
  t->recent_cpu = 0;

// LAB 2.4
#ifdef USERPROG
  t->pagedir = NULL;
  list_init (&t->fd_list);
  t->next_fd = 2;
  t->exec_file = NULL;
  t->exit_status = -1;
#endif

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/** Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/** Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/** Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/** Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/** Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/** Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);


