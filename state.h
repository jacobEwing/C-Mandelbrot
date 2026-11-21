#ifndef STATE_H
#define STATE_H

#include <stdint.h>
#include <gmp.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MANDELBROT_WEB_URL "http://weirdly.net/mandelbrot/"
#define SAVE_PNG_SIZE 256

typedef struct {
	mpf_t cx;
	mpf_t cy;
	mpf_t zoom;
} ViewState;

char* get_save_dir(void);

// Renders the fractal at the given view into an ARGB buffer (width*height uint32_t).
// Reads the global `config` for iteration limits / palette / modifiers.
void render_image(uint32_t* pixels, int width, int height, ViewState* view);

// Writes a PNG to `path` (verbatim, no path rewriting) with embedded metadata
// matching the current global `config` and `view`. Returns 1 on success.
int write_png_with_metadata(const char* path, const uint32_t* pixels,
                            int width, int height, ViewState* view);

// Convenience wrapper: resolves path (save_dir prefix + .png), renders, writes.
int save_render_png(const char* name_or_path, mpf_t cx, mpf_t cy, mpf_t zoom,
                    int width, int height);

int load_render(const char* name_or_path, mpf_t cx, mpf_t cy, mpf_t zoom);
char* get_state_url(mpf_t cx, mpf_t cy, mpf_t zoom);
char** list_saved_renders(int* count);

struct SDL_Texture;
struct SDL_Renderer;
struct SDL_Texture* load_thumbnail_texture(struct SDL_Renderer* ren, const char* name);

int delete_saved_render(const char* name);

#ifdef __cplusplus
}
#endif

#endif
