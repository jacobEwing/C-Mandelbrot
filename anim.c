#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <strings.h>
#include <time.h>
#include "anim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gmp.h>
#include <mpfr.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Forward declarations for helpers used by anim_run() */
static int aug_build(const AnimKeyframe* real, int n_real,
                     int via_bounce, double bounce_factor, double bounce_min,
                     AnimKeyframe** out_kfs, int* out_count);
static void aug_free(AnimKeyframe* kfs, int count);
static void compute_kf_frames(int total_frames, int n_kf, int* kf_frames);
/* Phase 3 helpers */
static int configs_differ_in_discrete(const RenderConfig* a, const RenderConfig* b);
static int render_anim_frame(uint32_t* pixels,
                             RenderConfig* cfg_a, RenderConfig* cfg_b,
                             ViewState* view, double t_real,
                             int policy, int width, int height);
static const char* path_basename(const char* p);
static double now_seconds(void);
static int encode_video(const AnimOptions* opts, const char* frames_dir, const char* output);
static void run_shell(const char* cmd);


int anim_load_keyframes(const char** paths, int count, int ignore_bad,
                        AnimKeyframe** out, int* out_count) {
	if (count <= 0) { *out = NULL; *out_count = 0; return 0; }

	AnimKeyframe* kfs = calloc(count, sizeof(AnimKeyframe));
	if (!kfs) return 0;
	int nloaded = 0;

	RenderConfig saved_config = config;

	for (int i = 0; i < count; i++) {
		AnimKeyframe* kf = &kfs[nloaded];
		mpf_init2(kf->view.cx, 256);
		mpf_init2(kf->view.cy, 256);
		mpf_init2(kf->view.zoom, 256);
		mpf_set_d(kf->view.cx, 0.0);
		mpf_set_d(kf->view.cy, 0.0);
		mpf_set_d(kf->view.zoom, 4.0);

		reset_config();

		if (!load_render(paths[i], kf->view.cx, kf->view.cy, kf->view.zoom)) {
			fprintf(stderr, "anim: failed to load keyframe: %s\n", paths[i]);
			mpf_clear(kf->view.cx);
			mpf_clear(kf->view.cy);
			mpf_clear(kf->view.zoom);
			if (!ignore_bad) {
				config = saved_config;
				anim_free_keyframes(kfs, nloaded);
				return 0;
			}
			continue;
		}

		kf->config = config;
		nloaded++;
	}

	config = saved_config;
	*out = kfs;
	*out_count = nloaded;
	return 1;
}

void anim_free_keyframes(AnimKeyframe* kfs, int count) {
	if (!kfs) return;
	for (int i = 0; i < count; i++) {
		mpf_clear(kfs[i].view.cx);
		mpf_clear(kfs[i].view.cy);
		mpf_clear(kfs[i].view.zoom);
	}
	free(kfs);
}

static int configs_differ_in_discrete(const RenderConfig* a, const RenderConfig* b) {
	return a->options.recursionDepth     != b->options.recursionDepth
	    || a->options.endCondition       != b->options.endCondition
	    || a->options.calcIterations     != b->options.calcIterations
	    || a->options.calcDisplacement   != b->options.calcDisplacement
	    || a->options.calcRotation       != b->options.calcRotation
	    || a->options.calcDisplacementXOR!= b->options.calcDisplacementXOR
	    || a->options.calcPolarCoordXOR  != b->options.calcPolarCoordXOR
	    || a->palette.staggerMaskRed     != b->palette.staggerMaskRed
	    || a->palette.staggerMaskRedLen  != b->palette.staggerMaskRedLen
	    || a->palette.staggerMaskGreen   != b->palette.staggerMaskGreen
	    || a->palette.staggerMaskGreenLen!= b->palette.staggerMaskGreenLen
	    || a->palette.staggerMaskBlue    != b->palette.staggerMaskBlue
	    || a->palette.staggerMaskBlueLen != b->palette.staggerMaskBlueLen
	    || a->palette.staggerBefore      != b->palette.staggerBefore
	    || a->palette.staggerAfter       != b->palette.staggerAfter;
}

