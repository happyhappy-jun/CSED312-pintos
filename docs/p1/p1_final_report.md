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

## Solution

## Discussion