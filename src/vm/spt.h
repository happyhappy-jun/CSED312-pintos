//
// Created by 김치헌 on 2023/11/23.
//

#ifndef PINTOS_SRC_VM_SPT_H_
#define PINTOS_SRC_VM_SPT_H_

#include "filesys/off_t.h"
#include <hash.h>

struct spt {
  struct hash spt;
};

struct spt_entry_file_info {
  struct file *file; // file pointer
  off_t ofs;  // offset
  uint32_t read_bytes; // read bytes
  uint32_t zero_bytes; // zero bytes
};

struct spt_entry {
  /* general info */
  void *upage; // user page
  void *kpage; // backed kernel page; NULL if frame not allocated, else frame->kpage.
  bool is_loaded; // true if frame allocated, else false.
  bool writable; // true if page is writable, else false.
  bool is_file; // true if file-backed page(executable/memory map), else (stack, etc) false.
  bool is_dirty; // true if file-backed page ever modified, else false.
  struct hash_elem elem; // hash element for spt.

  /* file info */
  struct spt_entry_file_info *file_info; // file info; NULL if not file-backed, else file_info.

  /* anon info */
  bool is_swapped; // true if swapped, else false.
  unsigned swap_index; // swap index; 0 if not in swap, else swap->index.
};

void spt_init(struct spt *);
void spt_destroy(struct spt *);
unsigned spt_hash(const struct hash_elem *, void * UNUSED);
bool spt_less(const struct hash_elem *, const struct hash_elem *, void * UNUSED);

struct spt_entry *spt_get_entry(struct spt *, void *);
struct spt_entry *spt_add_file(struct spt *, void *, bool, struct file *, off_t, uint32_t, uint32_t);
struct spt_entry *spt_add_anon(struct spt *, void *, bool);
void spt_remove_by_upage(struct spt *, void *);
void spt_remove_by_entry(struct spt *, struct spt_entry *);
bool spt_load_page_into_frame(struct spt_entry *);
void spt_evict_page_from_frame(struct spt_entry *);

#endif//PINTOS_SRC_VM_SPT_H_
