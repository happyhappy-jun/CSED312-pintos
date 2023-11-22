//
// Created by 김치헌 on 2023/11/22.
//

#ifndef PINTOS_SRC_VM_FRAME_H_
#define PINTOS_SRC_VM_FRAME_H_

#include "threads/thread.h"
#include "threads/palloc.h"
#include <hash.h>

struct frame_table {
  struct hash table;
};

struct frame {
  void *kpage;
  void *upage;
  struct thread *thread;
  struct hash_elem elem;
};

void frame_table_init(void);
unsigned frame_table_hash(const struct hash_elem *, void * UNUSED);
bool frame_table_less(const struct hash_elem *, const struct hash_elem *, void * UNUSED);

void *frame_alloc(void *, enum palloc_flags);
void frame_free(void *);

#endif//PINTOS_SRC_VM_FRAME_H_
