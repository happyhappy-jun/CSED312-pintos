# Project 1: Threads

## Preliminaries

Team Number: 20

윤병준(20190766)

김치헌(20190806)

# Alarm Clock

기존에 "busy waiting" 방식으로 구현된 `devices/timer.c`의 `timer_sleep()`을 새로 구현하는 것이 목적입니다.

## Solution

우리는 이 문제를 `sleep_list`라는 리스트와 원소 구조체 `sleep_list_elem`를 새롭게 정의한 후, 이 리스트를 일어나야하는 틱에 대해 정렬된 상태로 유지시키는 방식으로 해결하였습니다.

`sleep_list_elem`은 다음과 같이 정의했습니다.

```c
struct sleep_list_elem {
  struct list_elem elem;          /* List element */
  int64_t end_tick;               /* Tick to wake up */
  struct semaphore semaphore;     /* Semaphore to block a sleeping thread */
};
```

쓰레드가 `timer_sleep()`을 호출하면 새로 작성한 `thread_sleep()` 함수가 호출됩니다.

```c
// devices/timer.c
void timer_sleep(int64_t ticks) {
int64_t start = timer_ticks();

ASSERT(intr_get_level() == INTR_ON);
thread_sleep(start + ticks);
}
```

```c
// threads/thread.c
void thread_sleep(int64_t end_tick) {
/* Define `sleep_list_elem` variable and initialize it */
struct sleep_list_elem sleep_list_elem;
sleep_list_elem.end_tick = end_tick;
sema_init(&sleep_list_elem.semaphore, 0);

/* Insert `sleep_list_elem` into the `sleep_list` */
list_insert_ordered(&sleep_list, &sleep_list_elem.elem,
compare_thread_wakeup_tick, NULL);

/* Semaphore down. (i.e., Start sleeping) */
sema_down(&sleep_list_elem.semaphore);
}
```

`thread_sleep()`은 `sleep_elem`을 초기화하고 `sleep_list`에 `end_tick`에 대한 오름차순으로 삽입합니다.
이후, `sema_down()`을 호출하여 `sema_up()`이 호출될 때 까지 쓰레드를 재웁니다.

자고있는 쓰레드를 깨우기 위해서 새로운 함수 `thread_wakeup()`을 작성하였고 매 틱마다 호출되도록 하였습니다.

```c
// devices/timer.c
static void
timer_interrupt(struct intr_frame *args UNUSED) {
ticks++;
thread_tick();
thread_wakeup(ticks);
}
```

```c
// threads/thread.c
void thread_wakeup(int64_t current_tick) {
/* Define a placeholder for iterating */
struct sleep_list_elem *elem;

/* While loop until the `sleep_list` empty */
while (!list_empty(&sleep_list)) {
/* Get the front element */
elem = list_entry(list_front(&sleep_list), struct sleep_list_elem, elem);
/* Break the while loop if the element's `end_tick` is greater than
 * `current_tick` */
if (elem->end_tick > current_tick) {
break;
}

/* Else, pop front from the `sleep_list` and call sema_up for its
 * `semaphore` */
list_pop_front(&sleep_list);
sema_up(&elem->semaphore);
}
}
```

`thread_wakeup()`은 현재 틱보다 `end_tick`이 큰 원소가 나올 때까지 `sleep_list`를 순회하면서 잠든 쓰레드들을 깨웁니다.
`end_tick`이 현재 틱 보다 큰 원소를 기준으로 앞 쪽 원소에 담긴 쓰레드는 깨워야하고, 뒤 쪽은 아직 일어날 시간이 되지 않았음을 의미합니다.
이는 `sleep_list`가 `end_tick`에 대해 오름차순으로 정렬되어 있기 때문입니다.

## Discussion

잠들어 있던 쓰레드들이 동시에 일어나야 할 때, 우선 순위와는 상관없이 sleep_list에 들어온 순서대로 `sema_up()`이 호출되면서 `ready_list`로 들어갑니다.
이로 인해 다음 실행될 쓰레드를 선택해야하는 경우, 우선 순위가 고려되지 않을 수 있습니다.
하지만 이 부분은 Priority Scheduler 파트에서 ready_list에 쓰레드가 삽입되는 모든 경우에 대해 우선 순위를 고려한 삽입을 하도록 구현하면 해결되는 문제입니다.
따라서, timer_sleep()의 관점에서는 그대로 두어도 괜찮겠다는 결론을 내렸습니다.

# Priority Scheduler

## Solution

### Data Structure

