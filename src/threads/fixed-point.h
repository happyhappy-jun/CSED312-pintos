//
// Created by ChiHeon Kim on 2023/09/30.
//

#ifndef PINTOS_SRC_THREADS_FIXED_H_
#define PINTOS_SRC_THREADS_FIXED_H_

typedef int fixed_t;

fixed_t int2fp(int n);
int fp2int(fixed_t x);
int fp2int_round(fixed_t x);
fixed_t fp_add_y(fixed_t x, fixed_t y);
fixed_t fp_add_n(fixed_t x, int n);
fixed_t fp_sub_y(fixed_t x, fixed_t y);
fixed_t fp_sub_n(fixed_t x, int n);
fixed_t fp_mul_y(fixed_t x, fixed_t y);
fixed_t fp_mul_n(fixed_t x, int n);
fixed_t fp_div_y(fixed_t x, fixed_t y);
fixed_t fp_div_n(fixed_t x, int n);

#endif//PINTOS_SRC_THREADS_FIXED_H_
