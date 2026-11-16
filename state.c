#include "state.h"
#include "mandelbrot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cjson/cJSON.h"

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

// Builds the full config JSON tree. Caller must cJSON_Delete() the result.
static cJSON* build_state_tree(mpf_t cx, mpf_t cy, mpf_t zoom) {
	cJSON* root = cJSON_CreateObject();
	if (!root) return NULL;

	char buf[256];

	// --- map ---
	cJSON* map = cJSON_CreateObject();
	cJSON_AddItemToObject(root, "map", map);
	cJSON_AddNumberToObject(map, "x", mpf_get_d(cx));
	cJSON_AddNumberToObject(map, "y", mpf_get_d(cy));
	cJSON_AddNumberToObject(map, "width", mpf_get_d(zoom));
	cJSON_AddNumberToObject(map, "height", mpf_get_d(zoom));
	cJSON_AddNumberToObject(map, "accuracy", config.options.max_iter);
	// _precise fields preserve GMP precision for native round-trips
	gmp_snprintf(buf, sizeof(buf), "%.80Ff", cx);
	cJSON_AddStringToObject(map, "x_precise", buf);
	gmp_snprintf(buf, sizeof(buf), "%.80Ff", cy);
	cJSON_AddStringToObject(map, "y_precise", buf);
	gmp_snprintf(buf, sizeof(buf), "%.80Ff", zoom);
	cJSON_AddStringToObject(map, "zoom_precise", buf);

	// --- palette ---
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

	// --- options ---
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

int save_state(const char* filename, mpf_t cx, mpf_t cy, mpf_t zoom) {
	cJSON* root = build_state_tree(cx, cy, zoom);
	if (!root) return 0;
	char* json_str = cJSON_Print(root);
	cJSON_Delete(root);
	if (!json_str) return 0;

	FILE* f = fopen(filename, "w");
	int ok = 0;
	if (f) {
		fputs(json_str, f);
		fclose(f);
		ok = 1;
	}
	free(json_str);
	return ok;
}

int load_state(const char* filename, mpf_t cx, mpf_t cy, mpf_t zoom) {
	FILE* f = fopen(filename, "r");
	if (!f) return 0;

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size <= 0 || size > 1024 * 1024) { fclose(f); return 0; }

	char* buf = malloc(size + 1);
	if (!buf) { fclose(f); return 0; }
	if (fread(buf, 1, size, f) != (size_t)size) { free(buf); fclose(f); return 0; }
	buf[size] = '\0';
	fclose(f);

	cJSON* root = cJSON_Parse(buf);
	free(buf);
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

	cJSON_Delete(root);
	return 1;
}

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
