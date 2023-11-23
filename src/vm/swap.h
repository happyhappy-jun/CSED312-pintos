//
// Created by Yoon Jun on 11/23/23.
//

#ifndef PINTOS_SWAP_H
#define PINTOS_SWAP_H

#include "stdint.h"
typedef uint32_t swap_index_t;

void swap_init(void);
swap_index_t swap_out(void *page);
void swap_in(swap_index_t index, void *page);
void swap_free(swap_index_t index);

#endif//PINTOS_SWAP_H
