//
// Created by 김치헌 on 2023/11/30.
//

#include "vm/mmap.h"
#include "filesys/file.h"
#include "page.h"
#include "threads/malloc.h"
#include "threads/thread.h"

extern struct lock file_lock;
static struct mmap_entry *mmap_find(struct mmap_list *mmap_list, mmapid_t id);

void mmap_init(struct mmap_list *mmap_list) {
    list_init(&mmap_list->list);
    mmap_list->next_id = 0;
}

void mmap_destroy(struct mmap_list *mmap_list) {
    struct list_elem *e;
    while (!list_empty(&mmap_list->list)) {
        e = list_front(&mmap_list->list);
        struct mmap_entry *mmap_entry = list_entry(e, struct mmap_entry, elem);
        mmap_unmap_file(mmap_list, mmap_entry->id);
        free(mmap_entry);
    }
}

mmapid_t mmap_map_file(struct mmap_list *mmap_list, struct file *file, void *addr) {
  struct thread *cur = thread_current();
  struct spt *spt = &cur->spt;
  int fail_offset = -1;

  lock_acquire(&file_lock);
  struct file *file_copy = file_reopen(file);
  lock_release(&file_lock);
  if (file_copy == NULL) {
    return MMAP_FAILED;
  }

  lock_acquire(&file_lock);
  off_t size = file_length(file_copy);
  lock_release(&file_lock);
  if (size == 0) {
    return MMAP_FAILED;
  }

  for (size_t offset=0; offset < size; offset += PGSIZE) {
    size_t read_bytes = (offset + PGSIZE < size ? PGSIZE : size - offset);
    size_t zero_bytes = PGSIZE - read_bytes;
    struct spt_entry *spte = spt_insert_mmap(spt, addr + offset, file_copy, (off_t) offset, read_bytes, zero_bytes);
    if (spte == NULL) {
      fail_offset = (int)offset;
      break;
    }
  }

  if (fail_offset != -1) {
    for (size_t offset=0; offset < (size_t)fail_offset; offset += PGSIZE) {
      spt_remove(&cur->spt, addr + offset);
    }
    lock_acquire(&file_lock);
    file_close(file_copy);
    lock_release(&file_lock);
    return MMAP_FAILED;
  }

  struct mmap_entry *mmap_entry = malloc(sizeof(struct mmap_entry));
  mmap_entry->id = mmap_list->next_id++;
  mmap_entry->file = file_copy;
  mmap_entry->addr = addr;
  mmap_entry->size = size;
  list_push_back(&mmap_list->list, &mmap_entry->elem);
  return mmap_entry->id;
}

bool mmap_unmap_file(struct mmap_list *mmap_list, mmapid_t id) {
  struct mmap_entry *mmap_entry = mmap_find(mmap_list, id);
  if (mmap_entry == NULL) {
    return false;
  }

  if (mmap_entry->file != NULL) {
    struct thread *cur = thread_current();
    struct spt *spt = &cur->spt;
    void *addr = mmap_entry->addr;
    size_t size = mmap_entry->size;
    for (size_t offset=0; offset < size; offset += PGSIZE) {
      struct spt_entry *spte = spt_find(spt, addr + offset);
      ASSERT(spte != NULL)
      ASSERT(spte->type == MMAP)
      if (spte->location == LOADED)
        unload_page(spt, addr + offset);
      spt_remove(spt, addr + offset);
    }
    lock_acquire(&file_lock);
    file_close(mmap_entry->file);
    lock_release(&file_lock);
    mmap_entry->file = NULL;
  }
  list_remove(&mmap_entry->elem);
  return true;
}

static struct mmap_entry *mmap_find(struct mmap_list *mmap_list, mmapid_t id) {
  struct list_elem *e;
  for (e = list_begin(&mmap_list->list); e != list_end(&mmap_list->list); e = list_next(e)) {
    struct mmap_entry *mmap_entry = list_entry(e, struct mmap_entry, elem);
    if (mmap_entry->id == id) {
      return mmap_entry;
    }
  }
  return NULL;
}
