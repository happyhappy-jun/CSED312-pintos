//
// Created by 김치헌 on 2023/11/23.
//

#include "spt.h"
#include "string.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "filesys/file.h"

static struct spt_entry *spt_add(struct spt *, struct spt_entry *);
static void spt_remove_helper(struct hash_elem *, void * UNUSED);
static struct hash_elem *spt_get_hash_elem(struct spt *, void *);
static struct spt_entry *spt_make_clean_spt_entry(void *, bool, bool);
static void spt_load_page_into_frame_from_file(struct spt_entry *);
static void spt_load_page_into_frame_from_swap(struct spt_entry *);

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
static struct spt_entry *spt_add(struct spt* spt, struct spt_entry* spte) {
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
  // Todo: swap_index initial value (SWAP_ERROR?)
  spte->swap_index = -1;
  return spte;
}

// Add file-backed spt_entry
struct spt_entry *spt_add_file(struct spt *spt, void *upage, bool writable, struct file *file, off_t ofs, uint32_t read_bytes, uint32_t zero_bytes) {
  struct spt_entry* spte = spt_make_clean_spt_entry(upage, writable, true);
  struct spt_entry_file_info *file_info = malloc(sizeof(struct spt_entry_file_info));
  file_info->file = file;
  file_info->ofs = ofs;
  file_info->read_bytes = read_bytes;
  file_info->zero_bytes = zero_bytes;
  spte->file_info = file_info;
  return spt_add(spt, spte);
}

// Add non file-backed spt_entry
struct spt_entry *spt_add_anon(struct spt* spt, void *upage, bool writable) {
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
    // Todo: free corresponding swap table entry
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

    // Load this page.
    if (file_read(spte->file_info->file, spte->kpage, spte->file_info->read_bytes) != (int) spte->file_info->read_bytes) {
      PANIC("file_read failed");
    }
    memset(spte->kpage + spte->file_info->read_bytes, 0, spte->file_info->zero_bytes);
  }
}

// Load page from swap disk
static void spt_load_page_into_frame_from_swap(struct spt_entry *spte) {
  ASSERT(spte->is_swapped);
  // Todo: swap_index validation check

  // Get a frame from memory.
  spte->kpage = frame_alloc(spte->upage, PAL_USER);

  // Load this page.
  // Todo: swap in from swap disk.
  spte->is_swapped = false;
  // vm_swap_in(spte->swap_index, spte->kpage); // maybe?
  spte->swap_index = -1;
}


/* Evict spte corresponding frame
 *
 * Dirty File-backed Page and Anon Page will be swapped out
 * Otherwise, Page data in the frame will just be freed
 *
 * Set is_swapped, swap_index if swapped out
 * Clear is_loaded, kpage */
void spt_evict_page_from_frame(struct spt_entry *spte) {
  ASSERT(spte->is_loaded);
  ASSERT(spte->kpage != NULL);

  bool is_dirty = pagedir_is_dirty(thread_current()->pagedir, spte->upage);

  // 1. dirty file
  // 2. anon page (whether dirty or not)
  // will be swapped out when an eviction occurs.

  if (is_dirty || !spte->is_file) {
    spte->is_swapped = true;
    // Todo: swap out into swap disk.
    // spte->swap_index = vm_swap_out(spte->swap_index, spte->kpage); // maybe?
  }

  frame_free(spte->kpage);
  spte->is_loaded = false;
  spte->kpage = NULL;
}