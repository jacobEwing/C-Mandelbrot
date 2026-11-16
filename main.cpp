#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
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

static bool InputIntWheel(const char* label, int* v, int step = 1) {
	bool changed = ImGui::InputInt(label, v, step, step * 10);
	if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
		*v += (int)(ImGui::GetIO().MouseWheel * step);
		changed = true;
	}
	return changed;
}

static bool InputDoubleWheel(const char* label, double* v, double step = 0.01, double step_fast = 0.1) {
	bool changed = ImGui::InputDouble(label, v, step, step_fast, "%.6f");
	if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
		*v += ImGui::GetIO().MouseWheel * step;
		changed = true;
	}
	return changed;
}

int main(int argc, char *argv[]) {
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
		return 1;
	}

	// --- Render window (fractal only) ---
	SDL_Window *render_win = SDL_CreateWindow(
		"Mandelbrot Render",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		render_width, render_height,
		SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
	if (!render_win) {
		fprintf(stderr, "SDL_CreateWindow(render) Error: %s\n", SDL_GetError());
		return 1;
	}
	SDL_Renderer *render_ren = SDL_CreateRenderer(render_win, -1, SDL_RENDERER_ACCELERATED);
	if (!render_ren) render_ren = SDL_CreateRenderer(render_win, -1, SDL_RENDERER_SOFTWARE);

	int actual_w = render_width, actual_h = render_height;
	SDL_GetWindowSize(render_win, &actual_w, &actual_h);
	set_render_size(actual_w > 0 ? actual_w : 800, actual_h > 0 ? actual_h : 600);

	SDL_Texture *tex = SDL_CreateTexture(render_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, render_width, render_height);
	SDL_Texture *preview_tex = SDL_CreateTexture(render_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, render_width, render_height);
	uint32_t *pixels = (uint32_t*)calloc((size_t)render_width * render_height, sizeof(uint32_t));
	uint32_t *last_pixels = (uint32_t*)calloc((size_t)render_width * render_height, sizeof(uint32_t));

	SDL_UpdateTexture(preview_tex, NULL, last_pixels, render_width * sizeof(uint32_t));

	// --- UI window (ImGui controls only) ---
	int fx, fy;
	SDL_GetWindowPosition(render_win, &fx, &fy);
	SDL_Window *ui_win = SDL_CreateWindow(
		"Mandelbrot Controls",
		fx + actual_w + 20, fy,
		400, 640,
		SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
	if (!ui_win) {
		fprintf(stderr, "SDL_CreateWindow(ui) Error: %s\n", SDL_GetError());
		return 1;
	}
	SDL_Renderer *ui_ren = SDL_CreateRenderer(ui_win, -1, SDL_RENDERER_ACCELERATED);
	if (!ui_ren) ui_ren = SDL_CreateRenderer(ui_win, -1, SDL_RENDERER_SOFTWARE);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	ImGui::StyleColorsDark();
	ImGui_ImplSDL2_InitForSDLRenderer(ui_win, ui_ren);
	ImGui_ImplSDLRenderer2_Init(ui_ren);

	Uint32 render_win_id = SDL_GetWindowID(render_win);
	Uint32 ui_win_id = SDL_GetWindowID(ui_win);

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

	int ui_max_iter = config.options.max_iter;
	float ui_max_radius = config.options.maxRadius;
	int ui_recursion = config.options.recursionDepth;
	double ui_count_offset = config.options.countOffset;
	int ui_end_condition = config.options.endCondition;

	bool ui_calc_iter = config.options.calcIterations;
	bool ui_calc_disp = config.options.calcDisplacement;
	bool ui_calc_rot = config.options.calcRotation;
	bool ui_calc_xor_disp = config.options.calcDisplacementXOR;
	bool ui_calc_xor_polar = config.options.calcPolarCoordXOR;

	float ui_master_offset = config.palette.masterOffset;
	float ui_red_offset = config.palette.redOffset;
	float ui_green_offset = config.palette.greenOffset;
	float ui_blue_offset = config.palette.blueOffset;
	float ui_master_stagger = config.palette.masterStagger;
	float ui_red_stagger = config.palette.redStagger;
	float ui_green_stagger = config.palette.greenStagger;
	float ui_blue_stagger = config.palette.blueStagger;

	double ui_wave_period = config.palette.colourWavePeriod;
	double ui_red_period = config.palette.redPeriod;
	double ui_green_period = config.palette.greenPeriod;
	double ui_blue_period = config.palette.bluePeriod;

	bool ui_stagger_before = config.palette.staggerBefore;
	bool ui_stagger_after = config.palette.staggerAfter;
	char ui_mask_red[32] = "10";
	char ui_mask_green[32] = "10";
	char ui_mask_blue[32] = "10";

	bool palette_changed = false;

	while (running) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) { running = 0; break; }

			Uint32 evt_win = 0;
			switch (e.type) {
				case SDL_WINDOWEVENT: evt_win = e.window.windowID; break;
				case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: evt_win = e.button.windowID; break;
				case SDL_MOUSEMOTION: evt_win = e.motion.windowID; break;
				case SDL_MOUSEWHEEL: evt_win = e.wheel.windowID; break;
				case SDL_KEYDOWN: case SDL_KEYUP: evt_win = e.key.windowID; break;
				case SDL_TEXTINPUT: evt_win = e.text.windowID; break;
				default: break;
			}

			if (evt_win == ui_win_id) {
				ImGui_ImplSDL2_ProcessEvent(&e);
			}

			if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) {
				running = 0;
				break;
			}

			if (evt_win == render_win_id) {
				if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
					int nw = e.window.data1, nh = e.window.data2;
					if (nw > 0 && nh > 0) {
						resize_new_w = nw; resize_new_h = nh;
						pending_resize = 1; last_resize_time = SDL_GetTicks();
					}
				}
				if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_F11) {
					Uint32 flags = SDL_GetWindowFlags(render_win);
					if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP) {
						SDL_SetWindowFullscreen(render_win, 0);
					} else {
						SDL_SetWindowFullscreen(render_win, SDL_WINDOW_FULLSCREEN_DESKTOP);
					}
				}
				if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_P) {
					gmp_printf("--cx=%.80Ff --cy=%.80Ff --zoom=%.80Ff\n", view.cx, view.cy, view.zoom);
				}
				if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
					dragging = 1;
					drag_start_mx = (double)e.button.x; drag_start_my = (double)e.button.y;
					mpf_set(drag_start_cx, view.cx); mpf_set(drag_start_cy, view.cy);
					if (render_state.active) abort_render = 1;
				} else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
					if (dragging) {
						double ix = (double)e.button.x, iy = (double)e.button.y;
						double ddx = ix - drag_start_mx, ddy = iy - drag_start_my;
						if (ddx * ddx + ddy * ddy < 25) {
							mpf_t scale, temp, offset;
							mpf_init2(scale, 256); mpf_init2(temp, 256); mpf_init2(offset, 256);
							mpf_div_ui(scale, view.zoom, render_width);
							mpf_set_d(temp, ix - render_width / 2.0);
							mpf_mul(offset, temp, scale); mpf_add(view.cx, view.cx, offset);
							mpf_set_d(temp, iy - render_height / 2.0);
							mpf_mul(offset, temp, scale); mpf_add(view.cy, view.cy, offset);
							mpf_clear(scale); mpf_clear(temp); mpf_clear(offset);
							pending_render = 1; last_wheel_time = SDL_GetTicks();
							if (render_state.active) abort_render = 1;
						}
					}
					dragging = 0;
				} else if (e.type == SDL_MOUSEMOTION && dragging) {
					double ix = (double)e.motion.x, iy = (double)e.motion.y;
					double ddx = ix - drag_start_mx, ddy = iy - drag_start_my;
					mpf_t scale, dx_mpf, dy_mpf, temp;
					mpf_init2(scale, 256); mpf_init2(dx_mpf, 256); mpf_init2(dy_mpf, 256); mpf_init2(temp, 256);
					mpf_div_ui(scale, view.zoom, render_width);
					mpf_set_d(temp, ddx); mpf_mul(dx_mpf, temp, scale);
					mpf_sub(view.cx, drag_start_cx, dx_mpf);
					mpf_set_d(temp, ddy); mpf_mul(dy_mpf, temp, scale);
					mpf_sub(view.cy, drag_start_cy, dy_mpf);
					mpf_clear(scale); mpf_clear(dx_mpf); mpf_clear(dy_mpf); mpf_clear(temp);
					pending_render = 1; last_wheel_time = SDL_GetTicks();
					if (render_state.active) abort_render = 1;
				} else if (e.type == SDL_MOUSEWHEEL) {
					int mx_raw, my_raw; SDL_GetMouseState(&mx_raw, &my_raw);
					double mx = (double)mx_raw, my = (double)my_raw;
					mpf_t scale, dx, dy, temp, fx2, fy2;
					mpf_init2(scale, 256); mpf_init2(dx, 256); mpf_init2(dy, 256);
					mpf_init2(temp, 256); mpf_init2(fx2, 256); mpf_init2(fy2, 256);
					mpf_div_ui(scale, view.zoom, render_width);
					mpf_set_d(temp, mx - render_width / 2.0); mpf_mul(dx, temp, scale); mpf_add(fx2, view.cx, dx);
					mpf_set_d(temp, my - render_height / 2.0); mpf_mul(dy, temp, scale); mpf_add(fy2, view.cy, dy);
					if (e.wheel.y > 0) { mpf_mul_ui(view.zoom, view.zoom, 9); mpf_div_ui(view.zoom, view.zoom, 10); }
					else if (e.wheel.y < 0) { mpf_mul_ui(view.zoom, view.zoom, 10); mpf_div_ui(view.zoom, view.zoom, 9); }
					mpf_div_ui(scale, view.zoom, render_width);
					mpf_set_d(temp, mx - render_width / 2.0); mpf_mul(dx, temp, scale); mpf_sub(view.cx, fx2, dx);
					mpf_set_d(temp, my - render_height / 2.0); mpf_mul(dy, temp, scale); mpf_sub(view.cy, fy2, dy);
					mpf_clear(scale); mpf_clear(dx); mpf_clear(dy); mpf_clear(temp); mpf_clear(fx2); mpf_clear(fy2);
					pending_render = 1; last_wheel_time = SDL_GetTicks();
					if (render_state.active) abort_render = 1;
				}
			}
		}

		if (pending_resize && (SDL_GetTicks() - last_resize_time >= RESIZE_DEBOUNCE_MS)) {
			pending_resize = 0;
			if (resize_new_w != render_width || resize_new_h != render_height) {
				int old_w = render_width;
				render_state.active = 0; abort_render = 0;
				if (old_w > 0 && resize_new_w != old_w) {
					mpf_t ratio; mpf_init2(ratio, 256);
					mpf_set_ui(ratio, resize_new_w); mpf_div_ui(ratio, ratio, old_w);
					mpf_mul(view.zoom, view.zoom, ratio); mpf_clear(ratio);
				}
				set_render_size(resize_new_w, resize_new_h);
				free(pixels); pixels = (uint32_t*)calloc((size_t)render_width * render_height, sizeof(uint32_t));
				free(last_pixels); last_pixels = (uint32_t*)calloc((size_t)render_width * render_height, sizeof(uint32_t));
				SDL_DestroyTexture(tex); tex = SDL_CreateTexture(render_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, render_width, render_height);
				SDL_DestroyTexture(preview_tex); preview_tex = SDL_CreateTexture(render_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, render_width, render_height);
				have_last_render = 0; pending_render = 1; last_wheel_time = SDL_GetTicks() - WHEEL_DEBOUNCE_MS - 1;
			}
		}

		// --- ImGui frame ---
		ImGui_ImplSDLRenderer2_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
		ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
		ImGui::Begin("Mandelbrot Controls", nullptr,
			ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoBringToFrontOnFocus);

		char buf[128];
		mpf_to_str(view.cx, buf, sizeof(buf)); ImGui::Text("Center X: %s", buf);
		mpf_to_str(view.cy, buf, sizeof(buf)); ImGui::Text("Center Y: %s", buf);
		mpf_to_str(view.zoom, buf, sizeof(buf)); ImGui::Text("Zoom: %s", buf);

		bool geometry_changed = false;
		bool colour_changed = false;
		bool modifiers_changed = false;

		ImGui::Separator();
		ImGui::Text("Fractal Parameters");

		if (InputIntWheel("Iterations##maxiter", &ui_max_iter, 10)) {
			if (ui_max_iter < 100) ui_max_iter = 100;
			geometry_changed = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("×2")) { ui_max_iter *= 2; geometry_changed = true; }
		ImGui::SameLine();
		if (ImGui::Button("÷2")) { ui_max_iter /= 2; if (ui_max_iter < 1) ui_max_iter = 1; geometry_changed = true; }

		if (ImGui::SliderFloat("Max Radius", &ui_max_radius, 2.0f, 100.0f)) { geometry_changed = true; }
		if (ImGui::SliderInt("Recursion", &ui_recursion, 1, 10)) { geometry_changed = true; }

		if (InputDoubleWheel("Count Offset", &ui_count_offset, 0.1, 1.0)) { colour_changed = true; }

		const char* conditions[] = { "Addition", "Multiplication", "Subtraction", "Fluffy Cloud", "Foliage" };
		if (ImGui::Combo("End Condition", &ui_end_condition, conditions, IM_ARRAYSIZE(conditions))) { geometry_changed = true; }

		if (geometry_changed) {
			config.options.max_iter = ui_max_iter;
			config.options.maxRadius = ui_max_radius;
			config.options.recursionDepth = ui_recursion;
			config.options.endCondition = ui_end_condition;
			pending_render = 1; last_wheel_time = SDL_GetTicks();
			if (render_state.active) abort_render = 1;
		}
		if (colour_changed) {
			config.options.countOffset = ui_count_offset;
			if (have_last_render) redraw_palette(pixels, last_pixels, tex, preview_tex, render_ren);
		}

		ImGui::Separator();
		ImGui::Text("Calculation Modifiers");

		if (ImGui::Checkbox("Iterations##calciter", &ui_calc_iter)) modifiers_changed = true;
		ImGui::SameLine(); if (InputDoubleWheel("Coef##iter", &config.options.coefIterations, 0.01, 0.1)) modifiers_changed = true;
		if (ImGui::Checkbox("Displacement", &ui_calc_disp)) modifiers_changed = true;
		ImGui::SameLine(); if (InputDoubleWheel("Coef##disp", &config.options.coefDisplacement, 0.01, 0.1)) modifiers_changed = true;
		if (ImGui::Checkbox("Rotation", &ui_calc_rot)) modifiers_changed = true;
		ImGui::SameLine(); if (InputDoubleWheel("Coef##rot", &config.options.coefRotation, 0.01, 0.1)) modifiers_changed = true;
		if (ImGui::Checkbox("XOR Disp", &ui_calc_xor_disp)) modifiers_changed = true;
		ImGui::SameLine(); if (InputDoubleWheel("Coef##xor_disp", &config.options.coefDisplacementXOR, 0.01, 0.1)) modifiers_changed = true;
		if (ImGui::Checkbox("XOR Polar", &ui_calc_xor_polar)) modifiers_changed = true;
		ImGui::SameLine(); if (InputDoubleWheel("Coef##xor_polar", &config.options.coefPolarCoordXOR, 0.01, 0.1)) modifiers_changed = true;

		if (modifiers_changed) {
			config.options.calcIterations = ui_calc_iter ? 1 : 0;
			config.options.calcDisplacement = ui_calc_disp ? 1 : 0;
			config.options.calcRotation = ui_calc_rot ? 1 : 0;
			config.options.calcDisplacementXOR = ui_calc_xor_disp ? 1 : 0;
			config.options.calcPolarCoordXOR = ui_calc_xor_polar ? 1 : 0;
			if (have_last_render) redraw_palette(pixels, last_pixels, tex, preview_tex, render_ren);
		}

		ImGui::Separator();
		ImGui::Text("Palette Controls");

		if (ImGui::BeginTabBar("PaletteTabs")) {
			if (ImGui::BeginTabItem("Offset")) {
				if (ImGui::SliderFloat("Master", &ui_master_offset, -3.14f, 3.14f)) palette_changed = true;
				if (ImGui::SliderFloat("Red", &ui_red_offset, -3.14f, 3.14f)) palette_changed = true;
				if (ImGui::SliderFloat("Green", &ui_green_offset, -3.14f, 3.14f)) palette_changed = true;
				if (ImGui::SliderFloat("Blue", &ui_blue_offset, -3.14f, 3.14f)) palette_changed = true;
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Stagger")) {
				if (ImGui::Checkbox("Before Mods", &ui_stagger_before)) palette_changed = true;
				ImGui::SameLine();
				if (ImGui::Checkbox("After Mods", &ui_stagger_after)) palette_changed = true;

				if (ImGui::SliderFloat("Master", &ui_master_stagger, -3.14f, 3.14f)) palette_changed = true;
				if (ImGui::SliderFloat("Red", &ui_red_stagger, -3.14f, 3.14f)) palette_changed = true;
				if (ImGui::SliderFloat("Green", &ui_green_stagger, -3.14f, 3.14f)) palette_changed = true;
				if (ImGui::SliderFloat("Blue", &ui_blue_stagger, -3.14f, 3.14f)) palette_changed = true;

				ImGui::Separator();
				ImGui::Text("Bitmasks (binary strings)");
				if (ImGui::InputText("Red##mask", ui_mask_red, sizeof(ui_mask_red), ImGuiInputTextFlags_CharsDecimal)) palette_changed = true;
				if (ImGui::InputText("Green##mask", ui_mask_green, sizeof(ui_mask_green), ImGuiInputTextFlags_CharsDecimal)) palette_changed = true;
				if (ImGui::InputText("Blue##mask", ui_mask_blue, sizeof(ui_mask_blue), ImGuiInputTextFlags_CharsDecimal)) palette_changed = true;
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Period")) {
				if (InputDoubleWheel("Wave Period", &ui_wave_period, 1.0, 10.0)) palette_changed = true;
				ImGui::Separator();
				ImGui::Text("Multipliers:");
				if (InputDoubleWheel("Red##mult", &ui_red_period, 0.1, 1.0)) palette_changed = true;
				if (InputDoubleWheel("Green##mult", &ui_green_period, 0.1, 1.0)) palette_changed = true;
				if (InputDoubleWheel("Blue##mult", &ui_blue_period, 0.1, 1.0)) palette_changed = true;
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		if (palette_changed) {
			config.palette.masterOffset = ui_master_offset;
			config.palette.redOffset = ui_red_offset;
			config.palette.greenOffset = ui_green_offset;
			config.palette.blueOffset = ui_blue_offset;
			config.palette.masterStagger = ui_master_stagger;
			config.palette.redStagger = ui_red_stagger;
			config.palette.greenStagger = ui_green_stagger;
			config.palette.blueStagger = ui_blue_stagger;
			config.palette.colourWavePeriod = ui_wave_period;
			config.palette.redPeriod = ui_red_period;
			config.palette.greenPeriod = ui_green_period;
			config.palette.bluePeriod = ui_blue_period;
			config.palette.staggerBefore = ui_stagger_before ? 1 : 0;
			config.palette.staggerAfter = ui_stagger_after ? 1 : 0;

			config.palette.staggerMaskRed = (int)strtol(ui_mask_red, NULL, 2);
			config.palette.staggerMaskRedLen = strlen(ui_mask_red) > 0 ? strlen(ui_mask_red) : 1;
			config.palette.staggerMaskGreen = (int)strtol(ui_mask_green, NULL, 2);
			config.palette.staggerMaskGreenLen = strlen(ui_mask_green) > 0 ? strlen(ui_mask_green) : 1;
			config.palette.staggerMaskBlue = (int)strtol(ui_mask_blue, NULL, 2);
			config.palette.staggerMaskBlueLen = strlen(ui_mask_blue) > 0 ? strlen(ui_mask_blue) : 1;

			if (have_last_render) redraw_palette(pixels, last_pixels, tex, preview_tex, render_ren);
			palette_changed = false;
		}

		ImGui::End();
		ImGui::Render();

		// --- Fractal rendering ---
		if (render_state.active && abort_render) {
			render_state.active = 0;
			abort_render = 0;
		}

		if (!render_state.active) {
			if (pending_render && (SDL_GetTicks() - last_wheel_time >= WHEEL_DEBOUNCE_MS)) {
				pending_render = 0;
				begin_render(view.cx, view.cy, view.zoom, pixels, tex, render_ren, last_pixels, have_last_render, last_render_cx, last_render_cy, last_render_zoom);
			}
		}

		if (render_state.active) {
			int done = render_strip(pixels, tex, render_ren);
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

		// --- Present fractal window ---
		SDL_SetRenderDrawColor(render_ren, 0, 0, 0, 255);
		SDL_RenderClear(render_ren);
		bool in_scrolling = pending_render && ((SDL_GetTicks() - last_wheel_time) < WHEEL_DEBOUNCE_MS) && have_last_render;
		if (render_state.active || !in_scrolling) {
			SDL_RenderCopy(render_ren, tex, NULL, NULL);
		} else {
			double preview_s, preview_off_x, preview_off_y;
			if (compute_preview_transform(view.cx, view.cy, view.zoom, last_render_cx, last_render_cy, last_render_zoom, &preview_s, &preview_off_x, &preview_off_y)) {
				SDL_Rect src = { 0, 0, render_width, render_height };
				SDL_FRect dst = { (float)preview_off_x, (float)preview_off_y, (float)(render_width * preview_s), (float)(render_height * preview_s) };
				SDL_RenderCopyF(render_ren, preview_tex, &src, &dst);
			} else {
				SDL_RenderCopy(render_ren, tex, NULL, NULL);
			}
		}
		SDL_RenderPresent(render_ren);

		// --- Present UI window ---
		SDL_SetRenderDrawColor(ui_ren, 32, 32, 32, 255);
		SDL_RenderClear(ui_ren);
		ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), ui_ren);
		SDL_RenderPresent(ui_ren);

		SDL_Delay(5);
	}

	ImGui_ImplSDLRenderer2_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();

	mpf_clear(view.cx); mpf_clear(view.cy); mpf_clear(view.zoom);
	mpf_clear(last_render_cx); mpf_clear(last_render_cy); mpf_clear(last_render_zoom);
	mpf_clear(drag_start_cx); mpf_clear(drag_start_cy);
	free(pixels); free(last_pixels); free(g_results); free(g_counts);
	SDL_DestroyTexture(tex); SDL_DestroyTexture(preview_tex);
	SDL_DestroyRenderer(render_ren); SDL_DestroyWindow(render_win);
	SDL_DestroyRenderer(ui_ren); SDL_DestroyWindow(ui_win);
	SDL_Quit();
	return 0;
}