디자인 레포트에서 언급한 아래의 필드들을 `struct thread` 에 추가해 수정없이 구현했다.
`original_priority`는 스레드의 원래 우선순위를 저장하고, `waiting_lock`은 스레드가 기다리고 있는 락을 저장한다.
`donations`은 스레드가 받은 우선순위를 저장하는 리스트이고, `donation_elem`은 `donations` 리스트에 삽입될 때
사용되는 리스트 엘리먼트이다. 추가적으로 `init_thread` 함수를 콜하는 곳에어 각 필드를 초기화하는 코드를 추가했다.

```c
int original_priority;          /* Original priority of the thread */
struct lock *waiting_lock;      /* Lock that the thread is waiting for */
struct list donations;          /* List of donations to handle multiple donations */
struct list_elem donation_elem; /* List element for donation list */

// init_thread
//    t->original_priority = priority;
//    t->waiting_lock = NULL;
//    list_init(&t->donations);
```

### priority scheduling

우선도 기반 스케쥴링을 위해 다음과 같이 우선도 정렬로 `ready_list`를 관리하는 로직을 추가했다.

```c
bool compare_thread_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
  struct thread *thread_a = list_entry(a, struct thread, elem);
  struct thread *thread_b = list_entry(b, struct thread, elem);

  const int priority_a = thread_a->priority;
  const int priority_b = thread_b->priority;

  return priority_a > priority_b;
}
```

이후 `ready_list` 에 무언가를 삽입할때는 다음 처럼 스레드를 삽입하고 뺄때는 front 를 pop하는 식으로 구현했다.

```c
    list_insert_ordered(&ready_list, &cur->elem, compare_thread_priority, NULL);
```

### lock_acquire

`lock_acquire()`는 공유 자원 접근을 위해 락을 획득할때 스레드가 부르는 함수다.
락을 획득하기 전에 스레드가 기다리고 있는 락을 `waiting_lock`에 저장하고, `donations` 리스트에 스레드의 우선순위를
삽입한다. 그리고 `donate_priority()`를 호출해 현재 스레드가 기다리고 있는 락 (by referencing `waiting_lock`) 을
보유하고 있는 홀더에서 priority donation을 recursive 하게 수행한다. `donate_priority()` 의 자세한 로직은 하기 서술한다.

```c
void lock_acquire(struct lock *lock) {
  ASSERT(lock != NULL);
  ASSERT(!intr_context());
  ASSERT(!lock_held_by_current_thread(lock));

  struct thread *current_thread = thread_current();

  if (lock->holder != NULL) {
    current_thread->waiting_lock = lock;
    list_insert_ordered(&lock->holder->donations,
                        &current_thread->donation_elem,
                        compare_thread_priority, NULL);
    donate_priority();
  }
  sema_down(&lock->semaphore);
  current_thread->waiting_lock = NULL;
  lock->holder = current_thread;
}
```

### lock_release

공유 자원에 대한 락을 해제하게 된다면, 해당 락을 사용하고 싶어 우선순위를 빌려주었던 스레드 들에게 우선순위를 다시 반환해야한다.
`donations` 리스트에 있는 스레드가 우선순위를 빌려주었던 이유인 `waiting_lock`이 현재 락이라면, `donations` 리스트에서
해당 스레드를 제거한다. 그리고 `update_donations()`를 호출해 `donations` 리스트에 있는 스레드들의 우선순위를
재정렬 해준다.

```c
void lock_release(struct lock *lock) {
  ASSERT(lock != NULL);
  ASSERT(lock_held_by_current_thread(lock));

  lock->holder = NULL;

  clear_from_donations(lock);
  update_donations();

  sema_up(&lock->semaphore);
}
```

### clear_from_donations

위에서 언급한 이유로 donations 리스트를 순회하며 더이상 우선 순위를 빌려줄 필요가 없는 녀석들을 지워준다.

```c
void clear_from_donations(struct lock *lock) {
struct list_elem *e;
struct thread *t;

e = list_begin(&thread_current()->donations);
  for (e; e != list_end(&thread_current()->donations); e = list_next(e)) {
    t = list_entry(e, struct thread, donation_elem);
    if (t->waiting_lock == lock) {
      list_remove(e);
    }
  }
}
```

### update_donations

`donations` 리스트를 순회하며 스레드의 우선순위를 재정렬 해준다. 만약 `donations` 리스트가 비어있다면, 스레드의
원래 우선순위를 돌려준다. 그리고 `donations` 리스트에 있는 스레드들 중 가장 우선순위가 높은 스레드의 우선순위를
현재 스레드의 우선순위로 재배정을 해준다.

