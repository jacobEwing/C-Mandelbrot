#ifndef MANDELBROT_H
#define MANDELBROT_H

#include <stdint.h>
#include <gmp.h>
#include "df64.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	double colourWavePeriod;
	double redOffset, redStagger, redPeriod;
	double greenOffset, greenStagger, greenPeriod;
	double blueOffset, blueStagger, bluePeriod;
	double masterOffset, masterStagger;
	
	int staggerMaskRed, staggerMaskGreen, staggerMaskBlue;
	int staggerMaskRedLen, staggerMaskGreenLen, staggerMaskBlueLen;
	
	int staggerBefore, staggerAfter;
} Palette;

typedef struct {
	int max_iter;
	int calcIterations, calcDisplacement, calcRotation, calcDisplacementXOR, calcPolarCoordXOR;
	double coefIterations, coefDisplacement, coefRotation, coefDisplacementXOR, coefPolarCoordXOR;
	double countOffset;
	double maxRadius;
	int endCondition;
	int recursionDepth;
} Options;

typedef struct {
	Palette palette;
	Options options;
	double viewRotation; /* radians, ±π; rotates sampling grid about view centre */
} RenderConfig;

extern RenderConfig config;

typedef struct {
	int count;
	double z, zi;
	double dz, dzi;
} Result;

typedef struct {
	df64* z_real;
	df64* z_imag;
	df64* zmag2;
	df64* dz_real;
	df64* dz_imag;
	int capacity;
	
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
Result mandelbrot_calc(double c, double ci, int accuracy, int recursion_depth);
Result mandelbrot_calc_df64(df64 c, df64 ci, int accuracy, int recursion_depth);
uint32_t create_colour(const Result *r);
void reset_config(void);

#ifdef __cplusplus
}
#endif

#endif
