//
// Created by ±èÄ¡Çå on 2023/10/30.
//

#ifndef PINTOS_SRC_USERPROG_USER_MEMORY_ACCESS_H_
#define PINTOS_SRC_USERPROG_USER_MEMORY_ACCESS_H_

#include <stddef.h>

void *safe_memcpy_from_user(void *, const void *, size_t);
void *safe_memcpy_to_user(void *, const void *, size_t);
int safe_strcpy_from_user(char *, const char *);

#endif//PINTOS_SRC_USERPROG_USER_MEMORY_ACCESS_H_
