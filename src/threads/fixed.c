//
// Created by ChiHeon Kim on 2023/09/30.
//

#include "threads/fixed.h"
#include <inttypes.h>

int P = 17;
int Q = 14;
int F = 1 << Q;

fixed_t int2fp(int n) {
  return n * F;
}

int fp2int(fixed_t x) {
  return x / F;
}

int fp2int_round(fixed_t x) {
  if (x > 0) {
    return (x + F / 2) / F;
  } else {
    return (x - F / 2) / F;
  }
}

fixed_t add_y(fixed_t x, fixed_t y) {
  return x + y;
}

fixed_t add_n(fixed_t x, int n) {
  return x + n * F;
}

fixed_t sub_y(fixed_t x, fixed_t y) {
  return x - y;
}

fixed_t sub_n(fixed_t x, int n) {
  return x - n * F;
}

fixed_t mul_y(fixed_t x, fixed_t y) {
  return ((int64_t) x) * y / F;
}

fixed_t mul_n(fixed_t x, int n) {
  return x * n;
}

fixed_t div_y(fixed_t x, fixed_t y) {
  return ((int64_t) x) * F / y;
}

fixed_t div_n(fixed_t x, int n) {
  return x / n;
}
