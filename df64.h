#ifndef DF64_H
#define DF64_H

#include <math.h>

typedef struct {
	double hi, lo;
} df64;

static inline df64 df_from_double(double x) {
	df64 r = { x, 0.0 };
	return r;
}

static inline double df_to_double(df64 a) {
	return a.hi + a.lo;
}

static inline df64 df_quick_two_sum(double a, double b) {
	double s = a + b;
	double e = b - (s - a);
	df64 r = { s, e };
	return r;
}

static inline df64 df_two_sum(double a, double b) {
	double s = a + b;
	double bb = s - a;
	double e = (a - (s - bb)) + (b - bb);
	df64 r = { s, e };
	return r;
}

#ifdef __FMA__
static inline df64 df_two_prod(double a, double b) {
	double p = a * b;
	double e = fma(a, b, -p);
	df64 r = { p, e };
	return r;
}
#else
static inline df64 df_two_prod(double a, double b) {
	double p = a * b;
	const double SPLIT = 134217729.0;
	double ca = SPLIT * a;
	double a_hi = ca - (ca - a);
	double a_lo = a - a_hi;
	double cb = SPLIT * b;
	double b_hi = cb - (cb - b);
	double b_lo = b - b_hi;
	double e = ((a_hi * b_hi - p) + a_hi * b_lo + a_lo * b_hi) + a_lo * b_lo;
	df64 r = { p, e };
	return r;
}
#endif

static inline df64 df_add(df64 a, df64 b) {
	df64 s = df_two_sum(a.hi, b.hi);
	s.lo += a.lo + b.lo;
	return df_quick_two_sum(s.hi, s.lo);
}

static inline df64 df_sub(df64 a, df64 b) {
	df64 nb = { -b.hi, -b.lo };
	return df_add(a, nb);
}

static inline df64 df_mul(df64 a, df64 b) {
	df64 p = df_two_prod(a.hi, b.hi);
	p.lo += a.hi * b.lo + a.lo * b.hi;
	return df_quick_two_sum(p.hi, p.lo);
}

static inline df64 df_mul_d(df64 a, double b) {
	df64 p = df_two_prod(a.hi, b);
	p.lo += a.lo * b;
	return df_quick_two_sum(p.hi, p.lo);
}

static inline int df_gt(df64 a, df64 b) {
	if (a.hi > b.hi) return 1;
	if (a.hi < b.hi) return 0;
	return a.lo > b.lo;
}

static inline int df_lt(df64 a, df64 b) {
	if (a.hi < b.hi) return 1;
	if (a.hi > b.hi) return 0;
	return a.lo < b.lo;
}

static inline int df_eq(df64 a, df64 b) {
	return a.hi == b.hi && a.lo == b.lo;
}

static inline df64 df_neg(df64 a) {
	df64 r = { -a.hi, -a.lo };
	return r;
}

static inline df64 df_abs(df64 a) {
	if (a.hi < 0.0) return df_neg(a);
	if (a.hi > 0.0) return a;
	df64 r = { fabs(a.lo), 0.0 };
	return r;
}

#endif
