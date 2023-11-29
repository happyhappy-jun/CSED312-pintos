//
// Created by 김치헌 on 2023/11/22.
//

#include "vm/frame.h"
#include "filesys/file.h"
#include "stdio.h"
#include "string.h"
#include "swap.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"

static void frame_switch(struct frame *, void *);
static void load_page_into_frame_from_file(struct spt_entry *);
static void load_page_into_frame_from_swap(struct spt_entry *);
static void evict_page_from_frame_into_file(struct spt_entry *);
extern struct lock file_lock;
struct frame_table frame_table;
struct lock frame_table_lock;

void frame_table_init(void) {
    lock_init(&frame_table_lock);
  hash_init(&frame_table.table, frame_table_hash, frame_table_less, NULL);
}

unsigned frame_table_hash(const struct hash_elem *elem, void *aux UNUSED) {
  struct frame *f = hash_entry(elem, struct frame, elem);
  return hash_bytes(&f->kpage, sizeof f->kpage);
}

bool frame_table_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) {
  struct frame *f1 = hash_entry(a, struct frame, elem);
  struct frame *f2 = hash_entry(b, struct frame, elem);
  return f1->kpage < f2->kpage;
}

struct frame *get_frame(void *kpage) {
  struct frame f;
  f.kpage = kpage;
  struct hash_elem *e = hash_find(&frame_table.table, &f.elem);
  if (e == NULL)
      return NULL;
  return hash_entry(e, struct frame, elem);
}

static void frame_switch(struct frame *target, void *upage) {
  struct thread *target_holder = target->thread;
  struct spt_entry *target_spte = spt_get_entry(&target_holder->spt, target->upage);
  evict_page_from_frame(target_spte);
  pagedir_clear_page(target_holder->pagedir, target_spte->upage);
  target->upage = upage;
  target->thread = thread_current();
}

void *frame_alloc(void *upage, enum palloc_flags flags) {
  lock_acquire(&frame_table_lock);
  void *kpage = palloc_get_page(flags);
  if (kpage == NULL) {
    struct frame *target = get_frame_to_evict();
    frame_switch(target, upage);
    lock_release(&frame_table_lock);
    return target->kpage;
  }

  struct frame *f = malloc(sizeof(struct frame));
  f->kpage = kpage;
  f->upage = upage;
  f->thread = thread_current();
  hash_insert(&frame_table.table, &f->elem);
  lock_release(&frame_table_lock);
  return kpage;
}

void frame_free(void *kpage) {
  struct frame f;
  f.kpage = kpage;
  struct hash_elem *e = hash_find(&frame_table.table, &f.elem);
  if (e != NULL) {
    hash_delete(&frame_table.table, e);
    palloc_free_page(kpage);
    free(hash_entry(e, struct frame, elem));
  }
}

struct frame *get_frame_to_evict() {
  struct hash_iterator iter_hash;
  int i;
  for (i = 0; i < 2; i++) {
    hash_first(&iter_hash, &frame_table.table);
    do {
      struct frame *f = hash_entry(hash_cur(&iter_hash), struct frame, elem);
      if (f->pinned)
        continue;
      if (pagedir_is_accessed(f->thread->pagedir, f->upage)) {
        pagedir_set_accessed(f->thread->pagedir, f->upage, false);
        continue;
      }
      return f;
    } while (hash_next(&iter_hash));
  }

  return NULL;
}

void set_frame_pinning(void *kpage, bool pinned) {
  struct frame f_tmp;
  f_tmp.kpage = kpage;
  struct hash_elem *h = hash_find(&frame_table.table, &(f_tmp.elem));
  struct frame *f = hash_entry(h, struct frame, elem);
  f->pinned = pinned;
}

void unpin_frame(void *kpage) {
  set_frame_pinning(kpage, false);
}
void pin_frame(void *kpage) {
  set_frame_pinning(kpage, true);
}


