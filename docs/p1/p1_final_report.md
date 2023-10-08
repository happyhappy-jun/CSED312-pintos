# Project 1: Threads

## Preliminaries

Team Number: 20

윤병준(20190766)

김치헌(20190806)

# Alarm Clock

Our goal was to reimplement the `timer_sleep()` function defined in `devices/timer.c` which was originally implemented
as "busy waiting".

## Solution

We solved this problem by defining a `sleep_list` and its element `sleep_list_elem` and keeping the list in sorted
manner.

`sleep_list_elem` is defined as below.

```c
struct sleep_list_elem {
  struct list_elem elem;          /* List element */
  int64_t end_tick;               /* Tick to wake up */
  struct semaphore semaphore;     /* Semaphore to block a sleeping thread */
};
```

Everytime a thread calls `timer_sleep()`, the new defined `thread_sleep()` is called eventually.

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

`thread_sleep()` initiates a new `sleep_elem` and insert it into `sleep_list` in ascending order of `end_tick`.
Then, it makes the thread sleeps until the `sema_up()` using `sema_down()`.

To wake up the sleeping threads, we make a new function `thread_wakeup()` and call it every single tick.

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

`thread_wakeup()` iterates the `sleep_list`, awakening the threads until it finds the element whose `end_tick` is
greater than the current tick.
It means the element and every element behind hold the threads which have to sleep more because the `sleep_list` is
sorted in ascending order of `end_tick`.

## Discussion

# Priority Scheduler

## Solution

## Discussion

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
