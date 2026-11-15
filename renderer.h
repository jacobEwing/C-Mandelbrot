#ifndef RENDERER_H
#define RENDERER_H

#include <SDL2/SDL.h>
#include <gmp.h>
#include "df64.h"

#define STRIP_HEIGHT 64

typedef struct {
	int active;
	int next_strip_y;
	int use_perturbation;
	double x_min, y_min, dx, dy;
	df64 dx_df, dy_df, ref_off_x_df, ref_off_y_df;
	Uint32 t_start;
	Uint32 t_render_start;
} RenderState;

extern RenderState render_state;
extern uint16_t g_counts[];

int compute_preview_transform(mpf_t cx, mpf_t cy, mpf_t zoom,
                              mpf_t last_cx, mpf_t last_cy, mpf_t last_zoom,
                              double *out_s, double *out_off_x, double *out_off_y);
void fill_scaled_background(uint32_t *pixels, const uint32_t *last_pixels,
                            double s, double off_x, double off_y);
void begin_render(mpf_t cx, mpf_t cy, mpf_t zoom,
                  uint32_t *pixels, SDL_Texture *tex, SDL_Renderer *ren,
                  const uint32_t *last_pixels, int have_last,
                  mpf_t last_cx, mpf_t last_cy, mpf_t last_zoom);
int render_strip(uint32_t *pixels, SDL_Texture *tex, SDL_Renderer *ren);
void redraw_palette(uint32_t *pixels, SDL_Texture *tex, SDL_Renderer *ren);

#endif
