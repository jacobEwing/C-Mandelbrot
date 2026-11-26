#include "mandelbrot.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

ReferenceOrbit ref = {0};

static const RenderConfig default_config = {
	.palette = {
		.colourWavePeriod = 64,
		.redOffset = -0.47, .redStagger = 0, .redPeriod = 1,
		.greenOffset = 0, .greenStagger = 0, .greenPeriod = 1,
		.blueOffset = 0.7, .blueStagger = 0, .bluePeriod = 1,
		.masterOffset = 0, .masterStagger = 0,
		.staggerMaskRed = 2, .staggerMaskRedLen = 2,
		.staggerMaskGreen = 2, .staggerMaskGreenLen = 2,
		.staggerMaskBlue = 2, .staggerMaskBlueLen = 2,
		.staggerBefore = 1, .staggerAfter = 0
	},
	.options = {
		.max_iter = 1024,
		.calcIterations = 1, .calcDisplacement = 0, .calcRotation = 0, .calcDisplacementXOR = 0, .calcPolarCoordXOR = 0,
		.coefIterations = 1, .coefDisplacement = 1, .coefRotation = 1, .coefDisplacementXOR = 1, .coefPolarCoordXOR = 1,
		.countOffset = 0,
		.maxRadius = 4.0,
		.endCondition = 0,
		.recursionDepth = 1
	},
	.viewRotation = 0.0
};

RenderConfig config;

