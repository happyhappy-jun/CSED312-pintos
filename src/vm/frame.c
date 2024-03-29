//
// Created by 김치헌 on 2023/11/30.
//

#include "vm/frame.h"
#include "devices/timer.h"
#include "page.h"
#include "stdio.h"
#include "string.h"
#include "threads/malloc.h"
#include "threads/thread.h"

static struct frame_table frame_table;

static unsigned frame_table_hash(const struct hash_elem *elem, void *aux);
static bool frame_table_less(const struct hash_elem *a, const struct hash_elem *b, void *aux);
static struct frame *frame_to_evict(void);


void frame_table_init(void) {
  hash_init(&frame_table.frame_table, frame_table_hash, frame_table_less, NULL);
  lock_init(&frame_table.frame_table_lock);
}

static unsigned frame_table_hash(const struct hash_elem *elem, void *aux) {
  struct frame *fte = hash_entry(elem, struct frame, elem);
  return hash_bytes(&fte->kpage, sizeof(fte->kpage));
}

static bool frame_table_less(const struct hash_elem *a, const struct hash_elem *b, void *aux) {
  struct frame *fte_a = hash_entry(a, struct frame, elem);
  struct frame *fte_b = hash_entry(b, struct frame, elem);
  return fte_a->kpage < fte_b->kpage;
}

struct frame *frame_find(void *kpage) {
  struct frame finder;
  finder.kpage = kpage;
  bool hold = lock_held_by_current_thread(&frame_table.frame_table_lock);
  if (!hold)
    lock_acquire(&frame_table.frame_table_lock);
  struct hash_elem *e = hash_find(&frame_table.frame_table, &finder.elem);
  if (!hold)
    lock_release(&frame_table.frame_table_lock);
  if (e == NULL) {
    return NULL;
  }
  return hash_entry(e, struct frame, elem);
}

void *frame_alloc(void *upage, enum palloc_flags flags) {
  void *kpage = palloc_get_page(flags);
  
  if (kpage == NULL) {
    kpage = frame_switch(upage, flags);
    return kpage;
  }
  
  struct frame *f = malloc(sizeof(struct frame));
  f->kpage = kpage;
  f->upage = upage;
  f->thread = thread_current();
  f->timestamp = timer_ticks();
  f->spte = NULL;

  bool hold = lock_held_by_current_thread(&frame_table.frame_table_lock);
  if (!hold)
    lock_acquire(&frame_table.frame_table_lock);
  hash_insert(&frame_table.frame_table, &f->elem);
  frame_pin(f->kpage);
  if (!hold)
    lock_release(&frame_table.frame_table_lock);
  return kpage;
}

void frame_free(void *kpage) {
  bool hold = lock_held_by_current_thread(&frame_table.frame_table_lock);
  if (!hold)
    lock_acquire(&frame_table.frame_table_lock);
  struct frame *f = frame_find(kpage);
  hash_delete(&frame_table.frame_table, &f->elem);
  palloc_free_page(kpage);
  free(f);
  if (!hold)
    lock_release(&frame_table.frame_table_lock);
}

void *frame_switch(void *upage, enum palloc_flags flags) {
  struct frame *target = frame_to_evict();
  struct thread *target_thread = target->thread;
  struct spt_entry *target_spte = target->spte;
  bool zero = flags & PAL_ZERO;

  if (target == NULL) {
    PANIC("Cannot find frame to evict");
  }

  target->upage = upage;
  target->thread = thread_current();
  target->timestamp = timer_ticks();
  target->spte = NULL;
  unload_page_data(&target_thread->spt, target_spte);

  if (zero)
    memset(target->kpage, 0, PGSIZE);

  return target->kpage;
}

static struct frame *frame_to_evict(void){
  struct frame *target = NULL;
  struct hash_iterator i;
  int64_t min = INT64_MAX;

  bool hold = lock_held_by_current_thread(&frame_table.frame_table_lock);
  if (!hold)
    lock_acquire(&frame_table.frame_table_lock);
  hash_first(&i, &frame_table.frame_table);
  while (hash_next(&i)) {
      struct frame *f = hash_entry(hash_cur(&i), struct frame, elem);
      if (f->pinned)
          continue;
      if (f->timestamp < min) {
          min = f->timestamp;
          target = f;
      }
  }
  frame_pin(target->kpage);
  if (!hold)
    lock_release(&frame_table.frame_table_lock);
  return target;
}

void frame_pin(void *kpage) {
  bool hold = lock_held_by_current_thread(&frame_table.frame_table_lock);
  if (!hold)
    lock_acquire(&frame_table.frame_table_lock);
  struct frame *f = frame_find(kpage);
  if (f!=NULL)
    f->pinned = true;
  if (!hold)
    lock_release(&frame_table.frame_table_lock);
}

void frame_unpin(void *kpage) {
  bool hold = lock_held_by_current_thread(&frame_table.frame_table_lock);
  if (!hold)
    lock_acquire(&frame_table.frame_table_lock);
  struct frame *f = frame_find(kpage);
  if (f!=NULL)
    f->pinned = false;
  if (!hold)
    lock_release(&frame_table.frame_table_lock);
}

bool frame_pinned(void *kpage) {
  struct frame *f = frame_find(kpage);
  if (f == NULL)
      return false;
  return f->pinned;
}

bool frame_test_and_pin(void *kpage) {
  bool result = true;
  bool hold = lock_held_by_current_thread(&frame_table.frame_table_lock);
  if (!hold)
      lock_acquire(&frame_table.frame_table_lock);

  if (frame_pinned(kpage))
      result = false;
  else
      frame_pin(kpage);

  if (!hold)
      lock_release(&frame_table.frame_table_lock);

  return result;
}


void frame_set_spte(void *kpage, struct spt_entry *spte) {
  struct frame *f = frame_find(kpage);
  f->spte = spte;
}