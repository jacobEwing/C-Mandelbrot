#include "mandelbrot.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

ReferenceOrbit ref;

RenderConfig config = {
	.palette = {
		.colourWavePeriod = 64,
		.redOffset = -0.47, .redStagger = 0, .redPeriod = 1,
		.greenOffset = 0, .greenStagger = 0, .greenPeriod = 1,
		.blueOffset = 0.7, .blueStagger = 0, .bluePeriod = 1,
		.masterOffset = 0, .masterStagger = 0
	},
	.options = {
		.calcIterations = 1, .calcDisplacement = 0, .calcRotation = 0, .calcDisplacementXOR = 0, .calcPolarCoordXOR = 0,
		.coefIterations = 1, .coefDisplacement = 1, .coefRotation = 1, .coefDisplacementXOR = 1, .coefPolarCoordXOR = 1,
		.countOffset = 0,
		.maxRadius = 4.0
	}
};

static double rel_ang(double x1, double y1, double x2, double y2) {
	double deltax = x2 - x1;
	double deltay = y2 - y1;
	double hyp = sqrt(deltax * deltax + deltay * deltay);
	double theta = 0.0;
	const double PI = 3.14159265358979323846;

	if (x2 == x1) {
		theta = (y2 > y1) ? PI : 0.0;
	} else if (y2 == y1) {
		theta = ((x2 < x1) ? 3.0 : 1.0) * PI / 2.0;
	} else if (x2 > x1) {
		theta = PI - acos(deltay / hyp);
	} else {
		theta = 2.0 * PI - acos(-deltay / hyp);
	}
	return theta;
}

uint32_t create_colour(const Result *r) {
	if (r->count >= MAX_ITER) {
		return 0xFF000000;
	}

	double countAccum = config.options.countOffset;
	double dz = r->dz;
	double dzi = r->dzi;
	double ang = rel_ang(0.0, 0.0, dz, dzi);
	double radius = sqrt(dz * dz + dzi * dzi);
	const double PI = 3.14159265358979323846;

	if (config.options.calcIterations) {
		countAccum += r->count * config.options.coefIterations;
	}
	if (config.options.calcDisplacement) {
		if (r->count > 1.0) countAccum += radius * config.options.coefDisplacement;
	}
	if (config.options.calcRotation) {
		if (r->count > 1.0) countAccum += ((double)MAX_ITER / (r->count + 1.0)) * (0.5 + sin(ang) / 2.0) * config.options.coefRotation;
	}
	if (config.options.calcDisplacementXOR) {
		if (r->count > 1.0) {
			int xa = (int)(MAX_ITER * dzi);
			int xb = (int)(MAX_ITER * dz);
			countAccum += config.options.coefDisplacementXOR * (double)(xa ^ xb) / (double)MAX_ITER;
		} else {
			int xa = (int)(MAX_ITER * r->zi);
			int xb = (int)(MAX_ITER * r->z);
			countAccum += config.options.coefDisplacementXOR * (double)(xa ^ xb) / (double)MAX_ITER;
		}
	}
	if (config.options.calcPolarCoordXOR) {
		if (r->count > 1.0) {
			int xa = (int)(MAX_ITER * ang);
			int xb = (int)(MAX_ITER * radius);
			countAccum += config.options.coefPolarCoordXOR * (double)(xa ^ xb) / (double)MAX_ITER;
		} else {
			double rad = sqrt(r->z * r->z + r->zi * r->zi);
			double theta = sin(rel_ang(0.0, 0.0, r->z, r->zi) * 2.0 + PI / 2.0) * PI;
			theta += sin(rad * 4.0) * 2.0 / rad;
			int xa = (int)(MAX_ITER * theta);
			int xb = (int)(MAX_ITER * rad);
			countAccum += config.options.coefPolarCoordXOR * (double)(xa ^ xb) / (double)MAX_ITER;
		}
	}

	ang = countAccum * 2.0 * PI / config.palette.colourWavePeriod;

	double red = 127.5 + 127.5 * sin(ang * config.palette.redPeriod + config.palette.redOffset + config.palette.masterOffset);
	double green = 127.5 + 127.5 * sin(ang * config.palette.greenPeriod + config.palette.greenOffset + config.palette.masterOffset);
	double blue = 127.5 + 127.5 * sin(ang * config.palette.bluePeriod + config.palette.blueOffset + config.palette.masterOffset);

	uint8_t r_val = (uint8_t)(red + 0.5);
	uint8_t g_val = (uint8_t)(green + 0.5);
	uint8_t b_val = (uint8_t)(blue + 0.5);

	return (255 << 24) | (r_val << 16) | (g_val << 8) | b_val;
}

df64 mpf_to_df64(mpf_t x) {
	double hi = mpf_get_d(x);
	mpf_t diff;
	mpf_init2(diff, mpf_get_prec(x));
	mpf_set_d(diff, hi);
	mpf_sub(diff, x, diff);
	double lo = mpf_get_d(diff);
	mpf_clear(diff);
	return df_quick_two_sum(hi, lo);
}

