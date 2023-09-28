//
// Created by Yoon Jun on 2023/09/22.
//

#include "compare.h"
#include "threads/synch.h"
#include "threads/thread.h"

bool compare_thread_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
  struct thread *thread_a = list_entry(a, struct thread, elem);
  struct thread *thread_b = list_entry(b, struct thread, elem);

  const int priority_a = thread_a->priority;
  const int priority_b = thread_b->priority;

  return priority_a > priority_b;
}

bool compare_sema_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	struct semaphore_elem *sa = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *sb = list_entry(b, struct semaphore_elem, elem);

	struct list_elem *sa_elem = list_begin(&sa->semaphore.waiters);
	struct list_elem *sb_elem = list_begin(&sb->semaphore.waiters);

	struct thread *ta = list_entry(sa_elem, struct thread, elem);
	struct thread *tb = list_entry(sb_elem, struct thread, elem);

	if (ta->priority > tb->priority)
		return true;

	return false;
}
