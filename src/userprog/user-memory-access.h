//
// Created by ��ġ�� on 2023/10/30.
//

#ifndef PINTOS_SRC_USERPROG_USER_MEMORY_ACCESS_H_
#define PINTOS_SRC_USERPROG_USER_MEMORY_ACCESS_H_

#include <stdbool.h>
#include <stddef.h>
#define PHYS_BASE ((void *) 0xc0000000)
#define STACK_BOTTOM ((void *) 0x0048000)

bool validate_uaddr(const void *);
void *safe_memcpy_from_user(void *, const void *, size_t);
void *safe_memcpy_to_user(void *, const void *, size_t);
int safe_strcpy_from_user(char *, const char *);

#endif//PINTOS_SRC_USERPROG_USER_MEMORY_ACCESS_H_
