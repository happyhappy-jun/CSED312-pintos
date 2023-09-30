//
// Created by ±èÄ¡Çå on 2023/09/30.
//

#ifndef PINTOS_SRC_THREADS_FIXED_H_
#define PINTOS_SRC_THREADS_FIXED_H_

typedef int fixed_t;

fixed_t int2fp(int n);
int fp2int(fixed_t x);
int fp2int_round(fixed_t x);
fixed_t add_y(fixed_t x, fixed_t y);
fixed_t add_n(fixed_t x, int n);
fixed_t sub_y(fixed_t x, fixed_t y);
fixed_t sub_n(fixed_t x, int n);
fixed_t mul_y(fixed_t x, fixed_t y);
fixed_t mul_n(fixed_t x, int n);
fixed_t div_y(fixed_t x, fixed_t y);
fixed_t div_n(fixed_t x, int n);

#endif//PINTOS_SRC_THREADS_FIXED_H_
