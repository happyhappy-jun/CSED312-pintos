//
// Created by Yoon Jun on 11/23/23.
//

#include "swap.h"
#include "devices/block.h"
#include "threads/vaddr.h"
#include "vm/swap.h"
#include <bitmap.h>

static const size_t SECTORS_NUM = PGSIZE / BLOCK_SECTOR_SIZE;
static struct block *swap_block;
static struct bitmap *swap_table;

void swap_init(void) {
  swap_block = block_get_role(BLOCK_SWAP);
  if (swap_block == NULL) {
    PANIC("Getting swap block failed");
  }

  swap_table = bitmap_create(block_size(swap_block) / SECTORS_NUM);
  if (swap_table == NULL) {
    PANIC("Creating swap table failed");
  }

  bitmap_set_all(swap_table, true);
}

swap_index_t swap_out(void *page) {
  size_t index = bitmap_scan_and_flip(swap_table, 0, 1, true);

  for (size_t i = 0; i < SECTORS_NUM; i++){
    block_write(swap_block, index * SECTORS_NUM + i, page + i * BLOCK_SECTOR_SIZE);
  }

  bitmap_set(swap_table, index, true);
  return index;
}

void swap_in(swap_index_t index, void *page) {
  bitmap_set(swap_table, index, false);

  for (size_t i = 0; i < SECTORS_NUM; i++){
    block_read(swap_block, index * SECTORS_NUM + i, page + i * BLOCK_SECTOR_SIZE);
  }
}

void swap_free(swap_index_t index) {
  bitmap_set(swap_table, index, false);
}
