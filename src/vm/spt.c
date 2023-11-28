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
  // Todo: 7. On process termination
  // spt_remove_helper() will free each spt_entry in spt.
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
    pagedir_clear_page(thread_current()->pagedir, spte->upage);
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
void spt_load_page_into_frame(struct spt_entry *spte) {
  ASSERT(!spte->is_loaded);
  ASSERT(spte->kpage == NULL);
  if (spte->is_file) {
    spt_load_page_into_frame_from_file(spte);
  } else if (spte->is_swapped) {
    spt_load_page_into_frame_from_swap(spte);
  } else {
    // Not in file and not in swap disk
    // => load blank page
    spte->kpage = frame_alloc(spte->upage, PAL_USER | PAL_ZERO);
  }
  spte->is_loaded = true;
  unpin_frame(spte->kpage);
}

// Load page from file
static void spt_load_page_into_frame_from_file(struct spt_entry *spte) {
  ASSERT(spte->is_file);
  if (spte->is_swapped) {
    // writable file page can be swapped out!
    ASSERT(spte->writable)
    spt_load_page_into_frame_from_swap(spte);
  } else {
    // Get a frame from memory.
    spte->kpage = frame_alloc(spte->upage, PAL_USER);

    bool holding_lock = lock_held_by_current_thread(&file_lock);
    if (!holding_lock)
      lock_acquire (&file_lock);

    // Load this page.
    file_seek(spte->file_info->file, spte->file_info->ofs);
    if (file_read(spte->file_info->file, spte->kpage, spte->file_info->read_bytes) != (int) spte->file_info->read_bytes) {
      frame_free(spte->kpage);
      lock_release (&file_lock);
    }
    memset(spte->kpage + spte->file_info->read_bytes, 0, spte->file_info->zero_bytes);
    if (!holding_lock)
      lock_release (&file_lock);
  }
}

// Load page from swap disk
static void spt_load_page_into_frame_from_swap(struct spt_entry *spte) {
  ASSERT(spte->is_swapped);
  // Todo: swap_index validation check

  // Get a frame from memory.
  spte->kpage = frame_alloc(spte->upage, PAL_USER);

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
void spt_evict_page_from_frame(struct spt_entry *spte) {
  ASSERT(spte->is_loaded);
  ASSERT(spte->kpage != NULL);

  struct frame *target_frame = get_frame(spte->kpage);
  struct thread *target_holder = target_frame->thread;
  bool is_dirty = pagedir_is_dirty(target_holder->pagedir, spte->upage);

  // dirty page will be write-backed into corresponding file.
  if (is_dirty && spte->is_file) {
    spt_evict_page_from_frame_into_file(spte);
  }

  // anon page (whether dirty or not) will be swapped out when an eviction occurs.
  if (!spte->is_file) {
    spte->is_swapped = true;
    spte->swap_index = swap_out(spte->kpage);
  }

  frame_free(spte->kpage);
  spte->is_loaded = false;
  spte->kpage = NULL;
}

static void spt_evict_page_from_frame_into_file(struct spt_entry *spte) {
  ASSERT(spte->is_file)
  bool holding_lock = lock_held_by_current_thread(&file_lock);
  if (!holding_lock)
    lock_acquire(&file_lock);

  struct spt_entry_file_info* file_info = spte->file_info;
  if (file_write_at(file_info->file, spte->kpage, file_info->read_bytes, file_info->ofs) != (int) file_info->read_bytes) {
    PANIC("Evict into file failed");
  }

  if (!holding_lock)
    lock_release(&file_lock);
}