static int render_anim_frame(uint32_t* pixels,
                             RenderConfig* cfg_a, RenderConfig* cfg_b,
                             ViewState* view, double t_real,
                             int policy, int width, int height) {
	int need_a = 0, need_b = 0;
	double blend = 0.0;

	switch (policy) {
		case 0: /* cut */
			need_a = (t_real < 0.5);
			need_b = !need_a;
			break;
		case 1: /* crossfade */
			if (t_real <= 0.0) need_a = 1;
			else if (t_real >= 1.0) need_b = 1;
			else if (!configs_differ_in_discrete(cfg_a, cfg_b)) need_a = 1;
			else { need_a = need_b = 1; blend = t_real; }
			break;
		case 2: /* hold-a */
			need_a = 1;
			break;
		case 3: /* hold-b */
			need_b = 1;
			break;
	}

	RenderConfig saved = config;

	if (!need_b) {
		config = *cfg_a;
		render_image(pixels, width, height, view);
		config = saved;
		return 1;
	}
	if (!need_a) {
		config = *cfg_b;
		render_image(pixels, width, height, view);
		config = saved;
		return 1;
	}

	size_t npix = (size_t)width * (size_t)height;
	uint32_t* buf_b = malloc(npix * sizeof(uint32_t));
	if (!buf_b) { config = saved; return 0; }

	config = *cfg_a;
	render_image(pixels, width, height, view);
	config = *cfg_b;
	render_image(buf_b, width, height, view);
	config = saved;

	double ia = 1.0 - blend;
	for (size_t i = 0; i < npix; i++) {
		uint32_t pa = pixels[i], pb = buf_b[i];
		uint32_t ra = (pa >> 16) & 0xFFu, ga = (pa >> 8) & 0xFFu, ba = pa & 0xFFu;
		uint32_t rb = (pb >> 16) & 0xFFu, gb = (pb >> 8) & 0xFFu, bb = pb & 0xFFu;
		uint32_t r = (uint32_t)(ra * ia + rb * blend + 0.5);
		uint32_t g = (uint32_t)(ga * ia + gb * blend + 0.5);
		uint32_t b = (uint32_t)(ba * ia + bb * blend + 0.5);
		pixels[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
	}
	free(buf_b);
	return 1;
}

static const char* path_basename(const char* p) {
	const char* s = strrchr(p, '/');
	return s ? s + 1 : p;
}

static double now_seconds(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static int encode_video(const AnimOptions* opts, const char* frames_dir, const char* output) {
	const char* ext = strrchr(output, '.');
	int is_webm = (ext && strcasecmp(ext, ".webm") == 0);
	int is_mp4  = (ext && strcasecmp(ext, ".mp4")  == 0);

	if (opts->pixfmt == 0 && ((opts->width % 2) || (opts->height % 2))) {
		fprintf(stderr, "anim: warning: yuv420p requires even width/height; "
		                "use --pixfmt=yuv444p or even dimensions\n");
	}

	char codec_opts[256];
	if (opts->codec == 1 || is_webm) {
		snprintf(codec_opts, sizeof(codec_opts),
		         "-c:v libvpx-vp9 -crf %d -b:v 0", opts->crf);
	} else {
		snprintf(codec_opts, sizeof(codec_opts),
		         "-c:v libx264 -preset slow -crf %d", opts->crf);
	}
	const char* pixfmt   = opts->pixfmt == 1 ? "yuv444p" : "yuv420p";
	const char* movflags = is_mp4 ? "-movflags +faststart" : "";

	char cmd[4096];
	snprintf(cmd, sizeof(cmd),
	         "ffmpeg -y -loglevel error -framerate %d "
	         "-i '%s/frame_%%05d.png' %s -pix_fmt %s %s '%s'",
	         opts->fps, frames_dir, codec_opts, pixfmt, movflags, output);

	printf("Encoding to %s...\n", output);
	int rc = system(cmd);
	if (rc != 0) {
		fprintf(stderr, "anim: ffmpeg failed (exit status %d)\n  cmd: %s\n", rc, cmd);
		return 0;
	}
	return 1;
}

int anim_run(const AnimOptions* opts) {
	AnimKeyframe* kfs = NULL;
	int n = 0;
	if (!anim_load_keyframes(opts->keyframe_paths, opts->keyframe_count,
	                         opts->ignore_bad, &kfs, &n)) {
		fprintf(stderr, "anim: keyframe loading failed\n");
		return 1;
	}
	if (n == 0) {
		fprintf(stderr, "anim: no keyframes loaded\n");
		anim_free_keyframes(kfs, n);
		return 1;
	}
	if (n == 1) {
		fprintf(stderr, "anim: need at least two keyframes to animate (got 1)\n");
		anim_free_keyframes(kfs, n);
		return 1;
	}

	printf("Loaded %d keyframe(s):\n", n);
	for (int i = 0; i < n; i++) {
		printf("  [%d] %s\n", i, opts->keyframe_paths[i]);
		gmp_printf("       cx = %.30Ff\n", kfs[i].view.cx);
		gmp_printf("       cy = %.30Ff\n", kfs[i].view.cy);
		gmp_printf("       zoom = %.30Ff\n", kfs[i].view.zoom);
		printf("       max_iter=%d  maxRadius=%.3f  recursion=%d  endCondition=%d\n",
		       kfs[i].config.options.max_iter,
		       kfs[i].config.options.maxRadius,
		       kfs[i].config.options.recursionDepth,
		       kfs[i].config.options.endCondition);
		printf("       calc: iter=%d disp=%d rot=%d xdisp=%d xpolar=%d\n",
		       kfs[i].config.options.calcIterations,
		       kfs[i].config.options.calcDisplacement,
		       kfs[i].config.options.calcRotation,
		       kfs[i].config.options.calcDisplacementXOR,
		       kfs[i].config.options.calcPolarCoordXOR);
	}

	const char* interp_str = opts->interp_mode == 0 ? "linear" : "catmull-rom";
	const char* discrete_strs[] = { "cut", "crossfade", "hold-a", "hold-b" };
	const char* easing_strs[] = { "linear", "ease-in", "ease-out", "ease-in-out", "smoothstep" };

	printf("\nRun configuration:\n");
	printf("  output      = %s\n", opts->output_path);
	printf("  size        = %dx%d\n", opts->width, opts->height);
	printf("  fps         = %d\n", opts->fps);
	printf("  interpolation = %s\n", interp_str);
	if (opts->interp_mode == 1) {
		printf("  catmull tension = %.2f\n", opts->catmull_tension);
	}
	printf("  easing      = %s (linear mode only)\n", easing_strs[opts->easing]);
	printf("  via         = %s", opts->via_bounce ? "bounce" : "none");
	if (opts->via_bounce) {
		printf(" (factor=%.2f, min=%g)", opts->bounce_factor, opts->bounce_min);
	}
	printf("\n");
	printf("  discrete    = %s\n", discrete_strs[opts->discrete_policy]);
	printf("  codec       = %s  crf=%d  pixfmt=%s\n",
	       opts->codec == 0 ? "h264" : "vp9",
	       opts->crf,
	       opts->pixfmt == 0 ? "yuv420p" : "yuv444p");

	int total_frames = anim_resolve_total_frames(opts, n);
	if (total_frames < 2) total_frames = 2;
	printf("\nResolved total_frames = %d\n", total_frames);

	if (opts->dry_run) {
		printf("\n[--dry-run: would render %d frames]\n", total_frames);
		anim_free_keyframes(kfs, n);
		return 0;
	}

	/* Build augmented keyframes */
	AnimKeyframe* aug_kfs = NULL;
	int n_aug = 0;
	if (!aug_build(kfs, n, opts->via_bounce, opts->bounce_factor, opts->bounce_min,
	               &aug_kfs, &n_aug)) {
		fprintf(stderr, "anim: failed to build augmented keyframes\n");
		anim_free_keyframes(kfs, n);
		return 1;
	}
	if (opts->via_bounce && total_frames >= n && (total_frames - 1) / (n - 1) < 2) {
		fprintf(stderr, "anim: warning: too few frames per transition to see bounce peak "
		                "(need >= 2 per segment)\n");
	}

	/* Keyframe → frame index map */
	if (n < 2) {
		/* defensive: already guaranteed above, but helps GCC's value-range
		   analysis see that n >= 2 here and silence -Walloc-size-larger-than */
		aug_free(aug_kfs, n_aug);
		anim_free_keyframes(kfs, n);
		return 1;
	}
	int* kf_frames = malloc(sizeof(int) * (size_t)n);

	if (!kf_frames) { aug_free(aug_kfs, n_aug); anim_free_keyframes(kfs, n); return 1; }
	compute_kf_frames(total_frames, n, kf_frames);

	/* ---- Classify output destination ---- */
	char output[1024];
	snprintf(output, sizeof(output), "%s", opts->output_path);
	size_t olen = strlen(output);
	while (olen > 1 && output[olen-1] == '/') output[--olen] = '\0';

	struct stat ost;
	int out_is_dir = (stat(output, &ost) == 0 && S_ISDIR(ost.st_mode));
	const char* out_ext = strrchr(output, '.');
	int out_is_png   = !out_is_dir && out_ext && strcasecmp(out_ext, ".png") == 0;
	int out_is_movie = !out_is_dir && out_ext &&
	                   (strcasecmp(out_ext, ".mp4")  == 0 ||
	                    strcasecmp(out_ext, ".mkv")  == 0 ||
	                    strcasecmp(out_ext, ".webm") == 0);

	if (!out_is_dir && !out_is_png && !out_is_movie) {
		fprintf(stderr, "anim: output '%s' is not a movie (.mp4/.mkv/.webm), "
		                "a .png, or an existing directory\n", output);
		free(kf_frames); aug_free(aug_kfs, n_aug); anim_free_keyframes(kfs, n);
		return 1;
	}

	int single_frame_mode = opts->single_frame;
	if (out_is_png && single_frame_mode < 0) single_frame_mode = 0;
	if (single_frame_mode >= total_frames) single_frame_mode = total_frames - 1;
	if (single_frame_mode < -1) single_frame_mode = -1;

	char frames_dir[1024];
	frames_dir[0] = '\0';
	int own_tmpdir = 0;
	int do_encode = 0;

	if (out_is_png) {
		/* single frame written directly to output */
	} else if (out_is_dir) {
		snprintf(frames_dir, sizeof(frames_dir), "%s", output);
	} else if (opts->render_only || single_frame_mode >= 0) {
		size_t stem_len = out_ext ? (size_t)(out_ext - output) : strlen(output);
		snprintf(frames_dir, sizeof(frames_dir), "%.*s_frames", (int)stem_len, output);
		errno = 0;
		if (mkdir(frames_dir, 0755) != 0 && errno != EEXIST) {
			fprintf(stderr, "anim: cannot create frames dir %s: %s\n",
			        frames_dir, strerror(errno));
			free(kf_frames); aug_free(aug_kfs, n_aug); anim_free_keyframes(kfs, n);
			return 1;
		}
	} else {
		snprintf(frames_dir, sizeof(frames_dir), "/tmp/animandel-XXXXXX");
		if (!mkdtemp(frames_dir)) {
			fprintf(stderr, "anim: cannot create tmpdir: %s\n", strerror(errno));
			free(kf_frames); aug_free(aug_kfs, n_aug); anim_free_keyframes(kfs, n);
			return 1;
		}
		own_tmpdir = 1;
		do_encode = 1;
	}

	if (do_encode && system("command -v ffmpeg >/dev/null 2>&1") != 0) {
		fprintf(stderr, "anim: ffmpeg not found. Install with: sudo apt install ffmpeg\n");
		free(kf_frames); aug_free(aug_kfs, n_aug); anim_free_keyframes(kfs, n);
		return 1;
	}

	/* ---- Render loop ---- */
	int first = single_frame_mode >= 0 ? single_frame_mode : 0;
	int last  = single_frame_mode >= 0 ? single_frame_mode : total_frames - 1;

	if (out_is_png) {
		printf("\nRendering frame %d -> %s\n", first, output);
	} else {
		printf("\nRendering %d frame(s) -> %s\n", last - first + 1, frames_dir);
	}

	int w = opts->width  > 0 ? opts->width  : 1;
	int h = opts->height > 0 ? opts->height : 1;
	uint32_t* pixels = malloc(sizeof(uint32_t) * (size_t)w * (size_t)h);
	if (!pixels) {
		fprintf(stderr, "anim: out of memory\n");
		if (own_tmpdir && !opts->keep_frames) {
			char cmd[2048];
			snprintf(cmd, sizeof(cmd), "rm -rf '%s'", frames_dir);
			run_shell(cmd);
		}
		free(kf_frames); aug_free(aug_kfs, n_aug); anim_free_keyframes(kfs, n);
		return 1;
	}

	ViewState view;
	mpf_init2(view.cx, 256);
	mpf_init2(view.cy, 256);
	mpf_init2(view.zoom, 256);

	int is_tty = isatty(STDERR_FILENO);
	double frame_time_sum = 0.0;
	int n_rendered = 0;
	int failed = 0;

	for (int i = first; i <= last; i++) {
		char fpath[2048];
		if (out_is_png) {
			snprintf(fpath, sizeof(fpath), "%s", output);
		} else {
			snprintf(fpath, sizeof(fpath), "%s/frame_%05d.png", frames_dir, i);
		}

		/* Resumability: skip existing, non-zero, non-single-frame */
		if (!opts->overwrite && single_frame_mode < 0 && !out_is_png) {
			struct stat st;
			if (stat(fpath, &st) == 0 && st.st_size > 0) {
				if (is_tty) {
					fprintf(stderr, "\r  [%d/%d] skip existing %s          ",
					        i - first + 1, last - first + 1, path_basename(fpath));
				} else {
					printf("frame %d/%d: skipped (exists) %s\n",
					       i - first + 1, last - first + 1, fpath);
				}
				continue;
			}
		}

		RenderConfig cfg_a, cfg_b;
		double t_real = 0.0;
		if (!anim_eval_frame(kfs, n, aug_kfs, n_aug, kf_frames, total_frames,
		                     i, opts->interp_mode, opts->catmull_tension,
		                     opts->easing,
		                     &cfg_a, &cfg_b, &view, &t_real)) {
			fprintf(stderr, "\nanim: failed to evaluate frame %d\n", i);
			failed = 1;
			break;
		}

		double frame_start = now_seconds();
		if (!render_anim_frame(pixels, &cfg_a, &cfg_b, &view, t_real,
		                       opts->discrete_policy, w, h)) {
			fprintf(stderr, "\nanim: failed to render frame %d\n", i);
			failed = 1;
			break;
		}
		double frame_time = now_seconds() - frame_start;
		frame_time_sum += frame_time;
		n_rendered++;

		/* Metadata: pick the nearest discrete side for round-tripping */
		RenderConfig meta_cfg;
		switch (opts->discrete_policy) {
			case 2: meta_cfg = cfg_a; break;
			case 3: meta_cfg = cfg_b; break;
			default: meta_cfg = (t_real < 0.5) ? cfg_a : cfg_b; break;
		}
		RenderConfig saved = config;
		config = meta_cfg;
		int wrote = write_png_with_metadata(fpath, pixels, w, h, &view);
		config = saved;

		if (!wrote) {
			fprintf(stderr, "\nanim: failed to write %s\n", fpath);
			failed = 1;
			break;
		}

		int done  = i - first + 1;
		int total = last - first + 1;
		if (is_tty) {
			double avg = n_rendered ? frame_time_sum / n_rendered : frame_time;
			int eta = (int)((last - i) * avg);
			fprintf(stderr, "\r  [%d/%d] %s  %.2fs  eta %d:%02d       ",
			        done, total, path_basename(fpath), frame_time, eta / 60, eta % 60);
		} else {
			printf("frame %d/%d: %s (%.2fs)\n", done, total, fpath, frame_time);
		}
	}

	if (is_tty) fprintf(stderr, "\n");

	mpf_clear(view.cx);
	mpf_clear(view.cy);
	mpf_clear(view.zoom);
	free(pixels);

	if (failed) {
		if (own_tmpdir && !opts->keep_frames) {
			char cmd[2048];
			snprintf(cmd, sizeof(cmd), "rm -rf '%s'", frames_dir);
			run_shell(cmd);
		}
		free(kf_frames); aug_free(aug_kfs, n_aug); anim_free_keyframes(kfs, n);
		return 1;
	}

	/* ---- Encode ---- */
	if (do_encode && !opts->render_only && single_frame_mode < 0) {
		if (!encode_video(opts, frames_dir, output)) {
			fprintf(stderr, "anim: encoding failed; frames left in %s\n", frames_dir);
			free(kf_frames); aug_free(aug_kfs, n_aug); anim_free_keyframes(kfs, n);
			return 1;
		}
	}

	/* ---- Cleanup tmpdir ---- */
	if (own_tmpdir && !opts->keep_frames) {
		char cmd[2048];
		snprintf(cmd, sizeof(cmd), "rm -rf '%s'", frames_dir);
		run_shell(cmd);
	}

	if (do_encode && single_frame_mode < 0) {
		printf("Wrote %s\n", output);
	} else if (!out_is_png) {
		printf("Frames written to %s\n", frames_dir);
	}

	free(kf_frames);
	aug_free(aug_kfs, n_aug);
	anim_free_keyframes(kfs, n);
	return 0;
}

static void run_shell(const char* cmd) {
	int ret = system(cmd);
	(void)ret;
}

/* ---- Easing ---- */

static double ease(int mode, double t) {
	if (t <= 0.0) return 0.0;
	if (t >= 1.0) return 1.0;
	switch (mode) {
		case 1: return t * t;
		case 2: { double u = 1.0 - t; return 1.0 - u * u; }
		case 3: return (t < 0.5) ? 2.0*t*t : 1.0 - 2.0*(1.0-t)*(1.0-t);
		case 4: return t * t * (3.0 - 2.0 * t);
		default: return t;
	}
}

/* ---- Angle helpers ---- */

static double wrap_delta(double d) {
	double r = fmod(d, 2.0 * M_PI);
	if (r >  M_PI) r -= 2.0 * M_PI;
	if (r <= -M_PI) r += 2.0 * M_PI;
	return r;
}

static double interp_angle_lin(double a, double b, double t) {
	return a + t * wrap_delta(b - a);
}

static void catmull_mpfr(mpfr_t out,
                         const mpfr_t p0, const mpfr_t p1,
                         const mpfr_t p2, const mpfr_t p3,
                         const mpfr_t t, double tension) {
	mpfr_prec_t prec = mpfr_get_prec(t);
	mpfr_t t2, t3, a, b, m0, m1, k;
	mpfr_init2(t2, prec); mpfr_init2(t3, prec);
	mpfr_init2(a, prec);  mpfr_init2(b, prec);
	mpfr_init2(m0, prec); mpfr_init2(m1, prec);
	mpfr_init2(k, prec);

	mpfr_set_d(k, (1.0 - tension) * 0.5, MPFR_RNDN);

	mpfr_mul(t2, t, t, MPFR_RNDN);
	mpfr_mul(t3, t2, t, MPFR_RNDN);

	/* m0 = k/2 * (p2 - p0), m1 = k/2 * (p3 - p1) */
	mpfr_sub(m0, p2, p0, MPFR_RNDN);
	mpfr_mul(m0, m0, k, MPFR_RNDN);
	mpfr_sub(m1, p3, p1, MPFR_RNDN);
	mpfr_mul(m1, m1, k, MPFR_RNDN);

	/* out = h00*p1 + h10*m0 + h01*p2 + h11*m1 */
	mpfr_mul_ui(a, t3, 2, MPFR_RNDN);
	mpfr_mul_ui(b, t2, 3, MPFR_RNDN);
	mpfr_sub(a, a, b, MPFR_RNDN);
	mpfr_add_ui(a, a, 1, MPFR_RNDN);
	mpfr_mul(out, p1, a, MPFR_RNDN);

	mpfr_set(a, t3, MPFR_RNDN);
	mpfr_mul_ui(b, t2, 2, MPFR_RNDN);
	mpfr_sub(a, a, b, MPFR_RNDN);
	mpfr_add(a, a, t, MPFR_RNDN);
	mpfr_mul(a, a, m0, MPFR_RNDN);
	mpfr_add(out, out, a, MPFR_RNDN);

	mpfr_mul_si(a, t3, -2, MPFR_RNDN);
	mpfr_mul_ui(b, t2, 3, MPFR_RNDN);
	mpfr_add(a, a, b, MPFR_RNDN);
	mpfr_mul(a, a, p2, MPFR_RNDN);
	mpfr_add(out, out, a, MPFR_RNDN);

	mpfr_sub(a, t3, t2, MPFR_RNDN);
	mpfr_mul(a, a, m1, MPFR_RNDN);
	mpfr_add(out, out, a, MPFR_RNDN);

	mpfr_clears(t2, t3, a, b, m0, m1, k, NULL);
}

/* ---- Catmull-Rom scalar ---- */

static double catmull_scalar(double p0, double p1, double p2, double p3,
                              double t, double tension) {
	double k = 1.0 - tension;
	double t2 = t * t, t3 = t2 * t;
	double m0 = k * 0.5 * (p2 - p0);
	double m1 = k * 0.5 * (p3 - p1);
	return ( 2.0*t3 - 3.0*t2 + 1.0) * p1
	     + (        t3 - 2.0*t2 + t) * m0
	     + (-2.0*t3 + 3.0*t2      ) * p2
	     + (        t3 -       t2 ) * m1;
}

static double catmull_angle(double a0, double a1, double a2, double a3,
                             double t, double tension) {
	double u0 = a0;
	double u1 = u0 + wrap_delta(a1 - u0);
	double u2 = u1 + wrap_delta(a2 - u1);
	double u3 = u2 + wrap_delta(a3 - u2);
	return catmull_scalar(u0, u1, u2, u3, t, tension);
}

/* ---- Unified scalar interpolation ---- */

static double interp_val(double v0, double v1, double v2, double v3,
                          int mode, double t, double tension) {
	if (mode == 0) return v1 + (v2 - v1) * t;
	return catmull_scalar(v0, v1, v2, v3, t, tension);
}

static int interp_val_int(int v0, int v1, int v2, int v3,
                           int mode, double t, double tension) {
	double d = interp_val((double)v0, (double)v1, (double)v2, (double)v3,
	                      mode, t, tension);
	return (int)(d + (d >= 0 ? 0.5 : -0.5));
}

static double interp_val_angle(double v0, double v1, double v2, double v3,
                                int mode, double t, double tension) {
	if (mode == 0) return interp_angle_lin(v1, v2, t);
	return catmull_angle(v0, v1, v2, v3, t, tension);
}

/* ---- View interpolation ---- */

// Linear mode: log-zoom + screen-relative xy (per spec §1).
static void interp_view_linear(mpf_t out_cx, mpf_t out_cy, mpf_t out_zoom,
                               const ViewState* a, const ViewState* b, double t) {
	if (t <= 0.0) {
		mpf_set(out_cx, a->cx); mpf_set(out_cy, a->cy); mpf_set(out_zoom, a->zoom);
		return;
	}
	if (t >= 1.0) {
		mpf_set(out_cx, b->cx); mpf_set(out_cy, b->cy); mpf_set(out_zoom, b->zoom);
		return;
	}

	mpfr_t wa, wb, w, lwa, lwb, x0, x1, y0, y1, dx, dy, den, ratio;
	mpfr_init2(wa, 256); mpfr_init2(wb, 256); mpfr_init2(w, 256);
	mpfr_init2(lwa, 256); mpfr_init2(lwb, 256);
	mpfr_init2(x0, 256); mpfr_init2(x1, 256);
	mpfr_init2(y0, 256); mpfr_init2(y1, 256);
	mpfr_init2(dx, 256); mpfr_init2(dy, 256);
	mpfr_init2(den, 256); mpfr_init2(ratio, 256);

	mpfr_set_f(wa, a->zoom, MPFR_RNDN);
	mpfr_set_f(wb, b->zoom, MPFR_RNDN);
	mpfr_log(lwa, wa, MPFR_RNDN);
	mpfr_log(lwb, wb, MPFR_RNDN);
	mpfr_sub(lwb, lwb, lwa, MPFR_RNDN);
	mpfr_mul_d(lwb, lwb, t, MPFR_RNDN);
	mpfr_add(lwa, lwa, lwb, MPFR_RNDN);
	mpfr_exp(w, lwa, MPFR_RNDN);
	mpfr_get_f(out_zoom, w, MPFR_RNDN);

	mpfr_set_f(x0, a->cx, MPFR_RNDN);
	mpfr_set_f(x1, b->cx, MPFR_RNDN);
	mpfr_set_f(y0, a->cy, MPFR_RNDN);
	mpfr_set_f(y1, b->cy, MPFR_RNDN);

	mpfr_sub(den, wb, wa, MPFR_RNDN);
	if (mpfr_zero_p(den)) {
		mpfr_set_d(ratio, t, MPFR_RNDN);
	} else {
		mpfr_sub(ratio, w, wa, MPFR_RNDN);
		mpfr_div(ratio, ratio, den, MPFR_RNDN);
	}

	mpfr_sub(dx, x1, x0, MPFR_RNDN);
	mpfr_mul(dx, dx, ratio, MPFR_RNDN);
	mpfr_add(dx, dx, x0, MPFR_RNDN);
	mpfr_get_f(out_cx, dx, MPFR_RNDN);

	mpfr_sub(dy, y1, y0, MPFR_RNDN);
	mpfr_mul(dy, dy, ratio, MPFR_RNDN);
	mpfr_add(dy, dy, y0, MPFR_RNDN);
	mpfr_get_f(out_cy, dy, MPFR_RNDN);

	mpfr_clears(wa, wb, w, lwa, lwb, x0, x1, y0, y1, dx, dy, den, ratio, (mpfr_ptr)0);
}

// Catmull-Rom mode: 3D curve through (cx, cy, log(zoom)).
static void interp_view_catmull(mpf_t out_cx, mpf_t out_cy, mpf_t out_zoom,
                                const ViewState* v0, const ViewState* v1,
                                const ViewState* v2, const ViewState* v3,
                                double t, double tension) {
	if (t <= 0.0) {
		mpf_set(out_cx, v1->cx); mpf_set(out_cy, v1->cy); mpf_set(out_zoom, v1->zoom);
		return;
	}
	if (t >= 1.0) {
		mpf_set(out_cx, v2->cx); mpf_set(out_cy, v2->cy); mpf_set(out_zoom, v2->zoom);
		return;
	}

	mpfr_prec_t prec = 256;
	mpfr_t x0, x1, x2, x3, y0, y1, y2, y3;
	mpfr_t lz0, lz1, lz2, lz3, lz_t, w_t, s;
	mpfr_t z1_mp, z2_mp, cx_mp, cy_mp, tmp, t_mp;
	mpfr_inits2(prec, x0, x1, x2, x3, y0, y1, y2, y3,
	            lz0, lz1, lz2, lz3, lz_t, w_t, s,
	            z1_mp, z2_mp, cx_mp, cy_mp, tmp, t_mp, (mpfr_ptr)0);

	mpfr_set_f(x0, v0->cx, MPFR_RNDN);
	mpfr_set_f(x1, v1->cx, MPFR_RNDN);
	mpfr_set_f(x2, v2->cx, MPFR_RNDN);
	mpfr_set_f(x3, v3->cx, MPFR_RNDN);
	mpfr_set_f(y0, v0->cy, MPFR_RNDN);
	mpfr_set_f(y1, v1->cy, MPFR_RNDN);
	mpfr_set_f(y2, v2->cy, MPFR_RNDN);
	mpfr_set_f(y3, v3->cy, MPFR_RNDN);

	mpfr_set_f(z1_mp, v1->zoom, MPFR_RNDN);
	mpfr_set_f(z2_mp, v2->zoom, MPFR_RNDN);

	mpfr_set_f(tmp, v0->zoom, MPFR_RNDN); mpfr_log(lz0, tmp, MPFR_RNDN);
	mpfr_log(lz1, z1_mp, MPFR_RNDN);
	mpfr_log(lz2, z2_mp, MPFR_RNDN);
	mpfr_set_f(tmp, v3->zoom, MPFR_RNDN); mpfr_log(lz3, tmp, MPFR_RNDN);

	mpfr_set_d(t_mp, t, MPFR_RNDN);

	catmull_mpfr(lz_t, lz0, lz1, lz2, lz3, t_mp, tension);
	mpfr_exp(w_t, lz_t, MPFR_RNDN);

	if (mpfr_cmp(z1_mp, z2_mp) == 0) {
		mpfr_set(s, t_mp, MPFR_RNDN);
	} else {
		mpfr_sub(s, w_t, z1_mp, MPFR_RNDN);
		mpfr_sub(tmp, z2_mp, z1_mp, MPFR_RNDN);
		mpfr_div(s, s, tmp, MPFR_RNDN);
	}

	catmull_mpfr(cx_mp, x0, x1, x2, x3, s, tension);
	catmull_mpfr(cy_mp, y0, y1, y2, y3, s, tension);

	mpfr_get_f(out_cx, cx_mp, MPFR_RNDN);
	mpfr_get_f(out_cy, cy_mp, MPFR_RNDN);
	mpfr_get_f(out_zoom, w_t, MPFR_RNDN);

	mpfr_clears(x0, x1, x2, x3, y0, y1, y2, y3,
	            lz0, lz1, lz2, lz3, lz_t, w_t, s,
	            z1_mp, z2_mp, cx_mp, cy_mp, tmp, t_mp, (mpfr_ptr)0);
}

/* ---- Config continuous interpolation ---- */

static void interp_config_cont(const RenderConfig* c0, const RenderConfig* c1,
                               const RenderConfig* c2, const RenderConfig* c3,
                               int mode, double t, double tension,
                               RenderConfig* out) {
	out->options.max_iter = interp_val_int(
		c0->options.max_iter, c1->options.max_iter,
		c2->options.max_iter, c3->options.max_iter, mode, t, tension);
	if (out->options.max_iter < 1) out->options.max_iter = 1;

	out->options.maxRadius = interp_val(
		c0->options.maxRadius, c1->options.maxRadius,
		c2->options.maxRadius, c3->options.maxRadius, mode, t, tension);
	out->options.countOffset = interp_val(
		c0->options.countOffset, c1->options.countOffset,
		c2->options.countOffset, c3->options.countOffset, mode, t, tension);

	out->options.coefIterations = interp_val(
		c0->options.coefIterations, c1->options.coefIterations,
		c2->options.coefIterations, c3->options.coefIterations, mode, t, tension);
	out->options.coefDisplacement = interp_val(
		c0->options.coefDisplacement, c1->options.coefDisplacement,
		c2->options.coefDisplacement, c3->options.coefDisplacement, mode, t, tension);
	out->options.coefRotation = interp_val(
		c0->options.coefRotation, c1->options.coefRotation,
		c2->options.coefRotation, c3->options.coefRotation, mode, t, tension);
	out->options.coefDisplacementXOR = interp_val(
		c0->options.coefDisplacementXOR, c1->options.coefDisplacementXOR,
		c2->options.coefDisplacementXOR, c3->options.coefDisplacementXOR, mode, t, tension);
	out->options.coefPolarCoordXOR = interp_val(
		c0->options.coefPolarCoordXOR, c1->options.coefPolarCoordXOR,
		c2->options.coefPolarCoordXOR, c3->options.coefPolarCoordXOR, mode, t, tension);

	out->palette.colourWavePeriod = interp_val(
		c0->palette.colourWavePeriod, c1->palette.colourWavePeriod,
		c2->palette.colourWavePeriod, c3->palette.colourWavePeriod, mode, t, tension);
	if (out->palette.colourWavePeriod < 1e-6)
		out->palette.colourWavePeriod = 1e-6;

	out->palette.masterOffset = interp_val_angle(
		c0->palette.masterOffset, c1->palette.masterOffset,
		c2->palette.masterOffset, c3->palette.masterOffset, mode, t, tension);
	out->palette.redOffset = interp_val_angle(
		c0->palette.redOffset, c1->palette.redOffset,
		c2->palette.redOffset, c3->palette.redOffset, mode, t, tension);
	out->palette.greenOffset = interp_val_angle(
		c0->palette.greenOffset, c1->palette.greenOffset,
		c2->palette.greenOffset, c3->palette.greenOffset, mode, t, tension);
	out->palette.blueOffset = interp_val_angle(
		c0->palette.blueOffset, c1->palette.blueOffset,
		c2->palette.blueOffset, c3->palette.blueOffset, mode, t, tension);

	out->palette.masterStagger = interp_val(
		c0->palette.masterStagger, c1->palette.masterStagger,
		c2->palette.masterStagger, c3->palette.masterStagger, mode, t, tension);
	out->palette.redStagger = interp_val(
		c0->palette.redStagger, c1->palette.redStagger,
		c2->palette.redStagger, c3->palette.redStagger, mode, t, tension);
	out->palette.greenStagger = interp_val(
		c0->palette.greenStagger, c1->palette.greenStagger,
		c2->palette.greenStagger, c3->palette.greenStagger, mode, t, tension);
	out->palette.blueStagger = interp_val(
		c0->palette.blueStagger, c1->palette.blueStagger,
		c2->palette.blueStagger, c3->palette.blueStagger, mode, t, tension);

	out->palette.redPeriod = interp_val(
		c0->palette.redPeriod, c1->palette.redPeriod,
		c2->palette.redPeriod, c3->palette.redPeriod, mode, t, tension);
	if (out->palette.redPeriod < 1e-6) out->palette.redPeriod = 1e-6;
	out->palette.greenPeriod = interp_val(
		c0->palette.greenPeriod, c1->palette.greenPeriod,
		c2->palette.greenPeriod, c3->palette.greenPeriod, mode, t, tension);
	if (out->palette.greenPeriod < 1e-6) out->palette.greenPeriod = 1e-6;
	out->palette.bluePeriod = interp_val(
		c0->palette.bluePeriod, c1->palette.bluePeriod,
		c2->palette.bluePeriod, c3->palette.bluePeriod, mode, t, tension);
	if (out->palette.bluePeriod < 1e-6) out->palette.bluePeriod = 1e-6;

	out->viewRotation = interp_val(
		c0->viewRotation, c1->viewRotation,
		c2->viewRotation, c3->viewRotation, mode, t, tension);

	/* discrete — copy from c1 (caller overrides for the b-side config) */
	out->options.recursionDepth = c1->options.recursionDepth;
	out->options.endCondition = c1->options.endCondition;
	out->options.calcIterations = c1->options.calcIterations;
	out->options.calcDisplacement = c1->options.calcDisplacement;
	out->options.calcRotation = c1->options.calcRotation;
	out->options.calcDisplacementXOR = c1->options.calcDisplacementXOR;
	out->options.calcPolarCoordXOR = c1->options.calcPolarCoordXOR;
	out->palette.staggerMaskRed = c1->palette.staggerMaskRed;
	out->palette.staggerMaskRedLen = c1->palette.staggerMaskRedLen;
	out->palette.staggerMaskGreen = c1->palette.staggerMaskGreen;
	out->palette.staggerMaskGreenLen = c1->palette.staggerMaskGreenLen;
	out->palette.staggerMaskBlue = c1->palette.staggerMaskBlue;
	out->palette.staggerMaskBlueLen = c1->palette.staggerMaskBlueLen;
	out->palette.staggerBefore = c1->palette.staggerBefore;
	out->palette.staggerAfter = c1->palette.staggerAfter;
}

static void copy_discrete_fields(const RenderConfig* src, RenderConfig* dst) {
	dst->options.recursionDepth = src->options.recursionDepth;
	dst->options.endCondition = src->options.endCondition;
	dst->options.calcIterations = src->options.calcIterations;
	dst->options.calcDisplacement = src->options.calcDisplacement;
	dst->options.calcRotation = src->options.calcRotation;
	dst->options.calcDisplacementXOR = src->options.calcDisplacementXOR;
	dst->options.calcPolarCoordXOR = src->options.calcPolarCoordXOR;
	dst->palette.staggerMaskRed = src->palette.staggerMaskRed;
	dst->palette.staggerMaskRedLen = src->palette.staggerMaskRedLen;
	dst->palette.staggerMaskGreen = src->palette.staggerMaskGreen;
	dst->palette.staggerMaskGreenLen = src->palette.staggerMaskGreenLen;
	dst->palette.staggerMaskBlue = src->palette.staggerMaskBlue;
	dst->palette.staggerMaskBlueLen = src->palette.staggerMaskBlueLen;
	dst->palette.staggerBefore = src->palette.staggerBefore;
	dst->palette.staggerAfter = src->palette.staggerAfter;
}

/* ---- Augmented keyframes ---- */

static int aug_build(const AnimKeyframe* real, int n_real,
                     int via_bounce, double bounce_factor, double bounce_min,
                     AnimKeyframe** out_kfs, int* out_count) {
	if (n_real < 1) { *out_kfs = NULL; *out_count = 0; return 1; }

	int n_aug = via_bounce ? (2 * n_real - 1) : n_real;
	AnimKeyframe* kfs = calloc(n_aug, sizeof(AnimKeyframe));
	if (!kfs) return 0;

	for (int i = 0; i < n_real; i++) {
		int idx = via_bounce ? 2 * i : i;
		kfs[idx].config = real[i].config;
		mpf_init2(kfs[idx].view.cx, 256);
		mpf_init2(kfs[idx].view.cy, 256);
		mpf_init2(kfs[idx].view.zoom, 256);
		mpf_set(kfs[idx].view.cx, real[i].view.cx);
		mpf_set(kfs[idx].view.cy, real[i].view.cy);
		mpf_set(kfs[idx].view.zoom, real[i].view.zoom);
	}

	if (via_bounce) {
		for (int i = 0; i < n_real - 1; i++) {
			int mid = 2 * i + 1;
			const AnimKeyframe* a = &real[i];
			const AnimKeyframe* b = &real[i + 1];

			mpf_init2(kfs[mid].view.cx, 256);
			mpf_init2(kfs[mid].view.cy, 256);
			mpf_init2(kfs[mid].view.zoom, 256);

			/* xy: arithmetic midpoint (straight-line path midpoint) */
			mpf_add(kfs[mid].view.cx, a->view.cx, b->view.cx);
			mpf_div_ui(kfs[mid].view.cx, kfs[mid].view.cx, 2);
			mpf_add(kfs[mid].view.cy, a->view.cy, b->view.cy);
			mpf_div_ui(kfs[mid].view.cy, kfs[mid].view.cy, 2);

			/* zoom: max(za, zb) * bounce_factor, clamped to >= bounce_min */
			mpf_t maxz, bf;
			mpf_init2(maxz, 256);
			mpf_init2(bf, 256);
			if (mpf_cmp(a->view.zoom, b->view.zoom) > 0) mpf_set(maxz, a->view.zoom);
			else mpf_set(maxz, b->view.zoom);
			mpf_set_d(bf, bounce_factor);
			mpf_mul(kfs[mid].view.zoom, maxz, bf);
			if (mpf_cmp_d(kfs[mid].view.zoom, bounce_min) < 0)
				mpf_set_d(kfs[mid].view.zoom, bounce_min);
			mpf_clear(maxz);
			mpf_clear(bf);

			/* config: linear midpoint of continuous; discrete from A */
			interp_config_cont(&a->config, &a->config, &b->config, &b->config,
			                   0, 0.5, 0.0, &kfs[mid].config);
		}
	}

	*out_kfs = kfs;
	*out_count = n_aug;
	return 1;
}

static void aug_free(AnimKeyframe* kfs, int count) {
	if (!kfs) return;
	for (int i = 0; i < count; i++) {
		mpf_clear(kfs[i].view.cx);
		mpf_clear(kfs[i].view.cy);
		mpf_clear(kfs[i].view.zoom);
	}
	free(kfs);
}

/* ---- Frame ↔ segment mapping ---- */

static void compute_kf_frames(int total_frames, int n_kf, int* kf_frames) {
	int n_seg = n_kf - 1;
	for (int k = 0; k <= n_seg; k++) {
		kf_frames[k] = (int)((double)k * (total_frames - 1) / n_seg + 0.5);
	}
	/* enforce strictly increasing */
	for (int k = 1; k <= n_seg; k++) {
		if (kf_frames[k] <= kf_frames[k-1]) kf_frames[k] = kf_frames[k-1] + 1;
	}
}

static void frame_to_segt(int f, const int* kf_frames, int n_kf,
                          int* out_seg, double* out_t) {
	int n_seg = n_kf - 1;
	if (n_seg <= 0) { *out_seg = 0; *out_t = 0.0; return; }
	if (f <= kf_frames[0]) { *out_seg = 0; *out_t = 0.0; return; }
	if (f >= kf_frames[n_seg]) { *out_seg = n_seg - 1; *out_t = 1.0; return; }

	int seg = 0;
	while (seg < n_seg - 1 && f >= kf_frames[seg + 1]) seg++;
	int f0 = kf_frames[seg], f1 = kf_frames[seg + 1];
	*out_seg = seg;
	*out_t = (double)(f - f0) / (double)(f1 - f0);
}

/* ---- Public: total_frames resolution ---- */

int anim_resolve_total_frames(const AnimOptions* opts, int n_kf) {
	int n_seg = n_kf - 1;
	if (n_seg < 1) return 1;
	int fps = opts->fps > 0 ? opts->fps : 30;

	if (opts->total_frames > 0) {
		if (opts->duration > 0.0)
			fprintf(stderr, "anim: warning: --total-frames overrides --duration\n");
		if (opts->frames_per_transition > 0)
			fprintf(stderr, "anim: warning: --total-frames overrides --frames-per-transition\n");
		return opts->total_frames;
	}
	if (opts->duration > 0.0) {
		if (opts->frames_per_transition > 0)
			fprintf(stderr, "anim: warning: --duration overrides --frames-per-transition\n");
		int n = (int)(opts->duration * fps + 0.5);
		return n < 2 ? 2 : n;
	}
	int per = opts->frames_per_transition > 0 ? opts->frames_per_transition : 90;
	return n_seg * per + 1;
}

/* ---- Public: single frame evaluation ---- */

int anim_eval_frame(const AnimKeyframe* real_kfs, int n_real,
                    const AnimKeyframe* aug_kfs, int n_aug,
                    const int* kf_frames, int total_frames,
                    int frame_idx, int interp_mode, double catmull_tension,
                    int easing,
                    RenderConfig* out_config_a, RenderConfig* out_config_b,
                    ViewState* out_view, double* out_t_real) {
	if (n_real < 2 || !real_kfs || !aug_kfs || !kf_frames) return 0;
	if (frame_idx < 0 || frame_idx >= total_frames) return 0;

	int seg_real;
	double t_real;
	frame_to_segt(frame_idx, kf_frames, n_real, &seg_real, &t_real);

	int seg_aug;
	double t_aug;

	if (n_aug == n_real) {
		seg_aug = seg_real;
		t_aug = t_real;
	} else {
		int seglen = kf_frames[seg_real + 1] - kf_frames[seg_real];
		if (seglen < 2) {
			/* Not enough frames to bounce meaningfully */
			seg_aug = 2 * seg_real;
			t_aug = 0.0;
		} else {
			int f_mid = kf_frames[seg_real] + seglen / 2;
			if (frame_idx < f_mid) {
				seg_aug = 2 * seg_real;
				t_aug = (double)(frame_idx - kf_frames[seg_real]) /
				        (double)(f_mid - kf_frames[seg_real]);
			} else {
				seg_aug = 2 * seg_real + 1;
				t_aug = (double)(frame_idx - f_mid) /
				        (double)(kf_frames[seg_real + 1] - f_mid);
			}
		}
	}

	if (seg_aug < 0) seg_aug = 0;
	if (seg_aug >= n_aug - 1) seg_aug = n_aug - 2;

	double t_aug_eased = (interp_mode == 0) ? ease(easing, t_aug) : t_aug;
	double t_real_eased = (interp_mode == 0) ? ease(easing, t_real) : t_real;

	/* View: catmull-rom (with tension) or linear */
	if (interp_mode == 1) {
		int i1 = seg_aug;
		int i2 = seg_aug + 1;
		int i0 = (seg_aug > 0) ? seg_aug - 1 : i1;
		int i3 = (seg_aug + 2 < n_aug) ? seg_aug + 2 : i2;

		interp_view_catmull(out_view->cx, out_view->cy, out_view->zoom,
		                    &aug_kfs[i0].view, &aug_kfs[i1].view,
		                    &aug_kfs[i2].view, &aug_kfs[i3].view,
		                    t_aug, catmull_tension);
	} else {
		const AnimKeyframe* a = &aug_kfs[seg_aug];
		const AnimKeyframe* b = &aug_kfs[seg_aug + 1];
		interp_view_linear(out_view->cx, out_view->cy, out_view->zoom,
		                   &a->view, &b->view, t_aug_eased);
	}

	/* Config parameters: always linear (with easing) to prevent overshoot
	   in colour coefficients and palette periods. */
	{
		const AnimKeyframe* a = &aug_kfs[seg_aug];
		const AnimKeyframe* b = &aug_kfs[seg_aug + 1];
		interp_config_cont(&a->config, &a->config, &b->config, &b->config,
		                   0, t_aug_eased, 0.0, out_config_a);
	}

	*out_config_b = *out_config_a;

	const AnimKeyframe* ra = &real_kfs[seg_real];
	const AnimKeyframe* rb = &real_kfs[seg_real + 1];
	copy_discrete_fields(&ra->config, out_config_a);
	copy_discrete_fields(&rb->config, out_config_b);

	*out_t_real = t_real_eased;
	return 1;
}
