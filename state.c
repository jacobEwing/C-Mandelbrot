#include "state.h"
#include "mandelbrot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <SDL2/SDL.h>
#include <png.h>
#include "cjson/cJSON.h"

/* ---- String/enum conversion helpers ---- */

static const char* ec_to_str(int ec) {
	switch (ec) {
		case 1: return "multiplication";
		case 2: return "subtraction";
		case 3: return "fluffyCloud";
		case 4: return "foliage";
		default: return "";
	}
}

static int ec_from_str(const char* s) {
	if (!s || !*s) return 0;
	if (strcmp(s, "multiplication") == 0) return 1;
	if (strcmp(s, "subtraction") == 0) return 2;
	if (strcmp(s, "fluffyCloud") == 0) return 3;
	if (strcmp(s, "foliage") == 0) return 4;
	return 0;
}

static void int_to_bin_str(int val, int len, char* out, int maxlen) {
	if (len <= 0) { out[0] = '0'; out[1] = '\0'; return; }
	if (len >= maxlen) len = maxlen - 1;
	for (int i = 0; i < len; i++) {
		out[i] = (val & (1 << (len - 1 - i))) ? '1' : '0';
	}
	out[len] = '\0';
}

/* ---- cJSON typed getters with defaults ---- */

static double get_num(const cJSON* parent, const char* name, double dflt) {
	cJSON* item = cJSON_GetObjectItemCaseSensitive(parent, name);
	if (!item) return dflt;
	if (cJSON_IsNumber(item)) return item->valuedouble;
	if (cJSON_IsString(item) && item->valuestring) return atof(item->valuestring);
	return dflt;
}

static int get_int(const cJSON* parent, const char* name, int dflt) {
	cJSON* item = cJSON_GetObjectItemCaseSensitive(parent, name);
	if (!item) return dflt;
	if (cJSON_IsNumber(item)) return (int)item->valuedouble;
	if (cJSON_IsString(item) && item->valuestring) return atoi(item->valuestring);
	return dflt;
}

static const char* get_str(const cJSON* parent, const char* name, const char* dflt) {
	cJSON* item = cJSON_GetObjectItemCaseSensitive(parent, name);
	if (!item || !cJSON_IsString(item) || !item->valuestring) return dflt;
	return item->valuestring;
}

/* ---- Base64 encoder (RFC 4648, no line breaks, standard alphabet) ---- */

static const char b64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char* base64_encode(const unsigned char* data, size_t len) {
	size_t out_len = 4 * ((len + 2) / 3);
	char* out = malloc(out_len + 1);
	if (!out) return NULL;
	size_t i = 0, j = 0;
	while (i < len) {
		unsigned int b0 = (i < len) ? data[i++] : 0;
		unsigned int b1 = (i < len) ? data[i++] : 0;
		unsigned int b2 = (i < len) ? data[i++] : 0;
		unsigned int triple = (b0 << 16) | (b1 << 8) | b2;
		out[j++] = b64_alphabet[(triple >> 18) & 0x3F];
		out[j++] = b64_alphabet[(triple >> 12) & 0x3F];
		out[j++] = b64_alphabet[(triple >> 6) & 0x3F];
		out[j++] = b64_alphabet[triple & 0x3F];
	}
	int pad = (int)((3 - (len % 3)) % 3);
	for (int p = 0; p < pad; p++) out[out_len - 1 - p] = '=';
	out[out_len] = '\0';
	return out;
}

/* ---- JSON tree builder ---- */

