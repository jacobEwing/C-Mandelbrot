#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <gmp.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include "renderer.h"
#include "mandelbrot.h"

#define WHEEL_DEBOUNCE_MS 100
#define RESIZE_DEBOUNCE_MS 100

typedef struct {
	mpf_t cx;
	mpf_t cy;
	mpf_t zoom;
} ViewState;

static void mpf_to_str(mpf_t x, char* buffer, size_t size) {
	gmp_snprintf(buffer, size, "%.18Ff", x);
}

int main(int argc, char *argv[]) {
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
		return 1;
	}

	SDL_Window *win = SDL_CreateWindow("Mandelbrot Native",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		render_width, render_height,
		SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
	SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
	if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);

	// Sync render size with actual window size (WM may have overridden the request).
	int actual_w = render_width, actual_h = render_height;
	SDL_GetWindowSize(win, &actual_w, &actual_h);
	if (actual_w < 1) actual_w = 1;
	if (actual_h < 1) actual_h = 1;
	set_render_size(actual_w, actual_h);

	SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, render_width, render_height);
	SDL_Texture *preview_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, render_width, render_height);
	uint32_t *pixels = (uint32_t*)calloc((size_t)render_width * render_height, sizeof(uint32_t));
	uint32_t *last_pixels = (uint32_t*)calloc((size_t)render_width * render_height, sizeof(uint32_t));

	SDL_UpdateTexture(preview_tex, NULL, last_pixels, render_width * sizeof(uint32_t));

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	ImGui::StyleColorsDark();
	ImGui_ImplSDL2_InitForSDLRenderer(win, ren);
	ImGui_ImplSDLRenderer2_Init(ren);

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
		if (strncmp(argv[i], "--cx=", 5) == 0) mpf_set_str(view.cx, argv[i] + 5, 10);
		else if (strncmp(argv[i], "--cy=", 5) == 0) mpf_set_str(view.cy, argv[i] + 5, 10);
		else if (strncmp(argv[i], "--zoom=", 7) == 0) mpf_set_str(view.zoom, argv[i] + 7, 10);
	}

	int running = 1;
	int pending_render = 1;
	int abort_render = 0;
	Uint32 last_wheel_time = SDL_GetTicks() - WHEEL_DEBOUNCE_MS;
	int have_last_render = 0;
	int dragging = 0;
	double drag_start_mx = 0.0, drag_start_my = 0.0;
	mpf_t drag_start_cx, drag_start_cy;
	mpf_init2(drag_start_cx, 256);
	mpf_init2(drag_start_cy, 256);

	int pending_resize = 0;
	int resize_new_w = render_width, resize_new_h = render_height;
	Uint32 last_resize_time = 0;

	float ui_master_offset = config.palette.masterOffset;
	float ui_wave_period = config.palette.colourWavePeriod;
	float ui_red_offset = config.palette.redOffset;
	float ui_green_offset = config.palette.greenOffset;
	float ui_blue_offset = config.palette.blueOffset;
	bool palette_changed = false;

	while (running) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			ImGui_ImplSDL2_ProcessEvent(&e);

			if (e.type == SDL_QUIT) running = 0;
			if (e.type == SDL_WINDOWEVENT) {
				if (e.window.event == SDL_WINDOWEVENT_CLOSE && e.window.windowID == SDL_GetWindowID(win)) {
					running = 0;
				} else if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
					int nw = e.window.data1;
					int nh = e.window.data2;
					if (nw > 0 && nh > 0) {
						resize_new_w = nw;
						resize_new_h = nh;
						pending_resize = 1;
						last_resize_time = SDL_GetTicks();
					}
				}
			}

			if (!io.WantCaptureMouse) {
				if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
					dragging = 1;
					drag_start_mx = (double)e.button.x;
					drag_start_my = (double)e.button.y;
					mpf_set(drag_start_cx, view.cx);
					mpf_set(drag_start_cy, view.cy);
					if (render_state.active) abort_render = 1;
				} else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
					if (dragging) {
						double ix = (double)e.button.x;
						double iy = (double)e.button.y;
						double ddx = ix - drag_start_mx;
						double ddy = iy - drag_start_my;
						if (ddx * ddx + ddy * ddy < 25) {
							mpf_t scale, temp, offset;
							mpf_init2(scale, 256); mpf_init2(temp, 256); mpf_init2(offset, 256);
							mpf_div_ui(scale, view.zoom, render_width);
							mpf_set_d(temp, ix - render_width / 2.0);
							mpf_mul(offset, temp, scale);
							mpf_add(view.cx, view.cx, offset);
							mpf_set_d(temp, iy - render_height / 2.0);
							mpf_mul(offset, temp, scale);
							mpf_add(view.cy, view.cy, offset);
							mpf_clear(scale); mpf_clear(temp); mpf_clear(offset);
							pending_render = 1;
							last_wheel_time = SDL_GetTicks();
							if (render_state.active) abort_render = 1;
						}
					}
					dragging = 0;
				} else if (e.type == SDL_MOUSEMOTION && dragging) {
					double ix = (double)e.motion.x;
					double iy = (double)e.motion.y;
					double ddx = ix - drag_start_mx;
					double ddy = iy - drag_start_my;
					mpf_t scale, dx_mpf, dy_mpf, temp;
					mpf_init2(scale, 256); mpf_init2(dx_mpf, 256); mpf_init2(dy_mpf, 256); mpf_init2(temp, 256);
					mpf_div_ui(scale, view.zoom, render_width);
					mpf_set_d(temp, ddx);
					mpf_mul(dx_mpf, temp, scale);
					mpf_sub(view.cx, drag_start_cx, dx_mpf);
					mpf_set_d(temp, ddy);
					mpf_mul(dy_mpf, temp, scale);
					mpf_sub(view.cy, drag_start_cy, dy_mpf);
					mpf_clear(scale); mpf_clear(dx_mpf); mpf_clear(dy_mpf); mpf_clear(temp);
					pending_render = 1;
					last_wheel_time = SDL_GetTicks();
					if (render_state.active) abort_render = 1;
				} else if (e.type == SDL_MOUSEWHEEL) {
					int mx_raw, my_raw;
					SDL_GetMouseState(&mx_raw, &my_raw);
					double mx = (double)mx_raw;
					double my = (double)my_raw;

					mpf_t scale, dx, dy, temp, fx, fy;
					mpf_init2(scale, 256); mpf_init2(dx, 256); mpf_init2(dy, 256);
					mpf_init2(temp, 256); mpf_init2(fx, 256); mpf_init2(fy, 256);

					mpf_div_ui(scale, view.zoom, render_width);
					mpf_set_d(temp, mx - render_width / 2.0);
					mpf_mul(dx, temp, scale);
					mpf_add(fx, view.cx, dx);
					mpf_set_d(temp, my - render_height / 2.0);
					mpf_mul(dy, temp, scale);
					mpf_add(fy, view.cy, dy);

					if (e.wheel.y > 0) {
						mpf_mul_ui(view.zoom, view.zoom, 9);
						mpf_div_ui(view.zoom, view.zoom, 10);
					} else if (e.wheel.y < 0) {
						mpf_mul_ui(view.zoom, view.zoom, 10);
						mpf_div_ui(view.zoom, view.zoom, 9);
					}

					mpf_div_ui(scale, view.zoom, render_width);
					mpf_set_d(temp, mx - render_width / 2.0);
					mpf_mul(dx, temp, scale);
					mpf_sub(view.cx, fx, dx);
					mpf_set_d(temp, my - render_height / 2.0);
					mpf_mul(dy, temp, scale);
					mpf_sub(view.cy, fy, dy);

					mpf_clear(scale); mpf_clear(dx); mpf_clear(dy);
					mpf_clear(temp); mpf_clear(fx); mpf_clear(fy);

					pending_render = 1;
					last_wheel_time = SDL_GetTicks();
					if (render_state.active) abort_render = 1;
				}
			}

			if (!io.WantCaptureKeyboard && e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_P) {
				gmp_printf("--cx=%.80Ff --cy=%.80Ff --zoom=%.80Ff\n", view.cx, view.cy, view.zoom);
			}
		}

		// Debounced resize handling
		if (pending_resize && (SDL_GetTicks() - last_resize_time >= RESIZE_DEBOUNCE_MS)) {
			pending_resize = 0;
			if (resize_new_w != render_width || resize_new_h != render_height) {
				int old_w = render_width;

				render_state.active = 0;
				abort_render = 0;

				// Keep pixel scale constant: view.zoom is "fractal width across
				// render_width pixels". If render_width grows by k, zoom grows by k.
				if (old_w > 0 && resize_new_w != old_w) {
					mpf_t ratio;
					mpf_init2(ratio, 256);
					mpf_set_ui(ratio, resize_new_w);
					mpf_div_ui(ratio, ratio, old_w);
					mpf_mul(view.zoom, view.zoom, ratio);
					mpf_clear(ratio);
				}

				set_render_size(resize_new_w, resize_new_h);

				free(pixels);
				pixels = (uint32_t*)calloc((size_t)render_width * render_height, sizeof(uint32_t));
				free(last_pixels);
				last_pixels = (uint32_t*)calloc((size_t)render_width * render_height, sizeof(uint32_t));

				SDL_DestroyTexture(tex);
				tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, render_width, render_height);
				SDL_DestroyTexture(preview_tex);
				preview_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, render_width, render_height);

				have_last_render = 0;
				pending_render = 1;
				last_wheel_time = SDL_GetTicks() - WHEEL_DEBOUNCE_MS - 1;
			}
		}

		ImGui_ImplSDLRenderer2_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);
		ImGui::Begin("Mandelbrot Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

		char buf[128];
		mpf_to_str(view.cx, buf, sizeof(buf));
		ImGui::Text("Center X: %s", buf);
		mpf_to_str(view.cy, buf, sizeof(buf));
		ImGui::Text("Center Y: %s", buf);
		mpf_to_str(view.zoom, buf, sizeof(buf));
		ImGui::Text("Zoom: %s", buf);
		ImGui::Text("Render size: %d x %d", render_width, render_height);

		ImGui::Separator();
		ImGui::Text("Palette");

		if (ImGui::SliderFloat("Master Offset", &ui_master_offset, -3.14f, 3.14f)) { palette_changed = true; }
		if (ImGui::SliderFloat("Wave Period", &ui_wave_period, 1.0f, 256.0f)) { palette_changed = true; }
		if (ImGui::SliderFloat("Red Offset", &ui_red_offset, -3.14f, 3.14f)) { palette_changed = true; }
		if (ImGui::SliderFloat("Green Offset", &ui_green_offset, -3.14f, 3.14f)) { palette_changed = true; }
		if (ImGui::SliderFloat("Blue Offset", &ui_blue_offset, -3.14f, 3.14f)) { palette_changed = true; }

		if (palette_changed) {
			config.palette.masterOffset = ui_master_offset;
			config.palette.colourWavePeriod = ui_wave_period;
			config.palette.redOffset = ui_red_offset;
			config.palette.greenOffset = ui_green_offset;
			config.palette.blueOffset = ui_blue_offset;
			if (have_last_render) redraw_palette(pixels, tex, ren);
			palette_changed = false;
		}

		ImGui::End();
		ImGui::Render();

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
				memcpy(last_pixels, pixels, (size_t)render_width * render_height * sizeof(uint32_t));
				SDL_UpdateTexture(preview_tex, NULL, last_pixels, render_width * sizeof(uint32_t));
				mpf_set(last_render_cx, view.cx);
				mpf_set(last_render_cy, view.cy);
				mpf_set(last_render_zoom, view.zoom);
				have_last_render = 1;
			}
		}

		SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
		SDL_RenderClear(ren);

		bool in_scrolling = pending_render
			&& ((SDL_GetTicks() - last_wheel_time) < WHEEL_DEBOUNCE_MS)
			&& have_last_render;

		if (render_state.active || !in_scrolling) {
			SDL_RenderCopy(ren, tex, NULL, NULL);
		} else {
			double preview_s, preview_off_x, preview_off_y;
			if (compute_preview_transform(view.cx, view.cy, view.zoom,
			                              last_render_cx, last_render_cy, last_render_zoom,
			                              &preview_s, &preview_off_x, &preview_off_y)) {
				SDL_Rect src = { 0, 0, render_width, render_height };
				SDL_FRect dst = { (float)preview_off_x, (float)preview_off_y,
				                  (float)(render_width * preview_s), (float)(render_height * preview_s) };
				SDL_RenderCopyF(ren, preview_tex, &src, &dst);
			} else {
				SDL_RenderCopy(ren, tex, NULL, NULL);
			}
		}

		ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), ren);
		SDL_RenderPresent(ren);

		SDL_Delay(5);
	}

	ImGui_ImplSDLRenderer2_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();

	mpf_clear(view.cx); mpf_clear(view.cy); mpf_clear(view.zoom);
	mpf_clear(last_render_cx); mpf_clear(last_render_cy); mpf_clear(last_render_zoom);
	mpf_clear(drag_start_cx); mpf_clear(drag_start_cy);
	free(pixels);
	free(last_pixels);
	free(g_results);
	free(g_counts);
	SDL_DestroyTexture(tex);
	SDL_DestroyTexture(preview_tex);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();
	return 0;
}
