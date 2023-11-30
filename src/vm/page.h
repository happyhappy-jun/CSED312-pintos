//
// Created by 김치헌 on 2023/11/30.
//

#ifndef PINTOS_SRC_VM_PAGE_H_
#define PINTOS_SRC_VM_PAGE_H_

#include "vm/spt.h"
#include "stdbool.h"

bool load_page(struct spt *spt, void *upage);
bool unload_page(struct spt *spt, void *upage);

#endif//PINTOS_SRC_VM_PAGE_H_
