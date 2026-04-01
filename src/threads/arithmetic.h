/*
 * Unfortunately, Pintos does not support floating-point arithmetic in the kernel, 
 * because it would complicate and slow the kernel. 
 * This means that calculations on real quantities must be simulated using integers.
 * The fundamental idea is to treat the rightmost bits of an integer as representing a fraction.
 */


#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <stdint.h>

/* fractional bits: q=14 */
#define FP_Q 14
#define FP_F (1 << FP_Q)

/* 类型定义，方便阅读 (虽然本质上就是 int) */
typedef int fp_t;

/* --- 转换操作 --- */

/* Convert n to fixed point: n * f */
#define CONVERT_TO_FP(n) ((n) * (FP_F))

/* Convert x to integer (rounding toward zero): x / f */
#define CONVERT_TO_INT_ZERO(x) ((x) / (FP_F))

/* Convert x to integer (rounding to nearest): 
   x >= 0 ? (x + f/2) / f : (x - f/2) / f */
#define CONVERT_TO_INT_NEAR(x) ((x) >= 0 ? \
    (((x) + (FP_F) / 2) / (FP_F)) : \
    (((x) - (FP_F) / 2) / (FP_F)))


/* --- 算术操作 --- */

/* Add x and y */
#define ADD_FP(x, y) ((x) + (y))

/* Subtract y from x */
#define SUB_FP(x, y) ((x) - (y))

/* Add x and integer n: x + n * f */
#define ADD_FP_INT(x, n) ((x) + (n) * (FP_F))

/* Subtract integer n from x: x - n * f */
#define SUB_FP_INT(x, n) ((x) - (n) * (FP_F))

/* Multiply x by y: ((int64_t) x) * y / f 
   注意：必须先转换为 64 位，否则 32 位乘法会溢出 */
#define MUL_FP(x, y) ((fp_t) (((int64_t) (x)) * (y) / (FP_F)))

/* Multiply x by n: x * n */
#define MUL_FP_INT(x, n) ((x) * (n))

/* Divide x by y: ((int64_t) x) * f / y 
   注意：必须先转换为 64 位，否则 32 位乘法会溢出 */
#define DIV_FP(x, y) ((fp_t) (((int64_t) (x)) * (FP_F) / (y)))

/* Divide x by n: x / n */
#define DIV_FP_INT(x, n) ((x) / (n))

#endif /* threads/arithmetic.h */

