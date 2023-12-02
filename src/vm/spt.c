//
// Created by 김치헌 on 2023/11/30.
//

#include "vm/spt.h"
#include "frame.h"
#include "page.h"
#include "stdio.h"
#include "swap.h"
#include "threads/malloc.h"
#include "userprog/process.h"

static unsigned spt_hash(const struct hash_elem *elem, void *aux);
static bool spt_less(const struct hash_elem *a, const struct hash_elem *b, void *aux);
static void spte_destroy(struct hash_elem *elem, void *aux);
static struct file_info *file_info_generator(struct file *file, off_t offset, uint32_t read_bytes, uint32_t zero_bytes);

void spt_init(struct spt *spt) {
  spt->pagedir = thread_current()->pagedir;
  lock_init(&spt->spt_lock);
  hash_init(&spt->table, spt_hash, spt_less, &spt->spt_lock);
}

static unsigned spt_hash(const struct hash_elem *elem, void *aux) {
  struct spt_entry *spte = hash_entry(elem, struct spt_entry, elem);
  return hash_bytes(&spte->upage, sizeof(spte->upage));
}

static bool spt_less(const struct hash_elem *a, const struct hash_elem *b, void *aux) {
  struct spt_entry *spte_a = hash_entry(a, struct spt_entry, elem);
  struct spt_entry *spte_b = hash_entry(b, struct spt_entry, elem);
  return spte_a->upage < spte_b->upage;
}

void spt_destroy(struct spt *spt) {
  lock_acquire(&spt->spt_lock);
  hash_destroy(&spt->table, spte_destroy);
  lock_release(&spt->spt_lock);
}

static void spte_destroy(struct hash_elem *elem, void *aux) {
  ASSERT(lock_held_by_current_thread((const struct lock *)aux))
  struct spt_entry *spte = hash_entry(elem, struct spt_entry, elem);
  if (spte->location == LOADED) {
    if (!frame_test_and_pin(spte->kpage)) {
      while (frame_pinned(spte->kpage));
    } else {
      frame_pin(spte->kpage);
      unload_page(&thread_current()->spt, spte);
    }
  }
  if (spte->location == SWAP) {
    swap_free(spte->swap_index);
  }
  if (spte->type == EXEC || spte->type == MMAP) {
    free(spte->file_info);
  }
  free(spte);
}

struct spt_entry *spt_find(struct spt *spt, void *upage) {
  struct spt_entry finder;
  finder.upage = upage;
  bool hold = lock_held_by_current_thread(&spt->spt_lock);
  if (!hold)
    lock_acquire(&spt->spt_lock);
  struct hash_elem *e = hash_find(&spt->table, &finder.elem);
  if (!hold)
    lock_release(&spt->spt_lock);
  if (e == NULL) {
      return NULL;
  }
  return hash_entry(e, struct spt_entry, elem);
}

static struct file_info *file_info_generator(struct file *file, off_t offset, uint32_t read_bytes, uint32_t zero_bytes) {
  struct file_info *file_info = malloc(sizeof(struct file_info));
  file_info->file = file;
  file_info->offset = offset;
  file_info->read_bytes = read_bytes;
  file_info->zero_bytes = zero_bytes;
  return file_info;
}


struct spt_entry *spt_insert_mmap(struct spt *spt, void *upage, struct file *file, off_t offset, uint32_t read_bytes, uint32_t zero_bytes) {
  struct spt_entry *spte = malloc(sizeof(struct spt_entry));
  if (spte == NULL) {
    return NULL;
  }
  spte->upage = upage;
  spte->kpage = NULL;
  spte->writable = true;
  spte->dirty = false;
  spte->type = MMAP;
  spte->location = FILE;
  spte->file_info = file_info_generator(file, offset, read_bytes, zero_bytes);
  spte->swap_index = -1;
  lock_acquire(&spt->spt_lock);
  struct hash_elem *e = hash_insert(&spt->table, &spte->elem);
  lock_release(&spt->spt_lock);
  if (e == NULL) {
    return spte;
  } else {
    free(spte);
    return NULL;
  }
}

struct spt_entry *spt_insert_exec(struct spt *spt, void *upage, bool writable, struct file *file, off_t offset, uint32_t read_bytes, uint32_t zero_bytes) {
  struct spt_entry *spte = malloc(sizeof(struct spt_entry));
  if (spte == NULL) {
    return NULL;
  }
  spte->upage = upage;
  spte->kpage = NULL;
  spte->writable = writable;
  spte->dirty = false;
  spte->type = EXEC;
  spte->location = FILE;
  spte->file_info = file_info_generator(file, offset, read_bytes, zero_bytes);
  spte->swap_index = -1;
  lock_acquire(&spt->spt_lock);
  struct hash_elem *e = hash_insert(&spt->table, &spte->elem);
  lock_release(&spt->spt_lock);
  if (e == NULL) {
    return spte;
  } else {
    free(spte);
    return NULL;
  }
}

struct spt_entry *spt_insert_stack(struct spt *spt, void *upage) {
  struct spt_entry *spte = malloc(sizeof(struct spt_entry));
  if (spte == NULL) {
    return NULL;
  }
  spte->upage = upage;
  spte->kpage = NULL;
  spte->writable = true;
  spte->dirty = false;
  spte->type = STACK;
  spte->location = ZERO;
  spte->file_info = NULL;
  spte->swap_index = -1;
  lock_acquire(&spt->spt_lock);
  struct hash_elem *e = hash_insert(&spt->table, &spte->elem);
  lock_release(&spt->spt_lock);
  if (e == NULL) {
      return spte;
  } else {
      free(spte);
      return NULL;
  }
}

void spt_remove(struct spt *spt, void *upage) {
  struct spt_entry *spte = spt_find(spt, upage);
  lock_acquire(&spt->spt_lock);
  hash_delete(&spt->table, &spte->elem);
  spte_destroy(&spte->elem, &spt->spt_lock);
  lock_release(&spt->spt_lock);
}
