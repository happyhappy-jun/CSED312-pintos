//
// Created by 김치헌 on 2023/11/28.
//

#include "vm/mmap.h"
#include "spt.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

mmapid_t mmap_map_file(struct file *file, void *addr, size_t size) {
  struct thread *cur = thread_current();

  for (size_t offset=0; offset < size; offset += PGSIZE) {
    size_t read_bytes = (offset + PGSIZE < size ? PGSIZE : size - offset);
    size_t zero_bytes = PGSIZE - read_bytes;

    struct spt_entry *spte = spt_add_file(&cur->spt, addr + offset, true, file, (off_t) offset, read_bytes, zero_bytes);
    if (spte == NULL)
      return MAP_FAILED;
  }

  mmapid_t id;
  if (!list_empty(&cur->mmap_list))
    id = (mmapid_t) list_entry(list_back(&cur->mmap_list), struct mmap_entry, elem)->id + 1;
  else
    id = 0;

  struct mmap_entry *mmap_entry = malloc(sizeof(struct mmap_entry));
  mmap_entry->id = id;
  mmap_entry->file = file;
  mmap_entry->addr = addr;
  mmap_entry->size = size;
  list_push_back(&cur->mmap_list, &mmap_entry->elem);

  return id;
}

bool mmap_unmap_file(mmapid_t id) {
  struct thread *cur = thread_current();
  struct mmap_entry *mmap_entry = mmap_get_mmap_entry_by_id(id);

  if (mmap_entry == NULL)
    return false;

  size_t offset;
  void* addr = mmap_entry->addr;
  size_t size = mmap_entry->size;

  for (offset = 0; offset < size; offset += PGSIZE) {
    spt_remove_by_upage(&cur->spt, addr+offset);
  }

  list_remove(&mmap_entry->elem);
  free(mmap_entry);
  return true;
}

struct mmap_entry *mmap_get_mmap_entry_by_id(mmapid_t id) {
  struct thread *cur = thread_current();
  struct list_elem *e;
  for (e = list_begin(&cur->mmap_list); e != list_end(&cur->mmap_list); e = list_next(e)) {
    struct mmap_entry *mmap_entry = list_entry(e, struct mmap_entry, elem);
    if (mmap_entry->id == id)
      return mmap_entry;
  }
  return NULL;
}
