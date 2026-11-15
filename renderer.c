#include "renderer.h"
#include "mandelbrot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef _OPENMP
#include <omp.h>
#endif

int render_width = 800;
int render_height = 600;
Result *g_results = NULL;
uint16_t *g_counts = NULL;
RenderState render_state = { 0 };

void set_render_size(int w, int h) {
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	if (w == render_width && h == render_height && g_results != NULL) return;
	render_width = w;
	render_height = h;
	free(g_results);
	g_results = (Result *)calloc((size_t)w * h, sizeof(Result));
	free(g_counts);
	g_counts = (uint16_t *)calloc((size_t)w * h, sizeof(uint16_t));
}

int compute_preview_transform(mpf_t cx, mpf_t cy, mpf_t zoom,
                              mpf_t last_cx, mpf_t last_cy, mpf_t last_zoom,
                              double *out_s, double *out_off_x, double *out_off_y) {
	if (mpf_sgn(last_zoom) == 0) return 0;

	mpf_t s, w, h, temp, off_x, off_y;
	mpf_init2(s, 256); mpf_init2(w, 256); mpf_init2(h, 256);
	mpf_init2(temp, 256); mpf_init2(off_x, 256); mpf_init2(off_y, 256);

	mpf_div(s, last_zoom, zoom);
	double s_d = mpf_get_d(s);
	if (!(s_d > 1e-3 && s_d < 1e3)) {
		mpf_clears(s, w, h, temp, off_x, off_y, NULL);
		return 0;
	}

	mpf_set_d(w, render_width);
	mpf_set_d(h, render_height);

	mpf_sub(temp, last_cx, cx);
	mpf_mul(temp, temp, w);
	mpf_div(temp, temp, zoom);
	mpf_div_ui(off_x, w, 2);
	mpf_add(off_x, off_x, temp);
	mpf_mul(temp, w, s);
	mpf_div_ui(temp, temp, 2);
	mpf_sub(off_x, off_x, temp);

	mpf_sub(temp, last_cy, cy);
	mpf_mul(temp, temp, w);
	mpf_div(temp, temp, zoom);
	mpf_div_ui(off_y, h, 2);
	mpf_add(off_y, off_y, temp);
	mpf_mul(temp, h, s);
	mpf_div_ui(temp, temp, 2);
	mpf_sub(off_y, off_y, temp);

	*out_s = s_d;
	*out_off_x = mpf_get_d(off_x);
	*out_off_y = mpf_get_d(off_y);

	mpf_clears(s, w, h, temp, off_x, off_y, NULL);
	return 1;
}

void fill_scaled_background(uint32_t *pixels, const uint32_t *last_pixels,
                            double s, double off_x, double off_y) {
	for (int y = 0; y < render_height; y++) {
		double src_y = (y - off_y) / s;
		int sy = (int)src_y;
		int row_in_bounds = (src_y >= 0 && sy < render_height);
		for (int x = 0; x < render_width; x++) {
			uint32_t px = 0xFF000000;
			if (row_in_bounds) {
				double src_x = (x - off_x) / s;
				int sx = (int)src_x;
				if (src_x >= 0 && sx < render_width) {
					px = last_pixels[sy * render_width + sx];
				}
			}
			pixels[y * render_width + x] = px;
		}
	}
}

static void render_row_double(uint32_t *pixels, int y, double x_min, double y_min, double dx, double dy) {
	double ci = y_min + y * dy;
	for (int x = 0; x < render_width; x++) {
		double cr = x_min + x * dx;
		Result r = mandelbrot_calc(cr, ci, MAX_ITER);
		g_results[y * render_width + x] = r;
		pixels[y * render_width + x] = create_colour(&r);
		g_counts[y * render_width + x] = (uint16_t)r.count;
	}
}

static void render_row_perturbation(uint32_t *pixels, int y,
                                    df64 dx_df, df64 dy_df,
                                    df64 ref_offset_x, df64 ref_offset_y) {
	df64 dci = df_add(df_mul_d(dy_df, (double)(y - render_height / 2)), ref_offset_y);
	for (int x = 0; x < render_width; x++) {
		df64 dc = df_add(df_mul_d(dx_df, (double)(x - render_width / 2)), ref_offset_x);
		Result r = mandelbrot_perturbation(dc, dci, MAX_ITER);
		g_results[y * render_width + x] = r;
		pixels[y * render_width + x] = create_colour(&r);
		g_counts[y * render_width + x] = (uint16_t)r.count;
	}
}

