//
// Created by 김치헌 on 2023/11/30.
//

#include "vm/page.h"
#include "debug.h"
#include "filesys/file.h"
#include "stdio.h"
#include "string.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/frame.h"

extern struct lock file_lock;
static void load_file(void *kbuffer, struct spt_entry *spte);
static void load_swap(void *kbuffer, struct spt_entry *spte);
static void unload_file(void *kbuffer, struct spt_entry *spte);
static void unload_swap(void *kbuffer, struct spt_entry *spte);


bool load_page(struct spt *spt, void *upage) {
  struct spt_entry *spte = spt_find(spt, upage);
  if (spte == NULL) {
    return false;
  }

  void *kbuffer = palloc_get_page(PAL_ZERO);
  switch (spte->location) {
  case LOADED:
    PANIC("Page already loaded");
  case FILE:
    load_file(kbuffer, spte);
    break;
  case SWAP:
    load_swap(kbuffer, spte);
    break;
  case ZERO:
    memset(kbuffer, 0, PGSIZE);
    break;
  default:
    return false;
  }
  void *kpage = frame_alloc(upage, PAL_USER);
  memcpy(kpage, kbuffer, PGSIZE);
  palloc_free_page(kbuffer);
  spte->location = LOADED;
  spte->kpage = kpage;
  return install_page(spte->upage, spte->kpage, spte->writable);
}


bool unload_page(struct spt *spt, void *upage) {
  struct spt_entry *spte = spt_find(spt, upage);
  if (spte == NULL) {
    return false;
  }

  ASSERT(spte->location == LOADED)

  bool dirty = spte->dirty;
  if (!dirty) {
    dirty = pagedir_is_dirty(spt->pagedir, spte->upage);
    spte->dirty = dirty;
  }
  pagedir_clear_page(spt->pagedir, spte->upage);

  void *kbuffer = NULL;
  if (dirty) {
    kbuffer = palloc_get_page(PAL_ZERO);
    memcpy(kbuffer, spte->kpage, PGSIZE);
  }
  frame_free(spte->kpage);

  switch (spte->type) {
  case MMAP:
    if (dirty) {
      unload_file(kbuffer, spte);
      spte->dirty = false;
    }
    spte->location = FILE;
    break;
  case EXEC:
    if (dirty) {
      unload_swap(kbuffer, spte);
      spte->location = SWAP;
    } else {
      spte->location = FILE;
    }
    break;
  case STACK:
    if (dirty) {
      unload_swap(kbuffer, spte);
      spte->location = SWAP;
    } else {
      spte->location = ZERO;
    }
    break;
  default:
    return false;
  }
  if (dirty)
    palloc_free_page(kbuffer);
  return true;
}

static void load_file(void *kbuffer, struct spt_entry *spte) {
  ASSERT(kbuffer != NULL)
  ASSERT(spte->location == FILE)
  ASSERT(spte->file_info != NULL)
  struct file_info *file_info = spte->file_info;

  lock_acquire(&file_lock);
  file_seek(file_info->file, file_info->offset);
  int read_bytes = file_read(file_info->file, kbuffer, (int) file_info->read_bytes);
  lock_release(&file_lock);
  memset(kbuffer + read_bytes, 0, (int) file_info->zero_bytes);

  if (read_bytes != (int) file_info->read_bytes) {
    PANIC("Failed to read file");
  }
}

static void load_swap(void *kbuffer, struct spt_entry *spte) {
  ASSERT(kbuffer != NULL)
  ASSERT(spte->location == SWAP)
  ASSERT(spte->swap_index != -1)

  // Todo: swap in into kbuffer

  spte->swap_index = -1;
}

static void unload_file(void *kbuffer, struct spt_entry *spte) {
  ASSERT(kbuffer != NULL)
  ASSERT(spte->file_info != NULL)

  struct file_info *file_info = spte->file_info;

  lock_acquire(&file_lock);
  file_write_at(file_info->file, kbuffer, file_info->read_bytes, spte->file_info->offset);
  lock_release(&file_lock);
}

static void unload_swap(void *kbuffer, struct spt_entry *spte) {
  ASSERT(kbuffer != NULL)
  // Todo: swap out into swap disk from kbuffer
}