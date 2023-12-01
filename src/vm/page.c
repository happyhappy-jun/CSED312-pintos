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
static void load_file(void *kbuffer, struct spt_entry *spte);
static void load_swap(void *kbuffer, struct spt_entry *spte);
static void unload_file(void *kbuffer, struct spt_entry *spte);
static void unload_swap(void *kbuffer, struct spt_entry *spte);

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
  void *kbuffer = palloc_get_page(PAL_ZERO);
  switch (spte->location) {
  case LOADED:
    palloc_free_page(kbuffer);
    while(spte->location == LOADED);
    return load_page_data(kpage, spt, spte);
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
  memcpy(kpage, kbuffer, PGSIZE);
  palloc_free_page(kbuffer);
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

  void *kbuffer = NULL;
  if (dirty) {
    kbuffer = palloc_get_page(PAL_ZERO);
    memcpy(kbuffer, spte->kpage, PGSIZE);
  }

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

  swap_in(spte->swap_index, kbuffer);
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
  ASSERT(spte->swap_index == -1)

  spte->swap_index = (int) swap_out(kbuffer);
}