void reset_config(void) {
	config = default_config;
}

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
	if (r->count >= config.options.max_iter * config.options.recursionDepth) {
		return 0xFF000000;
	}

	double countAccum = config.options.countOffset;
	double dz = r->dz;
	double dzi = r->dzi;
	double ang = rel_ang(0.0, 0.0, dz, dzi);
	double radius = sqrt(dz * dz + dzi * dzi);
	const double PI = 3.14159265358979323846;

	double staggerR = 0.0, staggerG = 0.0, staggerB = 0.0;

	if (config.palette.staggerBefore) {
		staggerR = ((1 << ((int)fabs(r->count) % config.palette.staggerMaskRedLen)) & config.palette.staggerMaskRed) ? 1.0 : 0.0;
		staggerG = ((1 << ((int)fabs(r->count) % config.palette.staggerMaskGreenLen)) & config.palette.staggerMaskGreen) ? 1.0 : 0.0;
		staggerB = ((1 << ((int)fabs(r->count) % config.palette.staggerMaskBlueLen)) & config.palette.staggerMaskBlue) ? 1.0 : 0.0;
	}

	if (config.options.calcIterations) {
		countAccum += r->count * config.options.coefIterations;
	}
	if (config.options.calcDisplacement) {
		if (r->count > 1.0) countAccum += radius * config.options.coefDisplacement;
	}
	if (config.options.calcRotation) {
		if (r->count > 1.0) countAccum += ((double)config.options.max_iter / (r->count + 1.0)) * (0.5 + sin(ang) / 2.0) * config.options.coefRotation;
	}

	if (config.palette.staggerAfter) {
		staggerR += ((1 << ((int)fabs(countAccum) % config.palette.staggerMaskRedLen)) & config.palette.staggerMaskRed) ? 1.0 : 0.0;
		staggerG += ((1 << ((int)fabs(countAccum) % config.palette.staggerMaskGreenLen)) & config.palette.staggerMaskGreen) ? 1.0 : 0.0;
		staggerB += ((1 << ((int)fabs(countAccum) % config.palette.staggerMaskBlueLen)) & config.palette.staggerMaskBlue) ? 1.0 : 0.0;
	}

	if (config.options.calcDisplacementXOR) {
		if (r->count > 1.0) {
			int xa = (int)(config.options.max_iter * dzi);
			int xb = (int)(config.options.max_iter * dz);
			countAccum += config.options.coefDisplacementXOR * (double)(xa ^ xb) / (double)config.options.max_iter;
		} else {
			int xa = (int)(config.options.max_iter * r->zi);
			int xb = (int)(config.options.max_iter * r->z);
			countAccum += config.options.coefDisplacementXOR * (double)(xa ^ xb) / (double)config.options.max_iter;
		}
	}
	if (config.options.calcPolarCoordXOR) {
		if (r->count > 1.0) {
			int xa = (int)(config.options.max_iter * ang);
			int xb = (int)(config.options.max_iter * radius);
			countAccum += config.options.coefPolarCoordXOR * (double)(xa ^ xb) / (double)config.options.max_iter;
		} else {
			double rad = sqrt(r->z * r->z + r->zi * r->zi);
			double theta = sin(rel_ang(0.0, 0.0, r->z, r->zi) * 2.0 + PI / 2.0) * PI;
			theta += sin(rad * 4.0) * 2.0 / rad;
			int xa = (int)(config.options.max_iter * theta);
			int xb = (int)(config.options.max_iter * rad);
			countAccum += config.options.coefPolarCoordXOR * (double)(xa ^ xb) / (double)config.options.max_iter;
		}
	}

	ang = countAccum * 2.0 * PI / config.palette.colourWavePeriod;

	double red = 127.5 + 127.5 * sin(ang * config.palette.redPeriod + config.palette.redOffset + config.palette.masterOffset + (config.palette.redStagger + config.palette.masterStagger) * staggerR);
	double green = 127.5 + 127.5 * sin(ang * config.palette.greenPeriod + config.palette.greenOffset + config.palette.masterOffset + (config.palette.greenStagger + config.palette.masterStagger) * staggerG);
	double blue = 127.5 + 127.5 * sin(ang * config.palette.bluePeriod + config.palette.blueOffset + config.palette.masterOffset + (config.palette.blueStagger + config.palette.masterStagger) * staggerB);

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
	ref.length = config.options.max_iter + 1;
	ref.escaped = 0;
	ref.escape_iteration = 0;
	ref.near_boundary_count = 0;
	ref.max_zmag2 = 0.0;

	ref.z_real[0] = df_from_double(0.0);
	ref.z_imag[0] = df_from_double(0.0);
	ref.zmag2[0] = df_from_double(0.0);
	ref.dz_real[0] = df_sub(df_from_double(0.0), ref.c_real);
	ref.dz_imag[0] = df_sub(df_from_double(0.0), ref.c_imag);

	for (int n = 1; n <= config.options.max_iter; n++) {
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

		/* zmag2 (used by perturbation's fast path when endCondition=0) */
		mpf_mul(tmp2, Zr, Zr);
		mpf_mul(tmp3, Zi, Zi);
		mpf_add(tmp2, tmp2, tmp3);
		ref.zmag2[n] = mpf_to_df64(tmp2);
		double z2 = mpf_get_d(tmp2);
		if (z2 > ref.max_zmag2) ref.max_zmag2 = z2;
		if (z2 > config.options.maxRadius - 0.5) ref.near_boundary_count++;

		/* escape check matching the pixel's end condition */
		int escaped = 0;
		switch (config.options.endCondition) {
			case 1: {
				mpf_t zr2, zi2;
				mpf_init2(zr2, 256); mpf_init2(zi2, 256);
				mpf_mul(zr2, Zr, Zr);
				mpf_mul(zi2, Zi, Zi);
				mpf_mul(zr2, zr2, zi2);
				escaped = mpf_cmp_d(zr2, config.options.maxRadius) > 0;
				mpf_clear(zr2); mpf_clear(zi2);
				break;
			}
			case 2: {
				mpf_t zr2, zi2, d;
				mpf_init2(zr2, 256); mpf_init2(zi2, 256); mpf_init2(d, 256);
				mpf_mul(zr2, Zr, Zr);
				mpf_mul(zi2, Zi, Zi);
				mpf_sub(d, zr2, zi2);
				mpf_abs(d, d);
				escaped = mpf_cmp_d(d, config.options.maxRadius) > 0;
				mpf_clear(zr2); mpf_clear(zi2); mpf_clear(d);
				break;
			}
			case 3: {
				mpf_t azr, azi, s;
				mpf_init2(azr, 256); mpf_init2(azi, 256); mpf_init2(s, 256);
				mpf_abs(azr, Zr);
				mpf_abs(azi, Zi);
				mpf_add(s, azr, azi);
				escaped = mpf_cmp_d(s, config.options.maxRadius) > 0;
				mpf_clear(azr); mpf_clear(azi); mpf_clear(s);
				break;
			}
			case 4: {
				mpf_t azr, azi, d;
				mpf_init2(azr, 256); mpf_init2(azi, 256); mpf_init2(d, 256);
				mpf_abs(azr, Zr);
				mpf_abs(azi, Zi);
				mpf_sub(d, azr, azi);
				mpf_abs(d, d);
				escaped = mpf_cmp_d(d, config.options.maxRadius) > 0;
				mpf_clear(azr); mpf_clear(azi); mpf_clear(d);
				break;
			}
			default:
				escaped = mpf_cmp_d(tmp2, config.options.maxRadius) > 0;
				break;
		}

		if (escaped) {
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
	int req_len = config.options.max_iter + 1;
	if (ref.capacity < req_len) {
		ref.z_real = (df64*)realloc(ref.z_real, req_len * sizeof(df64));
		ref.z_imag = (df64*)realloc(ref.z_imag, req_len * sizeof(df64));
		ref.zmag2  = (df64*)realloc(ref.zmag2,  req_len * sizeof(df64));
		ref.dz_real = (df64*)realloc(ref.dz_real, req_len * sizeof(df64));
		ref.dz_imag = (df64*)realloc(ref.dz_imag, req_len * sizeof(df64));
		ref.capacity = req_len;
	}

	mpf_t ref_cx, ref_cy;
	mpf_init2(ref_cx, 256);
	mpf_init2(ref_cy, 256);
	mpf_set(ref_cx, cx);
	mpf_set(ref_cy, cy);

	compute_orbit_gmp(ref_cx, ref_cy);

	if (ref.escaped && ref.escape_iteration < config.options.max_iter - 100) {
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
		mpf_set(ref_cx, best_cx);
		mpf_set(ref_cy, best_cy);

		mpf_clear(test_x); mpf_clear(test_y); mpf_clear(offset);
		mpf_clear(best_cx); mpf_clear(best_cy);
	}

	/* Compute offset in full mpf precision, then convert to df64.
	   Doing df64(cx) - df64(ref_c) directly loses precision below zoom ~1e-32. */
	mpf_t off_x_mpf, off_y_mpf;
	mpf_init2(off_x_mpf, 256);
	mpf_init2(off_y_mpf, 256);
	mpf_sub(off_x_mpf, cx, ref_cx);
	mpf_sub(off_y_mpf, cy, ref_cy);
	ref.offset_x = mpf_to_df64(off_x_mpf);
	ref.offset_y = mpf_to_df64(off_y_mpf);
	mpf_clear(off_x_mpf);
	mpf_clear(off_y_mpf);
	mpf_clear(ref_cx);
	mpf_clear(ref_cy);
}

Result mandelbrot_perturbation(df64 dc_r, df64 dc_i, int max_iter) {
	df64 dr = df_from_double(0.0);
	df64 di = df_from_double(0.0);
	df64 maxr = df_from_double(config.options.maxRadius);
	int end_cond = config.options.endCondition;
	int n = 0;

	while (n < max_iter) {
		if (n >= ref.length) {
			/* Ref exhausted (escaped before this pixel) — fall back to direct */
			df64 cr = df_add(ref.c_real, dc_r);
			df64 ci = df_add(ref.c_imag, dc_i);
			df64 zr = df_add(ref.z_real[n - 1], dr);
			df64 zi = df_add(ref.z_imag[n - 1], di);
			for (; n < max_iter; n++) {
				df64 zr2 = df_mul(zr, zr);
				df64 zi2 = df_mul(zi, zi);
				int esc = 0;
				switch (end_cond) {
					case 1: esc = df_gt(df_mul(zr2, zi2), maxr); break;
					case 2: esc = df_gt(df_abs(df_sub(zr2, zi2)), maxr); break;
					case 3: esc = df_gt(df_add(df_abs(zr), df_abs(zi)), maxr); break;
					case 4: esc = df_gt(df_abs(df_sub(df_abs(zr), df_abs(zi))), maxr); break;
					default: esc = df_gt(df_add(zr2, zi2), maxr); break;
				}
				if (esc) break;
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

		df64 zr = df_add(Zr, dr);
		df64 zi = df_add(Zi, di);

		int escape = 0;
		if (end_cond == 0) {
			/* fast path with the precomputed |Z|^2 */
			df64 cross = df_mul_d(df_add(df_mul(Zr, dr), df_mul(Zi, di)), 2.0);
			df64 dmag2 = df_add(df_mul(dr, dr), df_mul(di, di));
			df64 zmag2 = df_add(df_add(ref.zmag2[n], cross), dmag2);
			escape = df_gt(zmag2, maxr);
		} else {
			df64 zr2 = df_mul(zr, zr);
			df64 zi2 = df_mul(zi, zi);
			switch (end_cond) {
				case 1: escape = df_gt(df_mul(zr2, zi2), maxr); break;
				case 2: escape = df_gt(df_abs(df_sub(zr2, zi2)), maxr); break;
				case 3: escape = df_gt(df_add(df_abs(zr), df_abs(zi)), maxr); break;
				case 4: escape = df_gt(df_abs(df_sub(df_abs(zr), df_abs(zi))), maxr); break;
			}
		}

		if (escape) {
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

		/* drift correction: if deviation grows too large, switch to direct */
		df64 dr2 = df_mul(dr, dr);
		df64 di2 = df_mul(di, di);
		df64 dmag2 = df_add(dr2, di2);
		if (df_to_double(dmag2) > 1e-4) {
			df64 cr = df_add(ref.c_real, dc_r);
			df64 ci = df_add(ref.c_imag, dc_i);
			df64 zr_g = zr;
			df64 zi_g = zi;
			for (; n < max_iter; n++) {
				df64 zr2 = df_mul(zr_g, zr_g);
				df64 zi2 = df_mul(zi_g, zi_g);
				int esc = 0;
				switch (end_cond) {
					case 1: esc = df_gt(df_mul(zr2, zi2), maxr); break;
					case 2: esc = df_gt(df_abs(df_sub(zr2, zi2)), maxr); break;
					case 3: esc = df_gt(df_add(df_abs(zr_g), df_abs(zi_g)), maxr); break;
					case 4: esc = df_gt(df_abs(df_sub(df_abs(zr_g), df_abs(zi_g))), maxr); break;
					default: esc = df_gt(df_add(zr2, zi2), maxr); break;
				}
				if (esc) break;
				zi_g = df_add(df_mul_d(df_mul(zr_g, zi_g), 2.0), ci);
				zr_g = df_add(df_sub(zr2, zi2), cr);
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

		df64 Zr_dr = df_mul(Zr, dr);
		df64 Zi_di = df_mul(Zi, di);
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

Result mandelbrot_calc(double c, double ci, int accuracy, int recursion_depth) {
	double zr = 0.0, zi = 0.0, zsq = 0.0, zisq = 0.0;
	int i = 0;
	double maxr = config.options.maxRadius;

	switch (config.options.endCondition) {
		case 1:
			while (i < accuracy && zsq * zisq < maxr) {
				zi = zr * zi * 2.0 + ci; zr = zsq - zisq + c;
				zsq = zr * zr; zisq = zi * zi; i++;
			}
			break;
		case 2:
			while (i < accuracy && fabs(zsq - zisq) < maxr) {
				zi = zr * zi * 2.0 + ci; zr = zsq - zisq + c;
				zsq = zr * zr; zisq = zi * zi; i++;
			}
			break;
		case 3:
			while (i < accuracy && fabs(zr) + fabs(zi) < maxr) {
				zi = zr * zi * 2.0 + ci; zr = zsq - zisq + c;
				zsq = zr * zr; zisq = zi * zi; i++;
			}
			break;
		case 4:
			while (i < accuracy && fabs(fabs(zr) - fabs(zi)) < maxr) {
				zi = zr * zi * 2.0 + ci; zr = zsq - zisq + c;
				zsq = zr * zr; zisq = zi * zi; i++;
			}
			break;
		default:
			while (i < accuracy && zsq + zisq < maxr) {
				zi = zr * zi * 2.0 + ci; zr = zsq - zisq + c;
				zsq = zr * zr; zisq = zi * zi; i++;
			}
	}

	Result res;
	res.count = i;
	res.z = zr;
	res.zi = zi;
	res.dz = zr - c;
	res.dzi = zi - ci;

	if (recursion_depth > 1 && (zr != c || zi != ci)) {
		Result r2 = mandelbrot_calc(res.dz, res.dzi, accuracy, recursion_depth - 1);
		res.count += r2.count;
		res.z += r2.z;
		res.zi += r2.zi;
		res.dz += r2.dz;
		res.dzi += r2.dzi;
	}

	return res;
}

typedef struct {
	int count;
	df64 z, zi;
	df64 dz, dzi;
} ResultDf64;

static ResultDf64 mandelbrot_calc_df64_inner(df64 c, df64 ci, int accuracy, int recursion_depth) {
	df64 zr = {0.0, 0.0}, zi = {0.0, 0.0};
	df64 zsq = {0.0, 0.0}, zisq = {0.0, 0.0};
	df64 maxr = df_from_double(config.options.maxRadius);
	int i = 0;

	switch (config.options.endCondition) {
		case 1:
			while (i < accuracy && df_lt(df_mul(zsq, zisq), maxr)) {
				zi = df_add(df_mul_d(df_mul(zr, zi), 2.0), ci); zr = df_add(df_sub(zsq, zisq), c);
				zsq = df_mul(zr, zr); zisq = df_mul(zi, zi); i++;
			}
			break;
		case 2:
			while (i < accuracy && df_lt(df_abs(df_sub(zsq, zisq)), maxr)) {
				zi = df_add(df_mul_d(df_mul(zr, zi), 2.0), ci); zr = df_add(df_sub(zsq, zisq), c);
				zsq = df_mul(zr, zr); zisq = df_mul(zi, zi); i++;
			}
			break;
		case 3:
			while (i < accuracy && df_lt(df_add(df_abs(zr), df_abs(zi)), maxr)) {
				zi = df_add(df_mul_d(df_mul(zr, zi), 2.0), ci); zr = df_add(df_sub(zsq, zisq), c);
				zsq = df_mul(zr, zr); zisq = df_mul(zi, zi); i++;
			}
			break;
		case 4:
			while (i < accuracy && df_lt(df_abs(df_sub(df_abs(zr), df_abs(zi))), maxr)) {
				zi = df_add(df_mul_d(df_mul(zr, zi), 2.0), ci); zr = df_add(df_sub(zsq, zisq), c);
				zsq = df_mul(zr, zr); zisq = df_mul(zi, zi); i++;
			}
			break;
		default:
			while (i < accuracy && df_lt(df_add(zsq, zisq), maxr)) {
				zi = df_add(df_mul_d(df_mul(zr, zi), 2.0), ci); zr = df_add(df_sub(zsq, zisq), c);
				zsq = df_mul(zr, zr); zisq = df_mul(zi, zi); i++;
			}
	}

	ResultDf64 res;
	res.count = i;
	res.z = zr;
	res.zi = zi;
	res.dz = df_sub(zr, c);
	res.dzi = df_sub(zi, ci);

	if (recursion_depth > 1 && !(df_eq(zr, c) && df_eq(zi, ci))) {
		ResultDf64 r2 = mandelbrot_calc_df64_inner(res.dz, res.dzi, accuracy, recursion_depth - 1);
		res.count += r2.count;
		res.z = df_add(res.z, r2.z);
		res.zi = df_add(res.zi, r2.zi);
		res.dz = df_add(res.dz, r2.dz);
		res.dzi = df_add(res.dzi, r2.dzi);
	}

	return res;
}

Result mandelbrot_calc_df64(df64 c, df64 ci, int accuracy, int recursion_depth) {
	ResultDf64 r = mandelbrot_calc_df64_inner(c, ci, accuracy, recursion_depth);
	Result out;
	out.count = r.count;
	out.z = df_to_double(r.z);
	out.zi = df_to_double(r.zi);
	out.dz = df_to_double(r.dz);
	out.dzi = df_to_double(r.dzi);
	return out;
}
