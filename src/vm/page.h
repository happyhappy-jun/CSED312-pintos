//
// Created by 김치헌 on 2023/11/30.
//

#ifndef PINTOS_SRC_VM_PAGE_H_
#define PINTOS_SRC_VM_PAGE_H_

#include "vm/spt.h"
#include "stdbool.h"

bool load_page(struct spt *spt, void *upage);
bool unload_page(struct spt *spt, struct spt_entry *spte);
bool load_page_data(void *kpage, struct spt *spt, void *upage);
bool unload_page_data(struct spt *spt, struct spt_entry *spte);


#endif//PINTOS_SRC_VM_PAGE_H_