```c
void update_donations(void) {
  struct thread *current_thread;
  
  current_thread = thread_current();
  
  current_thread->priority = current_thread->original_priority;
  
  if (list_empty(&current_thread->donations)) {
    current_thread->priority = current_thread->original_priority;
  return;
  }

  struct thread *max = list_entry(
    list_max(&current_thread->donations, compare_thread_priority, NULL),
    struct thread,
    donation_elem
  );
  
  if (max->priority > current_thread->original_priority) {
    current_thread->priority = max->priority;
  }
}
```

### donate_priority

우선순위 기부를 recursive 하게 수행한다. `MAX_DONATION_DEPTH` 를 넘어가면 더이상 기부를 하지 않는다. 현재 스레드가
대기하고 있는 락의 홀더에 대해서 만약에 홀더의 우선순위가 현재 스레드의 우선순위보다 낮다면, 홀더의 우선순위를
현재 스레드의 우선순위로 재설정해준다. `depth` 변수가 추가되었는데, 도네이션 상황에서 donation chain 을 고려해야하는
스펙이다. 하지만, 얼마나 깊이까지 donation chain 을 고려해야하는지에 대한 스펙은 없어 디자인 레포트는 이 문제에 대해
고려해야한다고 언급하고 설계는 없었다. 이 부분에 대해서는 구현상의 변경점으로 discussion 섹션에서 설명한다.

```c
void donate_priority() {
  struct thread *temp_t = thread_current();
  int depth = 0;

  while (depth < MAX_DONATION_DEPTH && temp_t->waiting_lock != NULL) {
    temp_t = temp_t->waiting_lock->holder;
    
    if (temp_t->priority < thread_current()->priority)
      temp_t->priority = thread_current()->priority;
    
    depth++;
  }
}
```

### thread_set_priority

현재 스레드의 우선순위를 임의로 변경한다. 우선순위가 변경될 때마다, 도네이션의 상황을 고려해야하기 때문에, 바로 우선순위를 배정하지 않고
`original_priority` 에 저장해두고, `update_donations()` 과 `donate_priority()` 를 호출해 도네이션을 고려해
`priority` 는 indirectly 업데이트하도록 한다. 추가로, 스레드의 우선 순위를 변경으로 `preemptive yield` 상황이 발생할
수 있기 때문에 그 부분에 대한 코드가 추가 되었다. 만약에 `ready_list` 가 비어있다면, `thread_yield()` 를 호출하지 않고
원래 인터럽트 상태를 복구하고 함수를 종료한다. 만약에 `ready_list` 에 스레드가 대기하고 있는 상태라면, `ready_list` 에서
가장 우선순위가 높은 스레드를 뽑아 현재 스레드의 우선순위와 비교해 현재 스레드의 우선순위가 더 낮다면, voluntary yield 를 한다.
이때 후술할 레이스 컨디션등의 문제로 인해 인터럽트 컨텍스트에서 호출되는 경우에는 `thread_yield()` 를 호출하지 않는다.

```c
void thread_set_priority(int new_priority) {
  ASSERT(!thread_mlfqs);
  enum intr_level old_level;
  
  old_level = intr_disable();
  thread_current()->original_priority = new_priority;
  update_donations();
  donate_priority();
  
  if (list_empty(&ready_list)) {
    intr_set_level(old_level);
    return;
  }
  
  struct thread *t = list_entry(list_front(&ready_list), struct thread, elem);
  if (new_priority < t->priority)
    if (!intr_context())
      thread_yield();
  intr_set_level(old_level);
}
```

### cond

우선도 기반의 스케쥴링이 도입됨에 따라 cond 도 우선도 기반으로 움직여야한다. 따라서, 아래와 같이 cond 의 waiters 리스트를
정렬하는 기능을 추가했다.

```diff
Subject: [PATCH] cond 도 thread_priority 기준으로 정렬하도록 구현
diff --git a/src/threads/synch.c b/src/threads/synch.c
--- a/src/threads/synch.c	(revision ed0f9fc2dc7bd68b02f456cd811b4f3756d5c9fe)
+++ b/src/threads/synch.c	(revision 11051e72892e05129a3ca88266dc98485fec1032)

 /* Initializes condition variable COND.  A condition variable
    allows one piece of code to signal a condition and cooperating
    code to receive the signal and act upon it. */
@@ -266,7 +266,7 @@
   ASSERT(lock_held_by_current_thread(lock));
 
   sema_init(&waiter.semaphore, 0);
-  list_push_back(&cond->waiters, &waiter.elem);
+  list_insert_ordered(&cond->waiters, &waiter.elem, compare_sema_priority, NULL);
   lock_release(lock);
   sema_down(&waiter.semaphore);
   lock_acquire(lock);
@@ -286,6 +286,7 @@
   ASSERT(lock_held_by_current_thread(lock));
 
   if (!list_empty(&cond->waiters))
+    list_sort(&cond->waiters, compare_sema_priority, NULL);
     sema_up(&list_entry(list_pop_front(&cond->waiters),
                         struct semaphore_elem, elem)
                  ->semaphore);

}
```

