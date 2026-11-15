#ifndef MANDELBROT_H
#define MANDELBROT_H

#include <stdint.h>
#include <gmp.h>
#include "df64.h"

#define MAX_ITER 1024
#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

typedef struct {
	double colourWavePeriod;
	double redOffset, redStagger, redPeriod;
	double greenOffset, greenStagger, greenPeriod;
	double blueOffset, blueStagger, bluePeriod;
	double masterOffset, masterStagger;
} Palette;

typedef struct {
	int calcIterations, calcDisplacement, calcRotation, calcDisplacementXOR, calcPolarCoordXOR;
	double coefIterations, coefDisplacement, coefRotation, coefDisplacementXOR, coefPolarCoordXOR;
	double countOffset;
	double maxRadius;
} Options;

typedef struct {
	Palette palette;
	Options options;
} RenderConfig;

extern RenderConfig config;

typedef struct {
	int count;
	double z, zi;
	double dz, dzi;
} Result;

extern Result g_results[];

typedef struct {
	df64 z_real[MAX_ITER + 1];
	df64 z_imag[MAX_ITER + 1];
	df64 zmag2[MAX_ITER + 1];
	df64 dz_real[MAX_ITER + 1];
	df64 dz_imag[MAX_ITER + 1];
	int length;
	int escaped;
	int escape_iteration;
	df64 c_real, c_imag;
	df64 offset_x, offset_y;
	int near_boundary_count;
	double max_zmag2;
} ReferenceOrbit;

extern ReferenceOrbit ref;

df64 mpf_to_df64(mpf_t x);
void compute_reference_orbit(mpf_t cx, mpf_t cy, mpf_t zoom);
Result mandelbrot_perturbation(df64 dc_r, df64 dc_i, int max_iter);
Result mandelbrot_calc(double c, double ci, int max_iter);
uint32_t create_colour(const Result *r);

#endif
