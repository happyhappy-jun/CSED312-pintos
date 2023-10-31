//
// Created by ±èÄ¡Çå on 2023/10/30.
//

#include "userprog/user-memory-access.h"
#include "threads/vaddr.h"
#include <stdbool.h>
#include <stdint.h>

// Note
// get_user() and put_user() are from Pintos official reference,
// 3.1.5 Accessing User Memory

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int get_user(const uint8_t *uaddr) {
  int result;
  asm("movl $1f, %0; movzbl %1, %0; 1:"
      : "=&a"(result)
      : "m"(*uaddr));
  return result;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool put_user(uint8_t *udst, uint8_t byte) {
  int error_code;
  asm("movl $1f, %0; movb %b2, %1; 1:"
      : "=&a"(error_code), "=m"(*udst)
      : "q"(byte));
  return error_code != -1;
}

static bool validate_uaddr(const void *uaddr) {
  return uaddr < PHYS_BASE;
}

void *safe_memcpy_from_user(void *kdst, const void *usrc, size_t n) {
  uint8_t *dst = kdst;
  const uint8_t *src = usrc;
  int byte;

  ASSERT(kdst != NULL)

  if (!validate_uaddr(usrc) || !validate_uaddr(usrc + n - 1))
    return NULL;

  for (size_t i = 0; i < n; i++) {
    byte = get_user(src + i);
    if (byte == -1)
      return NULL;
    dst[i] = byte;
  }
  return kdst;
}

void *safe_memcpy_to_user(void *udst, const void *ksrc, size_t n) {
  uint8_t *dst = udst;
  const uint8_t *src = ksrc;
  int byte;

  if (!validate_uaddr(udst) || !validate_uaddr(udst + n - 1))
    return NULL;

  for (size_t i = 0; i < n; i++) {
    byte = src[i];
    if (!put_user(dst + i, byte))
      return NULL;
  }
  return udst;
}

int safe_strcpy_from_user(char *kdst, const char *usrc) {
  int byte;

  ASSERT(kdst != NULL)

  for (int i = 0;; i++) {
    if (!validate_uaddr(usrc + i))
      return -1;

    byte = get_user((const unsigned char *) usrc + i);
    if (byte == -1)
      return -1;

    kdst[i] = (char) byte;

    if (byte == '\0')
      return i;
  }
}