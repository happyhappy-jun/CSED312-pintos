//
// Created by 김치헌 on 2023/11/22.
//

#ifndef PINTOS_SRC_VM_FRAME_H_
#define PINTOS_SRC_VM_FRAME_H_

#include "threads/palloc.h"
#include "threads/thread.h"
#include "devices/timer.h"
#include <hash.h>

struct frame_table {
  struct hash table;
};

struct frame {
  void *kpage;
  void *upage;
  struct thread *thread;
  struct hash_elem elem;
  bool pinned;
  int64_t alloced_tick;
};

void frame_table_init(void);
unsigned frame_table_hash(const struct hash_elem *, void *UNUSED);
bool frame_table_less(const struct hash_elem *, const struct hash_elem *, void *UNUSED);

struct frame *get_frame(void *kpage);
void *frame_alloc(void *, enum palloc_flags);
void frame_free(void *);

struct frame *get_frame_to_evict(void);

void load_page_into_frame(void *, struct spt_entry *);
void evict_page_from_frame(struct spt_entry *);

void pin_frame(void *kpage);
void unpin_frame(void *kpage);
void set_frame_pinning(void *kpage, bool pinned);

#endif//PINTOS_SRC_VM_FRAME_H_