static cJSON* build_state_tree(mpf_t cx, mpf_t cy, mpf_t zoom) {
	cJSON* root = cJSON_CreateObject();
	if (!root) return NULL;

	char buf[256];

	/* --- map --- */
	cJSON* map = cJSON_CreateObject();
	cJSON_AddItemToObject(root, "map", map);
	cJSON_AddNumberToObject(map, "x", mpf_get_d(cx));
	cJSON_AddNumberToObject(map, "y", mpf_get_d(cy));
	cJSON_AddNumberToObject(map, "width", mpf_get_d(zoom));
	cJSON_AddNumberToObject(map, "height", mpf_get_d(zoom));
	cJSON_AddNumberToObject(map, "accuracy", config.options.max_iter);
	/* _precise fields preserve GMP precision for native round-trips.
	   The web app ignores unknown fields, so these are harmless there. */
	gmp_snprintf(buf, sizeof(buf), "%.80Ff", cx);
	cJSON_AddStringToObject(map, "x_precise", buf);
	gmp_snprintf(buf, sizeof(buf), "%.80Ff", cy);
	cJSON_AddStringToObject(map, "y_precise", buf);
	gmp_snprintf(buf, sizeof(buf), "%.80Ff", zoom);
	cJSON_AddStringToObject(map, "zoom_precise", buf);
	cJSON_AddNumberToObject(map, "rotation", config.viewRotation);
	
	/* --- palette --- */
	cJSON* palette = cJSON_CreateObject();
	cJSON_AddItemToObject(root, "palette", palette);
	cJSON_AddNumberToObject(palette, "colourWavePeriod", config.palette.colourWavePeriod);

	cJSON* red = cJSON_CreateObject();
	cJSON_AddItemToObject(palette, "red", red);
	cJSON_AddNumberToObject(red, "offset", config.palette.redOffset);
	cJSON_AddNumberToObject(red, "stagger", config.palette.redStagger);
	cJSON_AddNumberToObject(red, "period", config.palette.redPeriod);

	cJSON* green = cJSON_CreateObject();
	cJSON_AddItemToObject(palette, "green", green);
	cJSON_AddNumberToObject(green, "offset", config.palette.greenOffset);
	cJSON_AddNumberToObject(green, "stagger", config.palette.greenStagger);
	cJSON_AddNumberToObject(green, "period", config.palette.greenPeriod);

	cJSON* blue = cJSON_CreateObject();
	cJSON_AddItemToObject(palette, "blue", blue);
	cJSON_AddNumberToObject(blue, "offset", config.palette.blueOffset);
	cJSON_AddNumberToObject(blue, "stagger", config.palette.blueStagger);
	cJSON_AddNumberToObject(blue, "period", config.palette.bluePeriod);

	cJSON* master = cJSON_CreateObject();
	cJSON_AddItemToObject(palette, "master", master);
	cJSON_AddNumberToObject(master, "offset", config.palette.masterOffset);
	cJSON_AddNumberToObject(master, "stagger", config.palette.masterStagger);

	cJSON* mask = cJSON_CreateObject();
	cJSON_AddItemToObject(palette, "staggerMask", mask);
	int_to_bin_str(config.palette.staggerMaskRed, config.palette.staggerMaskRedLen, buf, sizeof(buf));
	cJSON_AddStringToObject(mask, "red", buf);
	int_to_bin_str(config.palette.staggerMaskGreen, config.palette.staggerMaskGreenLen, buf, sizeof(buf));
	cJSON_AddStringToObject(mask, "green", buf);
	int_to_bin_str(config.palette.staggerMaskBlue, config.palette.staggerMaskBlueLen, buf, sizeof(buf));
	cJSON_AddStringToObject(mask, "blue", buf);

	/* --- options --- */
	cJSON* options = cJSON_CreateObject();
	cJSON_AddItemToObject(root, "options", options);

	cJSON* flags = cJSON_CreateObject();
	cJSON_AddItemToObject(options, "calculationFlags", flags);
	cJSON_AddNumberToObject(flags, "iterations", config.options.calcIterations);
	cJSON_AddNumberToObject(flags, "displacement", config.options.calcDisplacement);
	cJSON_AddNumberToObject(flags, "rotation", config.options.calcRotation);
	cJSON_AddNumberToObject(flags, "displacementXOR", config.options.calcDisplacementXOR);
	cJSON_AddNumberToObject(flags, "polarCoordXOR", config.options.calcPolarCoordXOR);

	cJSON* coefs = cJSON_CreateObject();
	cJSON_AddItemToObject(options, "coefficients", coefs);
	cJSON_AddNumberToObject(coefs, "iterations", config.options.coefIterations);
	cJSON_AddNumberToObject(coefs, "displacement", config.options.coefDisplacement);
	cJSON_AddNumberToObject(coefs, "rotation", config.options.coefRotation);
	cJSON_AddNumberToObject(coefs, "displacementXOR", config.options.coefDisplacementXOR);
	cJSON_AddNumberToObject(coefs, "polarCoordXOR", config.options.coefPolarCoordXOR);

	cJSON_AddStringToObject(options, "endCondition", ec_to_str(config.options.endCondition));
	cJSON_AddNumberToObject(options, "maxRadius", config.options.maxRadius);
	cJSON_AddNumberToObject(options, "staggerBefore", config.palette.staggerBefore);
	cJSON_AddNumberToObject(options, "staggerAfter", config.palette.staggerAfter);
	cJSON_AddNumberToObject(options, "recursionDepth", config.options.recursionDepth);
	cJSON_AddNumberToObject(options, "countOffset", config.options.countOffset);

	return root;
}

