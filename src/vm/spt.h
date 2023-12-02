//
// Created by 김치헌 on 2023/11/30.
//

#ifndef PINTOS_SRC_VM_SPT_H_
#define PINTOS_SRC_VM_SPT_H_

#include "filesys/off_t.h"
#include "hash.h"
#include "stdbool.h"
#include "threads/synch.h"

enum spte_type {
  MMAP,
  EXEC,
  STACK
};

enum page_location {
  LOADED,
  FILE,
  SWAP,
  ZERO
};

struct spt {
  struct hash table;
  void *pagedir;
  struct lock spt_lock;
};

struct file_info {
  struct file *file;
  off_t offset;
  uint32_t read_bytes;
  uint32_t zero_bytes;
};

struct spt_entry {
  void *upage;
  void *kpage;
  bool writable;
  bool dirty;

  enum spte_type type;
  enum page_location location;

  struct file_info *file_info;
  int swap_index;

  struct hash_elem elem;
};

void spt_init(struct spt *spt);
void spt_destroy(struct spt *spt);

struct spt_entry *spt_find(struct spt *spt, void *upage);

struct spt_entry *spt_insert_mmap(struct spt *spt, void *upage, struct file *file, off_t offset, uint32_t read_bytes, uint32_t zero_bytes);
struct spt_entry *spt_insert_exec(struct spt *spt, void *upage, bool writable, struct file *file, off_t offset, uint32_t read_bytes, uint32_t zero_bytes);
struct spt_entry *spt_insert_stack(struct spt *spt, void *upage);

void spt_remove(struct spt *spt, void *upage);

#endif//PINTOS_SRC_VM_SPT_H_
