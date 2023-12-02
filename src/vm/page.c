//
// Created by 김치헌 on 2023/11/30.
//

#include "vm/page.h"
#include "debug.h"
#include "filesys/file.h"
#include "stdio.h"
#include "string.h"
#include "swap.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/frame.h"

extern struct lock file_lock;
static void load_file(void *kpage, struct spt_entry *spte);
static void load_swap(void *kpage, struct spt_entry *spte);
static void unload_file(void *kpage, struct spt_entry *spte);
static void unload_swap(void *kpage, struct spt_entry *spte);

bool load_page(struct spt *spt, struct spt_entry *spte) {
  void *kpage = frame_alloc(spte->upage, PAL_USER);
  bool success = load_page_data(kpage, spt, spte);
  if (!success) {
    frame_free(kpage);
    return false;
  }
  frame_set_spte(kpage, spte);
  frame_unpin(kpage);
  return true;
}

bool unload_page(struct spt *spt, struct spt_entry *spte) {
  bool success = unload_page_data(spt, spte);
  if (!success) {
    return false;
  }
  frame_free(spte->kpage);
  return true;
}


bool load_page_data(void *kpage, struct spt *spt, struct spt_entry *spte) {
  switch (spte->location) {
  case LOADED:
    printf("[tid:%d] waiting %p:%p (%d, %p)\n", thread_current()->tid, spte->upage, spte->kpage, frame_find(spte->kpage)->thread->tid, frame_find(spte->kpage)->upage);
    while(spte->location == LOADED);
    printf("[tid:%d] waiting done\n", thread_current()->tid);
    return load_page_data(kpage, spt, spte);
  case FILE:
    load_file(kpage, spte);
    break;
  case SWAP:
    load_swap(kpage, spte);
    break;
  case ZERO:
    memset(kpage, 0, PGSIZE);
    break;
  default:
    return false;
  }
  spte->location = LOADED;
  spte->kpage = kpage;
  return install_page(spte->upage, spte->kpage, spte->writable);
}


bool unload_page_data(struct spt *spt, struct spt_entry *spte) {
  ASSERT(spte->location == LOADED)
  bool dirty = spte->dirty;
  if (!dirty) {
    dirty = pagedir_is_dirty(spt->pagedir, spte->upage);
    spte->dirty = dirty;
  }
  pagedir_clear_page(spt->pagedir, spte->upage);

  switch (spte->type) {
  case MMAP:
    if (dirty) {
      unload_file(spte->kpage, spte);
      spte->dirty = false;
    }
    spte->location = FILE;
    break;
  case EXEC:
    if (dirty) {
      unload_swap(spte->kpage, spte);
      spte->location = SWAP;
    } else {
      spte->location = FILE;
    }
    break;
  case STACK:
    if (dirty) {
      unload_swap(spte->kpage, spte);
      spte->location = SWAP;
    } else {
      spte->location = ZERO;
    }
    break;
  default:
    return false;
  }
  ASSERT(spte->location != LOADED)
  return true;
}

static void load_file(void *kpage, struct spt_entry *spte) {
  ASSERT(kpage != NULL)
  ASSERT(spte->location == FILE)
  ASSERT(spte->file_info != NULL)
  struct file_info *file_info = spte->file_info;

  lock_acquire(&file_lock);
  int read_bytes = file_read_at(file_info->file, kpage, (int) file_info->read_bytes, file_info->offset);
  lock_release(&file_lock);
  memset(kpage + read_bytes, 0, (int) file_info->zero_bytes);

  if (read_bytes != (int) file_info->read_bytes) {
    PANIC("Failed to read file");
  }
}

static void load_swap(void *kpage, struct spt_entry *spte) {
  ASSERT(kpage != NULL)
  ASSERT(spte->location == SWAP)
  ASSERT(spte->swap_index != -1)

  swap_in(spte->swap_index, kpage);
  spte->swap_index = -1;
}

static void unload_file(void *kpage, struct spt_entry *spte) {
  ASSERT(kpage != NULL)
  ASSERT(spte->file_info != NULL)

  struct file_info *file_info = spte->file_info;

  lock_acquire(&file_lock);
  int write_bytes = file_write_at(file_info->file, kpage, (int) file_info->read_bytes, file_info->offset);
  lock_release(&file_lock);
  if (write_bytes != (int) file_info->read_bytes) {
    PANIC("Failed to write file");
  }
}

static void unload_swap(void *kpage, struct spt_entry *spte) {
  ASSERT(kpage != NULL)
  ASSERT(spte->swap_index == -1)

  spte->swap_index = (int) swap_out(kpage);
}