/* ---- JSON tree parser ---- */

static int parse_state_tree(cJSON* root, mpf_t cx, mpf_t cy, mpf_t zoom) {
	if (!root) return 0;

	cJSON* map = cJSON_GetObjectItemCaseSensitive(root, "map");
	if (map) {
		const char* xs = get_str(map, "x_precise", NULL);
		const char* ys = get_str(map, "y_precise", NULL);
		const char* zs = get_str(map, "zoom_precise", NULL);

		if (xs) mpf_set_str(cx, xs, 10);
		else mpf_set_d(cx, get_num(map, "x", 0));
		if (ys) mpf_set_str(cy, ys, 10);
		else mpf_set_d(cy, get_num(map, "y", 0));
		if (zs) mpf_set_str(zoom, zs, 10);
		else mpf_set_d(zoom, get_num(map, "width", 4.0));

		config.options.max_iter = get_int(map, "accuracy", config.options.max_iter);
		config.viewRotation = get_num(map, "rotation", config.viewRotation);
	}

	cJSON* palette = cJSON_GetObjectItemCaseSensitive(root, "palette");
	if (palette) {
		config.palette.colourWavePeriod = get_num(palette, "colourWavePeriod", config.palette.colourWavePeriod);

		cJSON* red = cJSON_GetObjectItemCaseSensitive(palette, "red");
		if (red) {
			config.palette.redOffset = get_num(red, "offset", config.palette.redOffset);
			config.palette.redStagger = get_num(red, "stagger", config.palette.redStagger);
			config.palette.redPeriod = get_num(red, "period", config.palette.redPeriod);
		}
		cJSON* green = cJSON_GetObjectItemCaseSensitive(palette, "green");
		if (green) {
			config.palette.greenOffset = get_num(green, "offset", config.palette.greenOffset);
			config.palette.greenStagger = get_num(green, "stagger", config.palette.greenStagger);
			config.palette.greenPeriod = get_num(green, "period", config.palette.greenPeriod);
		}
		cJSON* blue = cJSON_GetObjectItemCaseSensitive(palette, "blue");
		if (blue) {
			config.palette.blueOffset = get_num(blue, "offset", config.palette.blueOffset);
			config.palette.blueStagger = get_num(blue, "stagger", config.palette.blueStagger);
			config.palette.bluePeriod = get_num(blue, "period", config.palette.bluePeriod);
		}
		cJSON* master = cJSON_GetObjectItemCaseSensitive(palette, "master");
		if (master) {
			config.palette.masterOffset = get_num(master, "offset", config.palette.masterOffset);
			config.palette.masterStagger = get_num(master, "stagger", config.palette.masterStagger);
		}

		cJSON* mask = cJSON_GetObjectItemCaseSensitive(palette, "staggerMask");
		if (mask) {
			const char* r = get_str(mask, "red", "10");
			config.palette.staggerMaskRed = (int)strtol(r, NULL, 2);
			config.palette.staggerMaskRedLen = strlen(r) > 0 ? (int)strlen(r) : 1;
			const char* g = get_str(mask, "green", "10");
			config.palette.staggerMaskGreen = (int)strtol(g, NULL, 2);
			config.palette.staggerMaskGreenLen = strlen(g) > 0 ? (int)strlen(g) : 1;
			const char* b = get_str(mask, "blue", "10");
			config.palette.staggerMaskBlue = (int)strtol(b, NULL, 2);
			config.palette.staggerMaskBlueLen = strlen(b) > 0 ? (int)strlen(b) : 1;
		}
	}

	cJSON* options = cJSON_GetObjectItemCaseSensitive(root, "options");
	if (options) {
		cJSON* flags = cJSON_GetObjectItemCaseSensitive(options, "calculationFlags");
		if (flags) {
			config.options.calcIterations = get_int(flags, "iterations", config.options.calcIterations);
			config.options.calcDisplacement = get_int(flags, "displacement", config.options.calcDisplacement);
			config.options.calcRotation = get_int(flags, "rotation", config.options.calcRotation);
			config.options.calcDisplacementXOR = get_int(flags, "displacementXOR", config.options.calcDisplacementXOR);
			config.options.calcPolarCoordXOR = get_int(flags, "polarCoordXOR", config.options.calcPolarCoordXOR);
		}
		cJSON* coefs = cJSON_GetObjectItemCaseSensitive(options, "coefficients");
		if (coefs) {
			config.options.coefIterations = get_num(coefs, "iterations", config.options.coefIterations);
			config.options.coefDisplacement = get_num(coefs, "displacement", config.options.coefDisplacement);
			config.options.coefRotation = get_num(coefs, "rotation", config.options.coefRotation);
			config.options.coefDisplacementXOR = get_num(coefs, "displacementXOR", config.options.coefDisplacementXOR);
			config.options.coefPolarCoordXOR = get_num(coefs, "polarCoordXOR", config.options.coefPolarCoordXOR);
		}
		config.options.endCondition = ec_from_str(get_str(options, "endCondition", ""));
		config.options.maxRadius = get_num(options, "maxRadius", config.options.maxRadius);
		config.palette.staggerBefore = get_int(options, "staggerBefore", config.palette.staggerBefore);
		config.palette.staggerAfter = get_int(options, "staggerAfter", config.palette.staggerAfter);
		config.options.recursionDepth = get_int(options, "recursionDepth", config.options.recursionDepth);
		config.options.countOffset = get_num(options, "countOffset", config.options.countOffset);
	}

	return 1;
}

