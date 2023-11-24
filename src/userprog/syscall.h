#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/interrupt.h"
#include "userprog/process.h"
typedef int mmapid_t;

struct mmap_entry {
  mmapid_t id;
  struct file *file;
  struct list_elem elem;
  void *addr;
  size_t size;
};

void syscall_init(void);
bool sys_munmap(mmapid_t);

#endif /* userprog/syscall.h */
