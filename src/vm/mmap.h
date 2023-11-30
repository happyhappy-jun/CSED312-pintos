//
// Created by 김치헌 on 2023/11/30.
//

#ifndef PINTOS_SRC_VM_MMAP_H_
#define PINTOS_SRC_VM_MMAP_H_

#include "list.h"

#define MMAP_FAILED (mmapid_t)(-1)

typedef int mmapid_t;

struct mmap_list {
  struct list list;
  mmapid_t next_id;
};

struct mmap_entry {
  mmapid_t id;
  struct file *file;
  struct list_elem elem;
  void *addr;
  size_t size;
};

void mmap_init(struct mmap_list *mmap_list);
void mmap_destroy(struct mmap_list *mmap_list);
mmapid_t mmap_map_file(struct mmap_list *mmap_list, struct file *file, void *addr);
bool mmap_unmap_file(struct mmap_list *mmap_list, mmapid_t id);


#endif//PINTOS_SRC_VM_MMAP_H_
