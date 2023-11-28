//
// Created by 김치헌 on 2023/11/28.
//

#ifndef PINTOS_SRC_VM_MMAP_H_
#define PINTOS_SRC_VM_MMAP_H_

#include "list.h"
#include "user/syscall.h"

typedef int mmapid_t;

struct mmap_entry {
  mmapid_t id;
  struct file *file;
  struct list_elem elem;
  void *addr;
  size_t size;
};

mmapid_t mmap_map_file(struct file *, void *, size_t);
bool mmap_unmap_file(mmapid_t);
struct mmap_entry *mmap_get_mmap_entry_by_id(mmapid_t);

#endif//PINTOS_SRC_VM_MMAP_H_
