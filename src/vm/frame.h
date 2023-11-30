//
// Created by 김치헌 on 2023/11/30.
//

#ifndef PINTOS_SRC_VM_FRAME_H_
#define PINTOS_SRC_VM_FRAME_H_

#include "hash.h"
#include "threads/synch.h"
#include "threads/palloc.h"

struct frame_table {
  struct hash frame_table;
  struct lock frame_table_lock;
};

struct frame {
  void *kpage;
  void *upage;
  struct thread *thread;
  struct hash_elem elem;
  int64_t timestamp;
};

void frame_table_init(void);

void *frame_alloc(void *upage, enum palloc_flags flags);
void frame_free(void *kpage);

#endif//PINTOS_SRC_VM_FRAME_H_