## Discussion

큰 틀에서 디자인 리포트와 실제 구현은 크게 다르지 않다. 하지만, 가장 크게 차이가 갈렸던 부분은 preemptive yield 에 관한 부분이다.

### preemptive yield

이번 프로젝트에서는 특정 스레드의 우선도가 변경될 때, 스레드가 대기 리스트에 있다면, cpu 를 뺐거나, 현재 스레드가 cpu 를 가지고
있는데, 우선도가 낮다면, cpu 를 양보해야한다. 이에 대한 해결책으로써 `preemptive_yield()` 함수를 설계 했다. 하지만,
`alarm_clock`과 병합하는 과정에서 문제가 생겼다. `alarm_clock` 의 구현에서는 스레드를 직접적으로 관리하는 것이 아니라,
세마포어를 통해 간접적으로 관리한다. 그래서 `sema_up()` 에서 `preemptive_yield()` 를 호출할 때 만약에 `sema_up()` 이
interrupt context 에서 불린거라면, `thread_yield()`를 호출해서는 안된다. 왜냐하면, 인터럽트는 CPU의 제어권을 뺏는 행위인데,
제어권을 뺐은 상태에서 yield 를 부르게 된다면, 다양한 사이드 이펙트 등이 생긴다. 예를 들자면, yield 중에 timer interrupt 가
걸리고, scheduler 도 yield 가 필요하다 판단해 `intr_yield_on_return`을 통해 또 yield 하게 되면, 다른 로직 버그들을
일으킨다. 이 경우에는 `thread` 와 외부 하드웨어 모듈간의 레이스 컨디션이기 때문에, 세마포어나 락을 통해 synchronization 확보가
불가능해, 인터럽트를 사용한다. 따라서 다음과 같이 구현이 변경되었다. `sema_up()`에서 preempt 해야한다 판단이 되면,
만약에 인터럽트 컨택스트 안이라면 `intr_yield_on_return`, 아니라면 `thread_yield()` 를 호출한다. preempt 에 대한 판단중
`TIME_SLICE` 로 `thread_yield` 가 이뤄진다면, `ready_list` 에서 레이스 컨디션이 발생할 수 있다. 디자인 리포트에서
언급했던, preemptive yield 로직이 들어가야하는 곳에 적절하게 인터럽트에 따라서 yield 전 준비를 하는 로직을 추가해 해결했습니다.

```c
// thread_create
old_level = intr_disable();
if (thread_current()->priority < t->priority)
if (!intr_context())
thread_yield();
intr_set_level(old_level);

// sema_up
void sema_up(struct semaphore *sema) {
// initialize
old_level = intr_disable();
check_yield_on_return = false;
if (!list_empty(&sema->waiters)) {
check_yield_on_return = true;
// sort the waiters list and unblock
}
sema->value++;
intr_set_level(old_level);

old_level = intr_disable();
if (t != NULL) {
if (check_yield_on_return && thread_current()->priority < t->priority) {
if (intr_context())
intr_yield_on_return();
else
thread_yield();
}
}
intr_set_level(old_level);
}

// thread_set_priority
struct thread *t = list_entry(list_front(&ready_list),
struct thread, elem);
if (new_priority < t->priority)
if (!intr_context())
thread_yield();
intr_set_level(old_level);
```

### donatation chain

디자인 리포트에서 탐색 깊이에 대한 핸들링이 있어야한다 언급했었는데, 이부분은 테스트 코드를 확인함으로써 처리 했다. `priority-donate-chain`
코드 상에서 `#define NESTING_DEPTH 8` 으로 선언 되어있어 이 상수를 반영해서 구현했다. 이 부분에 대한 상세한 스펙 명시가 없어
테스트 코드에 따라 임의로 구현했고, 값을 조정함으로 수정하면 된다.

# Advanced Scheduler

개선된 스케쥴러인 MLFQS를 구현해야 합니다.

## Solution