/* ---- Save directory ---- */

char* get_save_dir(void) {
	const char* home = getenv("HOME");
	if (!home || !*home) return NULL;
	size_t len = strlen(home) + 32;
	char* dir = malloc(len);
	if (!dir) return NULL;
	snprintf(dir, len, "%s/.mandelbrot", home);
	mkdir(dir, 0755);   /* ignore EEXIST */
	return dir;
}

/* ---- Image renderer (three-way dispatch, matches the main renderer) ---- */
void render_image(uint32_t* pixels, int width, int height, ViewState* view) {
	double zoom_d = mpf_get_d(view->zoom);
	double aspect = (double)height / (double)width;

	int use_perturbation = (zoom_d < 1e-28)
		&& (config.options.endCondition == 0)
		&& (config.options.recursionDepth == 1);
	int use_df64 = (zoom_d < 1e-12) && !use_perturbation;

	double theta = config.viewRotation;
	double ct = cos(theta), st = sin(theta);

	mpf_t step_x_mpf, step_y_mpf, aspect_mpf;
	mpf_init2(step_x_mpf, 256);
	mpf_init2(step_y_mpf, 256);
	mpf_init2(aspect_mpf, 256);
	mpf_div_ui(step_x_mpf, view->zoom, width);
	mpf_set_d(aspect_mpf, aspect);
	mpf_mul(step_y_mpf, view->zoom, aspect_mpf);
	mpf_div_ui(step_y_mpf, step_y_mpf, height);
	df64 step_x = mpf_to_df64(step_x_mpf);
	df64 step_y = mpf_to_df64(step_y_mpf);
	mpf_clear(step_x_mpf); mpf_clear(step_y_mpf); mpf_clear(aspect_mpf);

	if (use_perturbation) {
		compute_reference_orbit(view->cx, view->cy, view->zoom);
		df64 ref_off_x = ref.offset_x;
		df64 ref_off_y = ref.offset_y;

		#pragma omp parallel for schedule(dynamic, 4)
		for (int y = 0; y < height; y++) {
			df64 yi = df_mul_d(step_y, (double)(y - height / 2));
			df64 dc_row_base = df_add(df_mul_d(yi, -st), ref_off_x);
			df64 dci_row_base = df_add(df_mul_d(yi, ct), ref_off_y);
			for (int x = 0; x < width; x++) {
				df64 xi = df_mul_d(step_x, (double)(x - width / 2));
				df64 dc  = df_add(df_mul_d(xi, ct), dc_row_base);
				df64 dci = df_add(df_mul_d(xi, st), dci_row_base);
				Result r = mandelbrot_perturbation(dc, dci, config.options.max_iter);
				pixels[y * width + x] = create_colour(&r);
			}
		}
	} else if (use_df64) {
		df64 cx_df = mpf_to_df64(view->cx);
		df64 cy_df = mpf_to_df64(view->cy);

		#pragma omp parallel for schedule(dynamic, 4)
		for (int y = 0; y < height; y++) {
			df64 yi = df_mul_d(step_y, (double)(y - height / 2));
			df64 cr_row_base = df_add(cx_df, df_mul_d(yi, -st));
			df64 ci_row_base = df_add(cy_df, df_mul_d(yi, ct));
			for (int x = 0; x < width; x++) {
				df64 xi = df_mul_d(step_x, (double)(x - width / 2));
				df64 cr_df = df_add(cr_row_base, df_mul_d(xi, ct));
				df64 ci_df = df_add(ci_row_base, df_mul_d(xi, st));
				Result r = mandelbrot_calc_df64(cr_df, ci_df,
				                                config.options.max_iter,
				                                config.options.recursionDepth);
				pixels[y * width + x] = create_colour(&r);
			}
		}
	} else {
		double cx_d = mpf_get_d(view->cx);
		double cy_d = mpf_get_d(view->cy);
		double width_d  = zoom_d;
		double height_d = width_d * aspect;
		double dx_step  = width_d  / width;
		double dy_step  = height_d / height;

		#pragma omp parallel for schedule(dynamic, 4)
		for (int y = 0; y < height; y++) {
			double yi = ((double)y - height / 2.0) * dy_step;
			double cr_row_base = cx_d - yi * st;
			double ci_row_base = cy_d + yi * ct;
			for (int x = 0; x < width; x++) {
				double xi = ((double)x - width / 2.0) * dx_step;
				double cr = cr_row_base + xi * ct;
				double ci = ci_row_base + xi * st;
				Result r = mandelbrot_calc(cr, ci, config.options.max_iter,
				                           config.options.recursionDepth);
				pixels[y * width + x] = create_colour(&r);
			}
		}
	}
}
/* ---- PNG save with metadata ---- */
static void resolve_save_path(const char* name_or_path, char* path, size_t path_size) {
	if (name_or_path[0] == '/' || (name_or_path[0] == '.' && name_or_path[1] == '/')) {
		snprintf(path, path_size, "%s", name_or_path);
	} else {
		char* dir = get_save_dir();
		if (dir) {
			snprintf(path, path_size, "%s/%s", dir, name_or_path);
			free(dir);
		} else {
			snprintf(path, path_size, "%s", name_or_path);
		}
	}
	size_t plen = strlen(path);
	if (plen < 4 || strcasecmp(path + plen - 4, ".png") != 0) {
		if (plen + 5 < path_size) {
			strcat(path, ".png");
		}
	}
}

