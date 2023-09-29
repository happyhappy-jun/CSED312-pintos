//
// Created by Yoon Jun on 2023/09/22.
//

#include "compare.h"
#include "threads/thread.h"

bool compare_thread_priority(const struct list_elem *a, const struct list_elem *b, void *aux) {
  const int priority_a = list_entry(a,
  struct thread, elem) -> priority;
  const int priority_b = list_entry(b,
  struct thread, elem) -> priority;
  return priority_a > priority_b;
}

bool compare_thread_wakeup_tick(const struct list_elem *a,
                                const struct list_elem *b void *aux) {
  const struct sleep_list_elem *elem_a =
      list_entry(a, struct sleep_list_elem, elem);
  const struct sleep_list_elem *elem_b =
      list_entry(b, struct sleep_list_elem, elem);

  return elem_a->end_tick < elem_b->end_tick;
}