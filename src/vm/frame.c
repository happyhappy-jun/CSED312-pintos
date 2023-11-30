//
// Created by 김치헌 on 2023/11/22.
//

#include "vm/frame.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"

struct frame_table frame_table;
struct lock frame_table_lock;

void frame_table_init(void) {
    lock_init(&frame_table_lock);
  hash_init(&frame_table.table, frame_table_hash, frame_table_less, NULL);
}

unsigned frame_table_hash(const struct hash_elem *elem, void *aux UNUSED) {
  struct frame *f = hash_entry(elem, struct frame, elem);
  return hash_bytes(&f->kpage, sizeof f->kpage);
}

bool frame_table_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) {
  struct frame *f1 = hash_entry(a, struct frame, elem);
  struct frame *f2 = hash_entry(b, struct frame, elem);
  return f1->kpage < f2->kpage;
}

struct frame *get_frame(void *kpage) {
  struct frame f;
  f.kpage = kpage;
  struct hash_elem *e = hash_find(&frame_table.table, &f.elem);
  if (e == NULL)
      return NULL;
  return hash_entry(e, struct frame, elem);
}

void *frame_alloc(void *upage, enum palloc_flags flags) {
  lock_acquire(&frame_table_lock);
  void *kpage = palloc_get_page(flags);
  if (kpage == NULL) {
    struct frame *target = get_frame_to_evict();
    struct thread *target_holder = target->thread;
    struct spt_entry *target_spte = spt_get_entry(&target_holder->spt, target->upage);
    spt_evict_page_from_frame(target_spte);
    pagedir_clear_page(target_holder->pagedir, target_spte->upage);
    kpage = palloc_get_page(flags);
    if (kpage == NULL) {
      PANIC("frame_alloc: palloc_get_page failed");
    }
  }
  struct frame *f = malloc(sizeof(struct frame));
  f->kpage = kpage;
  f->upage = upage;
  f->thread = thread_current();

  hash_insert(&frame_table.table, &f->elem);
  lock_release(&frame_table_lock);
  return kpage;
}

void frame_free(void *kpage) {
  struct frame f;
  f.kpage = kpage;
  bool holding_lock = lock_held_by_current_thread(&frame_table_lock);
  if (!holding_lock)
    lock_acquire(&frame_table_lock);
  struct hash_elem *e = hash_find(&frame_table.table, &f.elem);
  if (e != NULL) {
    hash_delete(&frame_table.table, e);
    palloc_free_page(kpage);
    free(hash_entry(e, struct frame, elem));
  }
  if (!holding_lock)
    lock_release(&frame_table_lock);
}

struct frame *get_frame_to_evict() {
  struct hash_iterator iter_hash;
  int i;
  for (i = 0; i < 2; i++) {
    hash_first(&iter_hash, &frame_table.table);
    do {
      struct frame *f = hash_entry(hash_cur(&iter_hash), struct frame, elem);
      if (f->pinned)
        continue;
      if (pagedir_is_accessed(f->thread->pagedir, f->upage)) {
        pagedir_set_accessed(f->thread->pagedir, f->upage, false);
        continue;
      }
      return f;
    } while (hash_next(&iter_hash));
  }

  return NULL;
}

void set_frame_pinning(void *kpage, bool pinned) {
  struct frame f_tmp;
  f_tmp.kpage = kpage;
  struct hash_elem *h = hash_find(&frame_table.table, &(f_tmp.elem));
  struct frame *f = hash_entry(h, struct frame, elem);
  f->pinned = pinned;
}

void unpin_frame(void *kpage) {
  set_frame_pinning(kpage, false);
}
void pin_frame(void *kpage) {
  set_frame_pinning(kpage, true);
}