int write_png_with_metadata(const char* path, const uint32_t* pixels,
                            int width, int height, ViewState* view) {
	cJSON* root = build_state_tree(view->cx, view->cy, view->zoom);
	if (!root) return 0;
	char* json_str = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (!json_str) return 0;

	FILE* fp = fopen(path, "wb");
	if (!fp) { free(json_str); return 0; }

	png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	png_infop info = png ? png_create_info_struct(png) : NULL;
	if (!png || !info) {
		if (png) png_destroy_write_struct(&png, NULL);
		fclose(fp); free(json_str);
		return 0;
	}
	if (setjmp(png_jmpbuf(png))) {
		png_destroy_write_struct(&png, &info);
		fclose(fp); free(json_str);
		return 0;
	}
	png_init_io(png, fp);
	png_set_IHDR(png, info, width, height, 8,
		PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	png_text text;
	memset(&text, 0, sizeof(text));
	text.compression = PNG_ITXT_COMPRESSION_NONE;
	text.key = (png_charp)"MandelbrotConfig";
	text.text = json_str;
	text.text_length = strlen(json_str);
	text.itxt_length = strlen(json_str);
	text.lang = (png_charp)"en";
	text.lang_key = (png_charp)"";
	png_set_text(png, info, &text, 1);

	png_write_info(png, info);

	uint8_t* row = malloc((size_t)width * 3);
	if (!row) {
		png_destroy_write_struct(&png, &info);
		fclose(fp); free(json_str);
		return 0;
	}
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			uint32_t p = pixels[y * width + x];
			row[x * 3 + 0] = (p >> 16) & 0xFF;
			row[x * 3 + 1] = (p >> 8) & 0xFF;
			row[x * 3 + 2] = p & 0xFF;
		}
		png_write_row(png, row);
	}
	free(row);

	png_write_end(png, NULL);
	png_destroy_write_struct(&png, &info);
	fclose(fp);
	free(json_str);
	return 1;
}