/* Load page into frame
 *
 * Caller should install the loaded page by install_page()
 *
 * Set kpage, is_loaded
 * Clear is_swapped, swap_index if swapped in */
void load_page_into_frame(void *kpage, struct spt_entry *spte) {
  ASSERT(!spte->is_loaded);
  ASSERT(spte->kpage == NULL);
  pin_frame(kpage);
  spte->kpage = kpage;
  if (spte->is_file) {
    load_page_into_frame_from_file(spte);
  } else if (spte->is_swapped) {
    load_page_into_frame_from_swap(spte);
  } else {
    memset(spte->kpage, 0, PGSIZE);
  }
  spte->is_loaded = true;
  unpin_frame(spte->kpage);
}

// Load page from file
static void load_page_into_frame_from_file(struct spt_entry *spte) {
  ASSERT(spte->is_file);
  if (spte->is_swapped) {
    // writable file page can be swapped out!
    ASSERT(spte->writable)
    load_page_into_frame_from_swap(spte);
  } else {
    // Load this page.
    lock_acquire (&file_lock);
    file_seek(spte->file_info->file, spte->file_info->ofs);
    int read_bytes = file_read(spte->file_info->file, spte->kpage, spte->file_info->read_bytes);
    lock_release(&file_lock);

    if (read_bytes != (int) spte->file_info->read_bytes) {
      PANIC("Load from file failed");
    }
    memset(spte->kpage + spte->file_info->read_bytes, 0, spte->file_info->zero_bytes);
  }
}

// Load page from swap disk
static void load_page_into_frame_from_swap(struct spt_entry *spte) {
  ASSERT(spte->is_swapped);

  // Load this page.
  spte->is_swapped = false;
  swap_in(spte->swap_index, spte->kpage);

  spte->is_loaded = true;
}

/* Evict spte corresponding frame
 *
 * Dirty File-backed Page and Anon Page will be swapped out
 * Otherwise, Page data in the frame will just be freed
 *
 * Caller should clear the page table entry by pagedir_clear_page()
 *
 * Set is_swapped, swap_index if swapped out
 * Clear is_loaded, kpage */
void evict_page_from_frame(struct spt_entry *spte) {
  bool is_dirty;
  ASSERT(spte->is_loaded)
  ASSERT(spte->kpage != NULL)
  struct frame *target_frame = get_frame(spte->kpage);
  ASSERT(target_frame != NULL)
  struct thread *target_holder = target_frame->thread;
  ASSERT(target_holder != NULL)

  pin_frame(spte->kpage);

  if (spte->is_dirty) {
    is_dirty = true;
  } else {
    is_dirty = pagedir_is_dirty(target_holder->pagedir, spte->upage);
    spte->is_dirty = is_dirty;
  }

  bool can_write = false;
  if (spte->is_file)
    can_write = spte->file_info->file != target_holder->pcb->file;

  // dirty page will be write-backed into corresponding file. (unless it is executable)
  if (is_dirty && can_write) {
    evict_page_from_frame_into_file(spte);
  }

  // 1. dirty page in executable (like data region)
  // 2. anon page (whether dirty or not)
  // will be swapped out when an eviction occurs.
  if ((is_dirty && !can_write) || !spte->is_file) {
    spte->is_swapped = true;
    spte->swap_index = swap_out(spte->kpage);
  }

  unpin_frame(spte->kpage);

  spte->is_loaded = false;
  spte->kpage = NULL;
}

static void evict_page_from_frame_into_file(struct spt_entry *spte) {
  ASSERT(spte->is_file)

  struct spt_entry_file_info* file_info = spte->file_info;
  lock_acquire(&file_lock);
  int write_bytes = file_write_at(file_info->file, spte->kpage, file_info->read_bytes, file_info->ofs);
  lock_release(&file_lock);

  if (write_bytes != (int) file_info->read_bytes) {
    PANIC("Evict into file failed");
  }
}