void begin_render(mpf_t cx, mpf_t cy, mpf_t zoom,
                  uint32_t *pixels, SDL_Texture *tex, SDL_Renderer *ren,
                  const uint32_t *last_pixels, int have_last,
                  mpf_t last_cx, mpf_t last_cy, mpf_t last_zoom) {
	double zoom_d = mpf_get_d(zoom);
	double cx_d = mpf_get_d(cx);
	double cy_d = mpf_get_d(cy);
	double aspect = (double)render_height / render_width;

	render_state.use_perturbation = (zoom_d < 1e-12);
	render_state.next_strip_y = 0;
	render_state.t_start = SDL_GetTicks();

	double preview_s, preview_off_x, preview_off_y;
	int have_preview = have_last && compute_preview_transform(cx, cy, zoom, last_cx, last_cy, last_zoom,
	                                                           &preview_s, &preview_off_x, &preview_off_y);
	if (have_preview) {
		fill_scaled_background(pixels, last_pixels, preview_s, preview_off_x, preview_off_y);
	} else {
		memset(pixels, 0, (size_t)render_width * render_height * sizeof(uint32_t));
	}

	SDL_UpdateTexture(tex, NULL, pixels, render_width * sizeof(uint32_t));

	gmp_printf("[view cx=%.80Ff cy=%.80Ff zoom=%.80Ff]\n", cx, cy, zoom);

	if (render_state.use_perturbation) {
		Uint32 t_ref_start = SDL_GetTicks();
		compute_reference_orbit(cx, cy, zoom);
		Uint32 t_ref_end = SDL_GetTicks();
		printf("[ref orbit: %u ms, escaped=%d, iter=%d, near_boundary=%d, max_zmag2=%.10f]\n",
			t_ref_end - t_ref_start, ref.escaped, ref.escape_iteration,
			ref.near_boundary_count, ref.max_zmag2);

		mpf_t dx_mpf, dy_mpf, aspect_mpf;
		mpf_init2(dx_mpf, 256); mpf_init2(dy_mpf, 256); mpf_init2(aspect_mpf, 256);
		mpf_div_ui(dx_mpf, zoom, render_width);
		mpf_set_d(aspect_mpf, aspect);
		mpf_mul(dy_mpf, zoom, aspect_mpf);
		mpf_div_ui(dy_mpf, dy_mpf, render_height);
		render_state.dx_df = mpf_to_df64(dx_mpf);
		render_state.dy_df = mpf_to_df64(dy_mpf);
		render_state.ref_off_x_df = ref.offset_x;
		render_state.ref_off_y_df = ref.offset_y;
		mpf_clear(dx_mpf); mpf_clear(dy_mpf); mpf_clear(aspect_mpf);
	} else {
		double width = zoom_d;
		double height = width * aspect;
		render_state.x_min = cx_d - width / 2.0;
		render_state.y_min = cy_d - height / 2.0;
		render_state.dx = width / render_width;
		render_state.dy = height / render_height;
	}

	render_state.t_render_start = SDL_GetTicks();
	render_state.active = 1;
}

int render_strip(uint32_t *pixels, SDL_Texture *tex, SDL_Renderer *ren) {
	int y_start = render_state.next_strip_y;
	int y_end = y_start + STRIP_HEIGHT;
	if (y_end > render_height) y_end = render_height;

	#pragma omp parallel for schedule(dynamic, 1)
	for (int y = y_start; y < y_end; y++) {
		if (render_state.use_perturbation) render_row_perturbation(pixels, y, render_state.dx_df, render_state.dy_df, render_state.ref_off_x_df, render_state.ref_off_y_df);
		else render_row_double(pixels, y, render_state.x_min, render_state.y_min, render_state.dx, render_state.dy);
	}

	SDL_Rect rect = { 0, y_start, render_width, y_end - y_start };
	SDL_UpdateTexture(tex, &rect, pixels + y_start * render_width, render_width * sizeof(uint32_t));

	render_state.next_strip_y = y_end;

	if (render_state.next_strip_y >= render_height) {
		Uint32 t_end = SDL_GetTicks();
		int nthreads = 1;
		#ifdef _OPENMP
		nthreads = omp_get_max_threads();
		#endif
		printf("[render: %u ms, threads=%d, total=%u ms]\n",
			t_end - render_state.t_render_start, nthreads, t_end - render_state.t_start);

		if (render_state.use_perturbation) {
			int in_set = 0, near_ref = 0, other = 0;
			for (int i = 0; i < render_width * render_height; i++) {
				int c = g_counts[i];
				if (c >= MAX_ITER) in_set++;
				else if (abs(c - ref.escape_iteration) < 10) near_ref++;
				else other++;
			}
			int mid_idx = (render_height / 2) * render_width + render_width / 2;
			int ref_count = g_counts[mid_idx];
			printf("[counts: in_set=%d near_ref=%d other=%d ref_pixel_count=%d (ref_iter=%d)]\n",
				in_set, near_ref, other, ref_count, ref.escape_iteration);
		}
		return 1;
	}
	return 0;
}

void redraw_palette(uint32_t *pixels, SDL_Texture *tex, SDL_Renderer *ren) {
	for (int y = 0; y < render_height; y++) {
		for (int x = 0; x < render_width; x++) {
			int idx = y * render_width + x;
			pixels[idx] = create_colour(&g_results[idx]);
		}
	}
	SDL_UpdateTexture(tex, NULL, pixels, render_width * sizeof(uint32_t));
}