int save_render_png(const char* name_or_path, mpf_t cx, mpf_t cy, mpf_t zoom,
                    int width, int height) {
	if (width < 1) width = 1;
	if (height < 1) height = 1;

	char path[1024];
	resolve_save_path(name_or_path, path, sizeof(path));

	ViewState view;
	mpf_init2(view.cx, 256);
	mpf_init2(view.cy, 256);
	mpf_init2(view.zoom, 256);
	mpf_set(view.cx, cx);
	mpf_set(view.cy, cy);
	mpf_set(view.zoom, zoom);

	uint32_t* pixels = malloc((size_t)width * height * 4);
	if (!pixels) {
		mpf_clear(view.cx); mpf_clear(view.cy); mpf_clear(view.zoom);
		return 0;
	}
	render_image(pixels, width, height, &view);
	int ok = write_png_with_metadata(path, pixels, width, height, &view);
	free(pixels);

	mpf_clear(view.cx); mpf_clear(view.cy); mpf_clear(view.zoom);
	return ok;
}

/* ---- Load config from PNG metadata ---- */

static int load_png_config(const char* path, mpf_t cx, mpf_t cy, mpf_t zoom) {
	FILE* fp = fopen(path, "rb");
	if (!fp) return 0;
	uint8_t sig[8];
	if (fread(sig, 1, 8, fp) != 8 || png_sig_cmp(sig, 0, 8) != 0) { fclose(fp); return 0; }
	rewind(fp);

	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	png_infop info = png ? png_create_info_struct(png) : NULL;
	if (!png || !info) { if (png) png_destroy_read_struct(&png, NULL, NULL); fclose(fp); return 0; }
	if (setjmp(png_jmpbuf(png))) { png_destroy_read_struct(&png, &info, NULL); fclose(fp); return 0; }

	png_init_io(png, fp);
	png_read_info(png, info);

	png_textp text_ptr;
	int num_text = 0;
	png_get_text(png, info, &text_ptr, &num_text);

	int ok = 0;
	for (int i = 0; i < num_text; i++) {
		if (strcmp(text_ptr[i].key, "MandelbrotConfig") == 0) {
			cJSON* root = cJSON_Parse(text_ptr[i].text);
			if (root) {
				ok = parse_state_tree(root, cx, cy, zoom);
				cJSON_Delete(root);
			}
			break;
		}
	}

	png_destroy_read_struct(&png, &info, NULL);
	fclose(fp);
	return ok;
}

