#ifndef ANIM_H
#define ANIM_H

#include "state.h"
#include "mandelbrot.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	RenderConfig config;
	ViewState view;
} AnimKeyframe;

typedef struct {
	const char* output_path;
	int width, height;
	int fps;

	// timing (0 = unset)
	int total_frames;
	double duration;
	int frames_per_transition;

	// interpolation
	int interp_mode;     // 0=linear, 1=catmull-rom
	int easing;          // 0=linear,1=ease-in,2=ease-out,3=ease-in-out,4=smoothstep
	int via_bounce;      // 0=none, 1=bounce
	double bounce_factor;
	double bounce_min;

	int discrete_policy; // 0=cut, 1=crossfade, 2=hold-a, 3=hold-b

	// encoding
	int codec;           // 0=h264, 1=vp9
	int crf;
	int pixfmt;          // 0=yuv420p, 1=yuv444p

	int jobs;
	int keep_frames;
	int overwrite;
	int ignore_bad;

	int dry_run;
	int render_only;
	int single_frame;    // -1 = unset

	const char* config_path;
	const char* save_config_path;

	const char** keyframe_paths;
	int keyframe_count;
} AnimOptions;

int anim_load_keyframes(const char** paths, int count, int ignore_bad,
                        AnimKeyframe** out, int* out_count);
void anim_free_keyframes(AnimKeyframe* kfs, int count);

int anim_run(const AnimOptions* opts);

// Evaluate a single animation frame.
// real_kfs / n_real       : the original keyframes (used for discrete params)
// aug                     : the augmented keyframe array (bounce = real + synthetics)
// kf_frames[0..n_real]    : integer frame index of each real keyframe
// total_frames            : total frames in the animation
// frame_idx               : frame to evaluate
// interp_mode / easing    : as in AnimOptions
//
// Outputs:
//   out_config_a  : full config — continuous params from interpolation, discrete from real A
//   out_config_b  : full config — same continuous params, discrete from real B
//   out_view      : interpolated view
//   out_t_real    : eased t within the *real* segment [0,1] (blend weight for crossfade)
// Returns 1 on success.
int anim_eval_frame(const AnimKeyframe* real_kfs, int n_real,
                    const AnimKeyframe* aug_kfs, int n_aug,
                    const int* kf_frames, int total_frames,
                    int frame_idx, int interp_mode, int easing,
                    RenderConfig* out_config_a, RenderConfig* out_config_b,
                    ViewState* out_view, double* out_t_real);

// Compute total_frames from --total-frames / --duration*fps / --frames-per-transition,
// with warnings for lower-priority ignored options.
int anim_resolve_total_frames(const AnimOptions* opts, int n_kf);

#ifdef __cplusplus
}
#endif

#endif
