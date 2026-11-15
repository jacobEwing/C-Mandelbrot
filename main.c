#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <gmp.h>
#include "renderer.h"
#include "mandelbrot.h"

#define ZOOM_FACTOR 0.9
#define WHEEL_DEBOUNCE_MS 100

typedef struct {
	mpf_t cx;
	mpf_t cy;
	mpf_t zoom;
} ViewState;

static void print_current_view(ViewState *view) {
	gmp_printf("--cx=%.80Ff --cy=%.80Ff --zoom=%.80Ff\n", view->cx, view->cy, view->zoom);
}

int main(int argc, char *argv[]) {
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
		return 1;
	}

	SDL_Window *win = SDL_CreateWindow("Mandelbrot Native", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
	SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
	if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
	SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WINDOW_WIDTH, WINDOW_HEIGHT);
	SDL_Texture *preview_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WINDOW_WIDTH, WINDOW_HEIGHT);
	uint32_t *pixels = calloc(WINDOW_WIDTH * WINDOW_HEIGHT, sizeof(uint32_t));
	uint32_t *last_pixels = calloc(WINDOW_WIDTH * WINDOW_HEIGHT, sizeof(uint32_t));

	SDL_UpdateTexture(preview_tex, NULL, last_pixels, WINDOW_WIDTH * sizeof(uint32_t));

	ViewState view;
	mpf_init2(view.cx, 256);
	mpf_init2(view.cy, 256);
	mpf_init2(view.zoom, 256);
	mpf_set_d(view.cx, -0.5);
	mpf_set_d(view.cy, 0.0);
	mpf_set_d(view.zoom, 4.0);

	mpf_t last_render_cx, last_render_cy, last_render_zoom;
	mpf_init2(last_render_cx, 256);
	mpf_init2(last_render_cy, 256);
	mpf_init2(last_render_zoom, 256);

	for (int i = 1; i < argc; i++) {
		if (strncmp(argv[i], "--cx=", 5) == 0) {
			if (mpf_set_str(view.cx, argv[i] + 5, 10) != 0) fprintf(stderr, "Bad --cx: %s\n", argv[i] + 5);
		} else if (strncmp(argv[i], "--cy=", 5) == 0) {
			if (mpf_set_str(view.cy, argv[i] + 5, 10) != 0) fprintf(stderr, "Bad --cy: %s\n", argv[i] + 5);
		} else if (strncmp(argv[i], "--zoom=", 7) == 0) {
			if (mpf_set_str(view.zoom, argv[i] + 7, 10) != 0) fprintf(stderr, "Bad --zoom: %s\n", argv[i] + 7);
		}
	}

	int running = 1;
	int pending_render = 1;
	int abort_render = 0;
	Uint32 last_wheel_time = SDL_GetTicks();

	int have_last_render = 0;

	int dragging = 0;
	int drag_start_mx = 0, drag_start_my = 0;
	mpf_t drag_start_cx, drag_start_cy;
	mpf_init2(drag_start_cx, 256);
	mpf_init2(drag_start_cy, 256);

	while (running) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) {
				running = 0;
			} else if (e.type == SDL_WINDOWEVENT) {
				if (e.window.event == SDL_WINDOWEVENT_EXPOSED) {
					SDL_RenderClear(ren);
					SDL_RenderCopy(ren, tex, NULL, NULL);
					SDL_RenderPresent(ren);
				}
			} else if (e.type == SDL_KEYDOWN) {
				if (e.key.keysym.sym == SDLK_p) {
					print_current_view(&view);
				} else if (e.key.keysym.sym == SDLK_r) {
					mpf_set_d(view.cx, -0.5);
					mpf_set_d(view.cy, 0.0);
					mpf_set_d(view.zoom, 4.0);
					have_last_render = 0;
					pending_render = 1;
					last_wheel_time = SDL_GetTicks();
					if (render_state.active) abort_render = 1;
				}
				// --- Palette Keyboard Controls ---
				else if (e.key.keysym.sym == SDLK_LEFTBRACKET) {
					config.palette.masterOffset -= 0.1;
					if (have_last_render) redraw_palette(pixels, tex, ren);
				} else if (e.key.keysym.sym == SDLK_RIGHTBRACKET) {
					config.palette.masterOffset += 0.1;
					if (have_last_render) redraw_palette(pixels, tex, ren);
				} else if (e.key.keysym.sym == SDLK_MINUS) {
					config.palette.colourWavePeriod *= 1.1;
					if (have_last_render) redraw_palette(pixels, tex, ren);
				} else if (e.key.keysym.sym == SDLK_EQUALS) {
					config.palette.colourWavePeriod /= 1.1;
					if (have_last_render) redraw_palette(pixels, tex, ren);
				} else if (e.key.keysym.sym == SDLK_1) {
					config.palette.redOffset -= 0.1;
					if (have_last_render) redraw_palette(pixels, tex, ren);
				} else if (e.key.keysym.sym == SDLK_2) {
					config.palette.redOffset += 0.1;
					if (have_last_render) redraw_palette(pixels, tex, ren);
				} else if (e.key.keysym.sym == SDLK_3) {
					config.palette.greenOffset -= 0.1;
					if (have_last_render) redraw_palette(pixels, tex, ren);
				} else if (e.key.keysym.sym == SDLK_4) {
					config.palette.greenOffset += 0.1;
					if (have_last_render) redraw_palette(pixels, tex, ren);
				} else if (e.key.keysym.sym == SDLK_5) {
					config.palette.blueOffset -= 0.1;
					if (have_last_render) redraw_palette(pixels, tex, ren);
				} else if (e.key.keysym.sym == SDLK_6) {
					config.palette.blueOffset += 0.1;
					if (have_last_render) redraw_palette(pixels, tex, ren);
				}
			} else if (e.type == SDL_MOUSEBUTTONDOWN) {
				if (e.button.button == SDL_BUTTON_LEFT) {
					dragging = 1;
					drag_start_mx = e.button.x;
					drag_start_my = e.button.y;
					mpf_set(drag_start_cx, view.cx);
					mpf_set(drag_start_cy, view.cy);
					if (render_state.active) abort_render = 1;
				}
			} else if (e.type == SDL_MOUSEBUTTONUP) {
				if (e.button.button == SDL_BUTTON_LEFT) {
					if (dragging) {
						int dx = e.button.x - drag_start_mx;
						int dy = e.button.y - drag_start_my;
						if (dx * dx + dy * dy < 25) {
							mpf_t scale, temp, offset;
							mpf_init2(scale, 256); mpf_init2(temp, 256); mpf_init2(offset, 256);
							mpf_div_ui(scale, view.zoom, WINDOW_WIDTH);
							mpf_set_d(temp, e.button.x - WINDOW_WIDTH / 2.0);
							mpf_mul(offset, temp, scale);
							mpf_add(view.cx, view.cx, offset);
							mpf_set_d(temp, e.button.y - WINDOW_HEIGHT / 2.0);
							mpf_mul(offset, temp, scale);
							mpf_add(view.cy, view.cy, offset);
							mpf_clear(scale); mpf_clear(temp); mpf_clear(offset);
							pending_render = 1;
							last_wheel_time = SDL_GetTicks();
							if (render_state.active) abort_render = 1;
						}
					}
					dragging = 0;
				}
			} else if (e.type == SDL_MOUSEMOTION) {
				if (dragging) {
					int dx = e.motion.x - drag_start_mx;
					int dy = e.motion.y - drag_start_my;
					mpf_t scale, dx_mpf, dy_mpf, temp;
					mpf_init2(scale, 256); mpf_init2(dx_mpf, 256); mpf_init2(dy_mpf, 256); mpf_init2(temp, 256);
					mpf_div_ui(scale, view.zoom, WINDOW_WIDTH);
					mpf_set_d(temp, dx);
					mpf_mul(dx_mpf, temp, scale);
					mpf_sub(view.cx, drag_start_cx, dx_mpf);
					mpf_set_d(temp, dy);
					mpf_mul(dy_mpf, temp, scale);
					mpf_sub(view.cy, drag_start_cy, dy_mpf);
					mpf_clear(scale); mpf_clear(dx_mpf); mpf_clear(dy_mpf); mpf_clear(temp);

					double preview_s, preview_off_x, preview_off_y;
					if (have_last_render && compute_preview_transform(view.cx, view.cy, view.zoom, last_render_cx, last_render_cy, last_render_zoom, &preview_s, &preview_off_x, &preview_off_y)) {
						SDL_Rect src = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
						SDL_FRect dst = { (float)preview_off_x, (float)preview_off_y, (float)(WINDOW_WIDTH * preview_s), (float)(WINDOW_HEIGHT * preview_s) };
						SDL_RenderClear(ren);
						SDL_RenderCopyF(ren, preview_tex, &src, &dst);
						SDL_RenderPresent(ren);
					}

					pending_render = 1;
					last_wheel_time = SDL_GetTicks();
					if (render_state.active) abort_render = 1;
				}
			} else if (e.type == SDL_MOUSEWHEEL) {
				int mx, my;
				SDL_GetMouseState(&mx, &my);

				mpf_t scale, dx, dy, temp, fx, fy;
				mpf_init2(scale, 256); mpf_init2(dx, 256); mpf_init2(dy, 256);
				mpf_init2(temp, 256); mpf_init2(fx, 256); mpf_init2(fy, 256);

				mpf_div_ui(scale, view.zoom, WINDOW_WIDTH);
				mpf_set_d(temp, mx - WINDOW_WIDTH / 2.0);
				mpf_mul(dx, temp, scale);
				mpf_add(fx, view.cx, dx);
				mpf_set_d(temp, my - WINDOW_HEIGHT / 2.0);
				mpf_mul(dy, temp, scale);
				mpf_add(fy, view.cy, dy);

				if (e.wheel.y > 0) {
					mpf_mul_ui(view.zoom, view.zoom, 9);
					mpf_div_ui(view.zoom, view.zoom, 10);
				} else if (e.wheel.y < 0) {
					mpf_mul_ui(view.zoom, view.zoom, 10);
					mpf_div_ui(view.zoom, view.zoom, 9);
				}

				mpf_div_ui(scale, view.zoom, WINDOW_WIDTH);
				mpf_set_d(temp, mx - WINDOW_WIDTH / 2.0);
				mpf_mul(dx, temp, scale);
				mpf_sub(view.cx, fx, dx);
				mpf_set_d(temp, my - WINDOW_HEIGHT / 2.0);
				mpf_mul(dy, temp, scale);
				mpf_sub(view.cy, fy, dy);

				mpf_clear(scale); mpf_clear(dx); mpf_clear(dy);
				mpf_clear(temp); mpf_clear(fx); mpf_clear(fy);

				double preview_s, preview_off_x, preview_off_y;
				if (have_last_render && compute_preview_transform(view.cx, view.cy, view.zoom, last_render_cx, last_render_cy, last_render_zoom, &preview_s, &preview_off_x, &preview_off_y)) {
					SDL_Rect src = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
					SDL_FRect dst = { (float)preview_off_x, (float)preview_off_y, (float)(WINDOW_WIDTH * preview_s), (float)(WINDOW_HEIGHT * preview_s) };
					SDL_RenderClear(ren);
					SDL_RenderCopyF(ren, preview_tex, &src, &dst);
					SDL_RenderPresent(ren);
				}

				pending_render = 1;
				last_wheel_time = SDL_GetTicks();
				if (render_state.active) abort_render = 1;
			}
		}

		if (render_state.active && abort_render) {
			render_state.active = 0;
			abort_render = 0;
		}

		if (!render_state.active) {
			if (pending_render && (SDL_GetTicks() - last_wheel_time >= WHEEL_DEBOUNCE_MS)) {
				pending_render = 0;
				begin_render(view.cx, view.cy, view.zoom, pixels, tex, ren, last_pixels, have_last_render, last_render_cx, last_render_cy, last_render_zoom);
			}
		}

		if (render_state.active) {
			int done = render_strip(pixels, tex, ren);
			if (done) {
				render_state.active = 0;
				memcpy(last_pixels, pixels, WINDOW_WIDTH * WINDOW_HEIGHT * sizeof(uint32_t));
				SDL_UpdateTexture(preview_tex, NULL, last_pixels, WINDOW_WIDTH * sizeof(uint32_t));
				mpf_set(last_render_cx, view.cx);
				mpf_set(last_render_cy, view.cy);
				mpf_set(last_render_zoom, view.zoom);
				have_last_render = 1;
			}
		}

		SDL_Delay(5);
	}

	mpf_clear(view.cx); mpf_clear(view.cy); mpf_clear(view.zoom);
	mpf_clear(last_render_cx); mpf_clear(last_render_cy); mpf_clear(last_render_zoom);
	mpf_clear(drag_start_cx); mpf_clear(drag_start_cy);
	free(pixels);
	free(last_pixels);
	SDL_DestroyTexture(tex);
	SDL_DestroyTexture(preview_tex);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();
	return 0;
}