static void compute_orbit_gmp(mpf_t Cx, mpf_t Cy) {
	mpf_t Zr, Zi, Cr, Ci, tmp1, tmp2, tmp3;
	mpf_init2(Zr, 256); mpf_init2(Zi, 256);
	mpf_init2(Cr, 256); mpf_init2(Ci, 256);
	mpf_init2(tmp1, 256); mpf_init2(tmp2, 256); mpf_init2(tmp3, 256);

	mpf_set(Cr, Cx);
	mpf_set(Ci, Cy);
	mpf_set_d(Zr, 0.0); mpf_set_d(Zi, 0.0);

	ref.c_real = mpf_to_df64(Cr);
	ref.c_imag = mpf_to_df64(Ci);
	ref.length = MAX_ITER + 1;
	ref.escaped = 0;
	ref.escape_iteration = 0;
	ref.near_boundary_count = 0;
	ref.max_zmag2 = 0.0;

	ref.z_real[0] = df_from_double(0.0);
	ref.z_imag[0] = df_from_double(0.0);
	ref.zmag2[0] = df_from_double(0.0);
	ref.dz_real[0] = df_sub(df_from_double(0.0), ref.c_real);
	ref.dz_imag[0] = df_sub(df_from_double(0.0), ref.c_imag);

	for (int n = 1; n <= MAX_ITER; n++) {
		mpf_mul(tmp1, Zr, Zi);
		mpf_mul_ui(tmp1, tmp1, 2);
		mpf_add(tmp1, tmp1, Ci);

		mpf_mul(tmp2, Zr, Zr);
		mpf_mul(tmp3, Zi, Zi);
		mpf_sub(tmp2, tmp2, tmp3);
		mpf_add(tmp2, tmp2, Cr);

		mpf_set(Zr, tmp2);
		mpf_set(Zi, tmp1);

		ref.z_real[n] = mpf_to_df64(Zr);
		ref.z_imag[n] = mpf_to_df64(Zi);

		mpf_sub(tmp2, Zr, Cr);
		mpf_sub(tmp3, Zi, Ci);
		ref.dz_real[n] = mpf_to_df64(tmp2);
		ref.dz_imag[n] = mpf_to_df64(tmp3);

		mpf_mul(tmp2, Zr, Zr);
		mpf_mul(tmp3, Zi, Zi);
		mpf_add(tmp2, tmp2, tmp3);
		ref.zmag2[n] = mpf_to_df64(tmp2);

		double z2 = mpf_get_d(tmp2);
		if (z2 > ref.max_zmag2) ref.max_zmag2 = z2;
		if (z2 > config.options.maxRadius - 0.5) ref.near_boundary_count++;

		if (df_gt(ref.zmag2[n], df_from_double(config.options.maxRadius))) {
			ref.escaped = 1;
			ref.escape_iteration = n;
			ref.length = n + 1;
			break;
		}
	}

	mpf_clear(Zr); mpf_clear(Zi); mpf_clear(Cr); mpf_clear(Ci);
	mpf_clear(tmp1); mpf_clear(tmp2); mpf_clear(tmp3);
}

void compute_reference_orbit(mpf_t cx, mpf_t cy, mpf_t zoom) {
	compute_orbit_gmp(cx, cy);

	if (ref.escaped && ref.escape_iteration < MAX_ITER - 100) {
		mpf_t test_x, test_y, offset;
		mpf_init2(test_x, 256); mpf_init2(test_y, 256); mpf_init2(offset, 256);

		int best_escape = ref.escape_iteration;
		mpf_t best_cx, best_cy;
		mpf_init2(best_cx, 256); mpf_init2(best_cy, 256);
		mpf_set(best_cx, cx);
		mpf_set(best_cy, cy);

		int found = 0;
		for (int i = -1; i <= 1 && !found; i++) {
			for (int j = -1; j <= 1 && !found; j++) {
				if (i == 0 && j == 0) continue;
				mpf_div_ui(offset, zoom, 4);
				mpf_mul_ui(test_x, offset, i);
				mpf_add(test_x, test_x, cx);
				mpf_mul_ui(test_y, offset, j);
				mpf_add(test_y, test_y, cy);

				compute_orbit_gmp(test_x, test_y);
				if (!ref.escaped) {
					mpf_set(best_cx, test_x);
					mpf_set(best_cy, test_y);
					found = 1;
				} else if (ref.escape_iteration > best_escape) {
					best_escape = ref.escape_iteration;
					mpf_set(best_cx, test_x);
					mpf_set(best_cy, test_y);
				}
			}
		}

		compute_orbit_gmp(best_cx, best_cy);

		mpf_clear(test_x); mpf_clear(test_y); mpf_clear(offset);
		mpf_clear(best_cx); mpf_clear(best_cy);
	}

	df64 view_cx_df = mpf_to_df64(cx);
	df64 view_cy_df = mpf_to_df64(cy);
	ref.offset_x = df_sub(view_cx_df, ref.c_real);
	ref.offset_y = df_sub(view_cy_df, ref.c_imag);
}

