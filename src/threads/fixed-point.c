//
// Created by ChiHeon Kim on 2023/09/30.
//

#include "threads/fixed-point.h"
#include <inttypes.h>

#define P 17
#define Q 14
#define F 1 << Q

fixed_t int2fp(int n) {
  return n * F;
}

int fp2int(fixed_t x) {
  return x / F;
}

int fp2int_round(fixed_t x) {
  if (x >= 0) {
    return (x + (F >> 1)) / F;
  } else {
    return (x - (F >> 1)) / F;
  }
}

fixed_t fp_add_y(fixed_t x, fixed_t y) {
  return x + y;
}

fixed_t fp_add_n(fixed_t x, int n) {
  return x + n * F;
}

fixed_t fp_sub_y(fixed_t x, fixed_t y) {
  return x - y;
}

fixed_t fp_sub_n(fixed_t x, int n) {
  return x - n * F;
}

fixed_t fp_mul_y(fixed_t x, fixed_t y) {
  return ((int64_t) x) * y / F;
}

fixed_t fp_mul_n(fixed_t x, int n) {
  return x * n;
}

fixed_t fp_div_y(fixed_t x, fixed_t y) {
  return ((int64_t) x) * F / y;
}

fixed_t fp_div_n(fixed_t x, int n) {
  return x / n;
}