/* ---- JSON file load (fallback) ---- */

static int load_json_file(const char* path, mpf_t cx, mpf_t cy, mpf_t zoom) {
	FILE* f = fopen(path, "r");
	if (!f) return 0;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz <= 0 || sz > 1024 * 1024) { fclose(f); return 0; }
	char* buf = malloc(sz + 1);
	if (!buf) { fclose(f); return 0; }
	if (fread(buf, 1, sz, f) != (size_t)sz) { free(buf); fclose(f); return 0; }
	buf[sz] = '\0';
	fclose(f);

	cJSON* root = cJSON_Parse(buf);
	free(buf);
	if (!root) return 0;
	int ok = parse_state_tree(root, cx, cy, zoom);
	cJSON_Delete(root);
	return ok;
}

/* ---- Dispatcher ---- */

int load_render(const char* name_or_path, mpf_t cx, mpf_t cy, mpf_t zoom) {
	/* Fresh default config so missing PNG fields (e.g. rotation on pre-rotation
	   files) resolve to defaults, not stale UI state. */
	reset_config();

	char path[2048];
	struct stat st;
	int found = 0;

	/* 1. Absolute or explicit relative path */
	if (name_or_path[0] == '/' || name_or_path[0] == '.') {
		if (stat(name_or_path, &st) == 0) {
			snprintf(path, sizeof(path), "%s", name_or_path);
			found = 1;
		}
	}

	/* 2. Try in cwd: as-is, then .png, then .json */
	if (!found) {
		if (stat(name_or_path, &st) == 0) {
			snprintf(path, sizeof(path), "%s", name_or_path);
			found = 1;
		}
		if (!found) {
			snprintf(path, sizeof(path), "%s.png", name_or_path);
			if (stat(path, &st) == 0) found = 1;
		}
		if (!found) {
			snprintf(path, sizeof(path), "%s.json", name_or_path);
			if (stat(path, &st) == 0) found = 1;
		}
	}

	/* 3. Fall back to save_dir: as-is, then .png, then .json */
	if (!found) {
		char* dir = get_save_dir();
		if (!dir) return 0;
		snprintf(path, sizeof(path), "%s/%s", dir, name_or_path);
		if (stat(path, &st) == 0) found = 1;
		if (!found) {
			snprintf(path, sizeof(path), "%s/%s.png", dir, name_or_path);
			if (stat(path, &st) == 0) found = 1;
		}
		if (!found) {
			snprintf(path, sizeof(path), "%s/%s.json", dir, name_or_path);
			if (stat(path, &st) == 0) found = 1;
		}
		free(dir);
	}

	if (!found) return 0;

	const char* ext = strrchr(path, '.');
	if (ext && strcasecmp(ext, ".png") == 0) return load_png_config(path, cx, cy, zoom);
	return load_json_file(path, cx, cy, zoom);
}

