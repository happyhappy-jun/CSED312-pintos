//
// Created by 김치헌 on 2023/11/23.
//

#include "spt.h"
#include "filesys/file.h"
#include "string.h"
#include "swap.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/frame.h"

static struct spt_entry *spt_add(struct spt *, struct spt_entry *);
static void spt_remove_helper(struct hash_elem *, void *UNUSED);
static struct hash_elem *spt_get_hash_elem(struct spt *, void *);
static struct spt_entry *spt_make_clean_spt_entry(void *, bool, bool);
static void spt_load_page_into_frame_from_file(struct spt_entry *);
static void spt_load_page_into_frame_from_swap(struct spt_entry *);
static void spt_evict_page_from_frame_into_file(struct spt_entry *);
extern struct lock file_lock;

// Initialize spt
void spt_init(struct spt *spt) {
  hash_init(&spt->spt, spt_hash, spt_less, NULL);
}

// Destroy spt
void spt_destroy(struct spt *spt) {
  hash_destroy(&spt->spt, spt_remove_helper);
}

// hash function for spt
unsigned spt_hash(const struct hash_elem *elem, void *aux UNUSED) {
  struct spt_entry *spte = hash_entry(elem, struct spt_entry, elem);
  return hash_bytes(&spte->upage, sizeof(spte->upage));
}

// less function for spt
bool spt_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) {
  struct spt_entry *spte_a = hash_entry(a, struct spt_entry, elem);
  struct spt_entry *spte_b = hash_entry(b, struct spt_entry, elem);
  return spte_a->upage < spte_b->upage;
}

/* Get spt_entry by upage
 *
 * Caller should page-align the upage by pg_round_down(). */
struct spt_entry *spt_get_entry(struct spt *spt, void *upage) {
  struct hash_elem *e = spt_get_hash_elem(spt, upage);
  if (e) {
    return hash_entry(e, struct spt_entry, elem);
  }
  return NULL;
}

// Get hash_elem by upage
static struct hash_elem *spt_get_hash_elem(struct spt *spt, void *upage) {
  struct spt_entry spte;
  spte.upage = upage;
  return hash_find(&spt->spt, &spte.elem);
}

/* Add spt_entry into spt hash table
 *
 * If added successfully, return original spte which is added.
 * If spt_entry exists, return existing spt_entry. */
static struct spt_entry *spt_add(struct spt *spt, struct spt_entry *spte) {
  struct hash_elem *e = hash_insert(&spt->spt, &spte->elem);
  if (e == NULL)
    return spte;
  struct spt_entry *existing_spte = hash_entry(e, struct spt_entry, elem);
  // Todo: what if spt already has the same entry with given upage?
  // 1. overwrite the given info into existing entry
  // 2. ignore the given info
  // 3. PANIC

  // free spte that is not added to spt
  if (spte->file_info)
    free(spte->file_info);
  free(spte);
  return existing_spte;
}

// Make clean spt_entry
static struct spt_entry *spt_make_clean_spt_entry(void *upage, bool writable, bool is_file) {
  struct spt_entry *spte = malloc(sizeof(struct spt_entry));
  if (spte == NULL)
    PANIC("malloc new spt_entry failed");
  spte->upage = upage;
  spte->kpage = NULL;
  spte->is_loaded = false;
  spte->writable = writable;
  spte->is_file = is_file;
  spte->is_swapped = false;
  spte->swap_index = -1;
  return spte;
}

// Add file-backed spt_entry
struct spt_entry *spt_add_file(struct spt *spt, void *upage, bool writable, struct file *file, off_t ofs, uint32_t read_bytes, uint32_t zero_bytes) {
  struct spt_entry *spte = spt_make_clean_spt_entry(upage, writable, true);
  struct spt_entry_file_info *file_info = malloc(sizeof(struct spt_entry_file_info));
  file_info->file = file;
  file_info->ofs = ofs;
  file_info->read_bytes = read_bytes;
  file_info->zero_bytes = zero_bytes;
  spte->file_info = file_info;
  return spt_add(spt, spte);
}