MLFQS 구현하기 위해서는 `recent_cpu`, `load_avg`를 계산하는 데 필요한 실수 연산이 있어야 합니다.
pintos에서 기본적으로 제공하는 부동소수점 연산이 없기 때문에 직접 고정점 연산을 `threads/fixed-point.c`에서 구현했습니다.

MLFQS를 구현하기 위해서 새로운 함수들을 작성했습니다. 각 함수는 공식 문서에 제시된 계산 식들을 참고했습니다.

```c
void calculate_priority(struct thread *t) {
  if (t != idle_thread) {
    int nice = t->nice;
    fixed_t recent_cpu = t->recent_cpu;
    t->priority = fp2int(fp_add_n(fp_div_n(recent_cpu, -4), PRI_MAX - nice * 2));
    if (t->priority > PRI_MAX)
      t->priority = PRI_MAX;
    else if (t->priority < PRI_MIN)
      t->priority = PRI_MIN;
  }
}

void increase_recent_cpu(struct thread *t) {
  if (t != idle_thread) {
    t->recent_cpu = fp_add_n(t->recent_cpu, 1);
  }
}

void calculate_recent_cpu(struct thread *t) {
  if (t != idle_thread) {
    fixed_t load_avg_mul_2 = fp_mul_n(load_avg, 2);
    t->recent_cpu = fp_add_n(
        fp_mul_y(
            fp_div_y(load_avg_mul_2, fp_add_n(load_avg_mul_2, 1)),
            t->recent_cpu),
        t->nice);
  }
}

void calculate_load_avg() {
  size_t size = list_size(&ready_list);
  if (thread_current() != idle_thread)
    size += 1;
  fixed_t c1 = fp_div_n(int2fp(59), 60);
  fixed_t c2 = fp_div_n(int2fp(1), 60);
  load_avg = fp_add_y(fp_mul_y(c1, load_avg), fp_mul_n(c2, size));
}
```

이 함수들은 각각 언제 어떻게 호출하는지에 대한 기본 규칙이 존재합니다.

- `calculate_priority()`: 4틱 마다 모든 쓰레드에 대해 호출합니다.
- `increase_recent_cpu()`: 매 틱마다 현재 쓰레드에 대해 호출합니다.
- `calculate_recent_cpu()`: 1초마다 모든 쓰레드에 대해 호출 합니다.
- `calculate_load_avg()`: 1초마다 호출합니다.

각 규칙을 적용하기 위해 `devices/timer.c`의 `timer_interrupt()`에 위 함수들을 다음과 같이 추가했습니다.

```c
static void
timer_interrupt(struct intr_frame *args UNUSED) {
  ticks++;
  thread_tick();
  thread_wakeup(ticks);
  if (thread_mlfqs) {
    increase_recent_cpu(thread_current());
    if (ticks % TIMER_FREQ == 0) {
      calculate_load_avg();
      thread_foreach(calculate_recent_cpu, NULL);
    }
    if (ticks % 4 == 0) {
      thread_foreach(calculate_priority, NULL);
      sort_ready_list();
    }
  }
}
```

모든 쓰레드에 대해 호출해야 하는 함수인` calculate_priority()`와 `calculate_recent_cpu()`는 pintos에서 기본 구현되어있는 `thread_foreach()`를 이용해
호출합니다.

`calculate_priority()`에 의해서 쓰레드의 우선 순위가 조정된 후에는 `ready_list`를 새로 정렬해야 합니다.
`devices/timer.c`에서는 `threads/thread.c`에서 정의된 `ready_list`에 접근할 수 없기 때문에 이를 정렬하는 함수를 따로 작성하여 `calculate_priority()`를
호출하는 부분 뒷쪽에 추가해 줬습니다.

## Discussion

공식 문서에서 설명하는 MLFQS는 가능한 우선 순위에 대한 큐를 각각 가지고 있고, 다음 실행될 쓰레드를 비어있지 않은 가장 높은 우선순위의 큐에서 라운드-로빈 방식으로 선택합니다.
하지만, 4틱마다 모든 쓰레드의 우선 순위를 새로 계산하는데, 매번 쓰레드들을 서로 다른 큐로 옮기는 것은 오버헤드가 큰 작업이라고 생각했습니다.
또한 기존의 `ready_list`를 우선 순위에 따라 정렬된 상태로 유지하기 위한 구현들을 해두었기 때문에 이를 그대로 사용하기로 결정했습니다.
`ready_list`가 우선 순위를 기준으로 정렬된 상태로 유지되고 있다면 각 우선 순위에 대한 큐를 순서대로 이어붙인 것과 동등하기 때문입니다.
