//
// Created by 김치헌 on 2023/11/22.
//

#include "vm/frame.h"
#include "threads/malloc.h"

struct frame_table frame_table;

void frame_table_init(void) {
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

void *frame_alloc(void *upage, enum palloc_flags flags) {
  void *kpage = palloc_get_page(flags);
  if (kpage == NULL) {
    // Todo : Evict
    PANIC("Evict");
  }

  struct frame *f = malloc(sizeof(struct frame));
  f->kpage = kpage;
  f->upage = upage;
  f->thread = thread_current();

  hash_insert(&frame_table.table, &f->elem);
  return kpage;
}

void frame_free(void *kpage) {
  struct frame f;
  f.kpage = kpage;
  struct hash_elem *e = hash_find(&frame_table.table, &f.elem);
  if (e != NULL) {
    hash_delete(&frame_table.table, e);
    palloc_free_page(kpage);
    free(hash_entry(e, struct frame, elem));
  }
}