// Add non file-backed spt_entry
struct spt_entry *spt_add_anon(struct spt *spt, void *upage, bool writable) {
  struct spt_entry *spte = spt_make_clean_spt_entry(upage, writable, false);
  spte->file_info = NULL;
  return spt_add(spt, spte);
}

/* Free spt_entry
 *
 * Used as DESTRUCTOR for hash_destroy() */
static void spt_remove_helper(struct hash_elem *elem, void *aux UNUSED) {
  struct spt_entry *spte = hash_entry(elem, struct spt_entry, elem);
  if (spte->is_loaded) {
    spt_evict_page_from_frame(spte);
//    pagedir_clear_page(thread_current()->pagedir, spte->upage);
  }
  if (spte->is_swapped)
    swap_free(spte->swap_index);
  // in swap free, we may need to check spt_entry and write back to the file
  if (spte->file_info)
    free(spte->file_info);

  // Todo: Don't sure if this is correct to free the spte itself in spt_remove_helper().
  // should read hash.h and hash.c to check DESTRUCTOR.
  free(spte);
}

// Remove spt_entry by upage
void spt_remove_by_upage(struct spt *spt, void *upage) {
  struct hash_elem *e = spt_get_hash_elem(spt, upage);
  if (e) {
    spt_remove_by_entry(spt, hash_entry(e, struct spt_entry, elem));
  }
}

// Remove spt_entry
void spt_remove_by_entry(struct spt *spt, struct spt_entry *spte) {
  hash_delete(&spt->spt, &spte->elem);
  spt_remove_helper(&spte->elem, NULL);
}

/* Load page into frame
 *
 * Caller should install the loaded page by install_page()
 *
 * Set kpage, is_loaded
 * Clear is_swapped, swap_index if swapped in */
bool spt_load_page_into_frame(struct spt_entry *spte) {
  ASSERT(!spte->is_loaded);
  ASSERT(spte->kpage == NULL);
  spte->kpage = frame_alloc(spte->upage, PAL_USER);
  if (spte->is_swapped) {
    spt_load_page_into_frame_from_swap(spte);
  } else if (spte->is_file) {
    spt_load_page_into_frame_from_file(spte);
  } else {
    memset(spte->kpage, 0, PGSIZE);
  }
  spte->is_loaded = true;
  bool result = install_page(spte->upage, spte->kpage, spte->writable);
  unpin_frame(spte->kpage);
  return result;
}

// Load page from file
static void spt_load_page_into_frame_from_file(struct spt_entry *spte) {
  ASSERT(spte->is_file);

  lock_acquire (&file_lock);
  file_seek(spte->file_info->file, spte->file_info->ofs);
  int read_bytes = file_read(spte->file_info->file, spte->kpage, spte->file_info->read_bytes);
  lock_release(&file_lock);

  if (read_bytes != (int) spte->file_info->read_bytes) {
    PANIC("Load from file failed");
  }

  memset(spte->kpage + spte->file_info->read_bytes, 0, spte->file_info->zero_bytes);
}

// Load page from swap disk
static void spt_load_page_into_frame_from_swap(struct spt_entry *spte) {
  ASSERT(spte->is_swapped);

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
void spt_evict_page_from_frame(struct spt_entry *spte) {
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
    spt_evict_page_from_frame_into_file(spte);
  }

  // 1. dirty page in executable (like data region)
  // 2. anon page (whether dirty or not)
  // will be swapped out when an eviction occurs.
  if ((is_dirty && !can_write) || !spte->is_file) {
    spte->is_swapped = true;
    spte->swap_index = swap_out(spte->kpage);
  }
  unpin_frame(spte->kpage);
  frame_free(spte->kpage);
  spte->is_loaded = false;
  spte->kpage = NULL;
  pagedir_clear_page(target_holder->pagedir, spte->upage);
}

static void spt_evict_page_from_frame_into_file(struct spt_entry *spte) {
  ASSERT(spte->is_file)

  struct spt_entry_file_info* file_info = spte->file_info;
  lock_acquire(&file_lock);
  int write_bytes = file_write_at(file_info->file, spte->kpage, file_info->read_bytes, file_info->ofs);
  lock_release(&file_lock);

  if (write_bytes != (int) file_info->read_bytes) {
    PANIC("Evict into file failed");
  }
}