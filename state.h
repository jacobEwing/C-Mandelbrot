#ifndef STATE_H
#define STATE_H

#include <stdint.h>
#include <gmp.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MANDELBROT_WEB_URL "http://weirdly.net/mandelbrot/"
#define SAVE_PNG_SIZE 256

// Returns the save directory (~/.mandelbrot/), creating it if missing.
// Caller must free() the returned string.
char* get_save_dir(void);

// Saves the current view + config as a PNG with metadata.
// Path is <save_dir>/<name>.png  (auto-appends .png if not present)
int save_render_png(const char* name_or_path, mpf_t cx, mpf_t cy, mpf_t zoom, int width, int height);

// Loads a saved render. Tries <save_dir>/<name>.png then <name>.json,
// and also accepts an absolute path with the .png or .json extension.
int load_render(const char* name_or_path, mpf_t cx, mpf_t cy, mpf_t zoom);

// Returns a malloc'd URL of the form "<MANDELBROT_WEB_URL>?config=<base64-json>".
char* get_state_url(mpf_t cx, mpf_t cy, mpf_t zoom);

// Gallery: returns malloc'd array of PNG basenames (without extension).
// *count is set to the array length. Each element must be free()'d by caller.
char** list_saved_renders(int* count);

// Loads a PNG file as an SDL_Texture for display in the gallery.
// Returns NULL on failure. Texture ownership passes to the caller.
struct SDL_Texture;
struct SDL_Renderer;
struct SDL_Texture* load_thumbnail_texture(struct SDL_Renderer* ren, const char* name);

// Removes a saved PNG by basename. Returns 1 on success.
int delete_saved_render(const char* name);

#ifdef __cplusplus
}
#endif

#endif