/* ---- Gallery: list saved PNG basenames ---- */

char** list_saved_renders(int* count) {
	*count = 0;
	char* dir = get_save_dir();
	if (!dir) return NULL;

	DIR* d = opendir(dir);
	if (!d) { free(dir); return NULL; }

	char** names = NULL;
	int cap = 0;
	struct dirent* ent;
	while ((ent = readdir(d)) != NULL) {
		size_t nlen = strlen(ent->d_name);
		if (nlen < 5) continue;
		if (strcasecmp(ent->d_name + nlen - 4, ".png") != 0) continue;

		if (*count >= cap) {
			cap = cap ? cap * 2 : 16;
			char** tmp = realloc(names, cap * sizeof(char*));
			if (!tmp) break;
			names = tmp;
		}
		names[*count] = strdup(ent->d_name);
		(*count)++;
	}
	closedir(d);
	free(dir);
	return names;
}

/* ---- Load a thumbnail PNG as an SDL_Texture ---- */

SDL_Texture* load_thumbnail_texture(SDL_Renderer* ren, const char* name) {
	char* dir = get_save_dir();
	if (!dir) return NULL;
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s.png", dir, name);
	free(dir);

	FILE* fp = fopen(path, "rb");
	if (!fp) return NULL;
	uint8_t sig[8];
	if (fread(sig, 1, 8, fp) != 8 || png_sig_cmp(sig, 0, 8) != 0) { fclose(fp); return NULL; }
	rewind(fp);

	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	png_infop info = png ? png_create_info_struct(png) : NULL;
	if (!png || !info) { if (png) png_destroy_read_struct(&png, NULL, NULL); fclose(fp); return NULL; }
	if (setjmp(png_jmpbuf(png))) { png_destroy_read_struct(&png, &info, NULL); fclose(fp); return NULL; }

	png_init_io(png, fp);
	png_read_info(png, info);

	png_uint_32 w = png_get_image_width(png, info);
	png_uint_32 h = png_get_image_height(png, info);
	int color_type = png_get_color_type(png, info);
	int bit_depth = png_get_bit_depth(png, info);

	if (bit_depth == 16) png_set_strip_16(png);
	if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
	if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
	if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
	if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE) {
		png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
	}
	if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
		png_set_gray_to_rgb(png);
	}
	png_read_update_info(png, info);

	size_t rowbytes = png_get_rowbytes(png, info);
	uint8_t* data = malloc(rowbytes * h);
	png_bytep* rows = malloc(sizeof(png_bytep) * h);
	for (png_uint_32 y = 0; y < h; y++) rows[y] = data + y * rowbytes;
	png_read_image(png, rows);
	free(rows);

	png_destroy_read_struct(&png, &info, NULL);
	fclose(fp);

	SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
	if (tex) SDL_UpdateTexture(tex, NULL, data, (int)rowbytes);
	free(data);
	return tex;
}

/* ---- URL for the web app ---- */

char* get_state_url(mpf_t cx, mpf_t cy, mpf_t zoom) {
	cJSON* root = build_state_tree(cx, cy, zoom);
	if (!root) return NULL;
	char* json_str = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (!json_str) return NULL;

	char* b64 = base64_encode((const unsigned char*)json_str, strlen(json_str));
	free(json_str);
	if (!b64) return NULL;

	size_t url_len = strlen(b64) + strlen(MANDELBROT_WEB_URL) + 16;
	char* url = malloc(url_len);
	if (url) snprintf(url, url_len, MANDELBROT_WEB_URL "?config=%s", b64);
	free(b64);
	return url;
}

/* ---- Delete ---- */

int delete_saved_render(const char* name) {
	char* dir = get_save_dir();
	if (!dir) return 0;
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s.png", dir, name);
	free(dir);
	return remove(path) == 0;
}