Result mandelbrot_perturbation(df64 dc_r, df64 dc_i, int max_iter) {
	df64 dr = df_from_double(0.0);
	df64 di = df_from_double(0.0);
	df64 maxr = df_from_double(config.options.maxRadius);
	int n = 0;

	while (n < max_iter) {
		if (n >= ref.length) {
			df64 cr = df_add(ref.c_real, dc_r);
			df64 ci = df_add(ref.c_imag, dc_i);
			df64 zr = df_add(ref.z_real[n - 1], dr);
			df64 zi = df_add(ref.z_imag[n - 1], di);
			for (; n < max_iter; n++) {
				df64 zr2 = df_mul(zr, zr);
				df64 zi2 = df_mul(zi, zi);
				if (df_gt(df_add(zr2, zi2), maxr)) break;
				zi = df_add(df_mul_d(df_mul(zr, zi), 2.0), ci);
				zr = df_add(df_sub(zr2, zi2), cr);
			}
			Result res = {
				n,
				df_to_double(zr),
				df_to_double(zi),
				df_to_double(df_sub(zr, cr)),
				df_to_double(df_sub(zi, ci))
			};
			return res;
		}

		df64 Zr = ref.z_real[n];
		df64 Zi = ref.z_imag[n];

		df64 Zr_dr = df_mul(Zr, dr);
		df64 Zi_di = df_mul(Zi, di);
		df64 cross = df_mul_d(df_add(Zr_dr, Zi_di), 2.0);

		df64 dr2 = df_mul(dr, dr);
		df64 di2 = df_mul(di, di);
		df64 dmag2 = df_add(dr2, di2);

		df64 zmag2 = df_add(df_add(ref.zmag2[n], cross), dmag2);

		if (df_gt(zmag2, maxr)) {
			df64 zr = df_add(Zr, dr);
			df64 zi = df_add(Zi, di);
			df64 dz_r = df_sub(df_add(ref.dz_real[n], dr), dc_r);
			df64 dz_i = df_sub(df_add(ref.dz_imag[n], di), dc_i);
			Result res = {
				n,
				df_to_double(zr),
				df_to_double(zi),
				df_to_double(dz_r),
				df_to_double(dz_i)
			};
			return res;
		}

		if (df_to_double(dmag2) > 1e-4) {
			df64 cr = df_add(ref.c_real, dc_r);
			df64 ci = df_add(ref.c_imag, dc_i);
			df64 zr_g = df_add(Zr, dr);
			df64 zi_g = df_add(Zi, di);
			for (; n < max_iter; n++) {
				df64 zr2_g = df_mul(zr_g, zr_g);
				df64 zi2_g = df_mul(zi_g, zi_g);
				if (df_gt(df_add(zr2_g, zi2_g), maxr)) break;
				zi_g = df_add(df_mul_d(df_mul(zr_g, zi_g), 2.0), ci);
				zr_g = df_add(df_sub(zr2_g, zi2_g), cr);
			}
			Result res = {
				n,
				df_to_double(zr_g),
				df_to_double(zi_g),
				df_to_double(df_sub(zr_g, cr)),
				df_to_double(df_sub(zi_g, ci))
			};
			return res;
		}

		df64 ndr_a = df_mul_d(df_sub(Zr_dr, Zi_di), 2.0);
		df64 ndr_b = df_sub(dr2, di2);
		df64 new_dr = df_add(df_add(ndr_a, ndr_b), dc_r);

		df64 Zr_di = df_mul(Zr, di);
		df64 Zi_dr = df_mul(Zi, dr);
		df64 ndi_a = df_mul_d(df_add(Zr_di, Zi_dr), 2.0);
		df64 ndi_b = df_mul_d(df_mul(dr, di), 2.0);
		df64 new_di = df_add(df_add(ndi_a, ndi_b), dc_i);

		dr = new_dr;
		di = new_di;
		n++;
	}
	Result res = {max_iter, 0, 0, 0, 0};
	return res;
}

Result mandelbrot_calc(double c, double ci, int max_iter) {
	double zr = 0.0, zi = 0.0;
	int i;
	for (i = 0; i < max_iter; i++) {
		double zr2 = zr * zr;
		double zi2 = zi * zi;
		if (zr2 + zi2 > config.options.maxRadius) break;
		zi = 2.0 * zr * zi + ci;
		zr = zr2 - zi2 + c;
	}
	Result res;
	res.count = i;
	res.z = zr;
	res.zi = zi;
	res.dz = zr - c;
	res.dzi = zi - ci;
	return res;
}
