#ifndef STATE_H
#define STATE_H

#define MANDELBROT_WEB_URL "http://weirdly.net/mandelbrot/"

#include <gmp.h>

#ifdef __cplusplus
extern "C" {
#endif

// Saves the view + config to a JSON file matching the JS app's schema.
int save_state(const char* filename, mpf_t cx, mpf_t cy, mpf_t zoom);

// Loads a view + config from a JSON file. Returns 1 on success.
int load_state(const char* filename, mpf_t cx, mpf_t cy, mpf_t zoom);

// Returns a malloc'd string of the form "?config=<base64-json>", suitable for
// pasting onto the end of mandelbrot.html's URL. Caller must free().
char* get_state_url(mpf_t cx, mpf_t cy, mpf_t zoom);

#ifdef __cplusplus
}
#endif

#endif
