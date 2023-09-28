//
// Created by Yoon Jun on 2023/09/22.
//

#ifndef COMPARE_H
#define COMPARE_H

#include <debug.h>
#include <list.h>
#include <stdbool.h>
#include <stdint.h>

bool compare_thread_priority(const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED);
#endif//COMPARE_H
