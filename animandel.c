#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "anim.h"

static void print_usage(const char* prog) {
	fprintf(stderr,
		"Usage: %s [options] <keyframe1> [keyframe2] ...\n"
		"\n"
		"Options:\n"
		"  --output=PATH              default: animation.mp4\n"
		"  --size=WxH                 default: 1920x1080\n"
		"  -w N, --width=N            override width\n"
		"  -h N, --height=N           override height\n"
		"  --fps=N                    default: 30\n"
		"  --total-frames=N\n"
		"  --duration=SECONDS\n"
		"  --frames-per-transition=N  default: 90\n"
		"  --interpolation=MODE       linear|catmull-rom (default: catmull-rom)\n"
		"  --via=MODE                 none|bounce (default: none)\n"
		"  --bounce-factor=N          default: 10\n"
		"  --bounce-min=W             default: 0.01\n"
		"  --easing=EASE              linear|ease-in|ease-out|ease-in-out|smoothstep\n"
		"  --discrete=POLICY          cut|crossfade|hold-a|hold-b (default: crossfade)\n"
		"  --codec=CODEC              h264|vp9 (default: h264)\n"
		"  --crf=N                    default: 18\n"
		"  --pixfmt=FMT               yuv420p|yuv444p (default: yuv420p)\n"
		"  --jobs=N                   default: 1\n"
		"  --keepframes               keep rendered frames\n"
		"  --overwrite                re-render existing frames\n"
		"  --ignorebad                skip unloadable keyframes\n"
		"  --dry-run                  print plan and exit\n"
		"  --render-only              skip ffmpeg encoding\n"
		"  --single-frame=N           render just one frame\n"
		"  --config=PATH              load options from file\n"
		"  --save-config=PATH         dump options to file\n"
		"  --help                     show this help\n",
		prog);
}

static int parse_size(const char* s, int* w, int* h) {
	return sscanf(s, "%dx%d", w, h) == 2 && *w > 0 && *h > 0;
}

int main(int argc, char* argv[]) {
	AnimOptions opts;
	memset(&opts, 0, sizeof(opts));
	opts.output_path = "animation.mp4";
	opts.width = 1920;
	opts.height = 1080;
	opts.fps = 30;
	opts.frames_per_transition = 0;
	opts.interp_mode = 1;              // catmull-rom
	opts.easing = 0;                   // linear
	opts.via_bounce = 0;
	opts.bounce_factor = 10.0;
	opts.bounce_min = 0.01;
	opts.discrete_policy = 1;          // crossfade (user preference; spec default was cut)
	opts.codec = 0;                    // h264
	opts.crf = 18;
	opts.pixfmt = 0;                   // yuv420p
	opts.jobs = 1;
	opts.single_frame = -1;

	const char** keyframes = calloc(argc, sizeof(char*));
	if (!keyframes) return 1;
	int nk = 0;

	for (int i = 1; i < argc; i++) {
		const char* a = argv[i];
		if (strcmp(a, "--help") == 0) { print_usage(argv[0]); free(keyframes); return 0; }
		if (a[0] != '-' || a[1] == '\0') { keyframes[nk++] = a; continue; }

		if (strncmp(a, "--output=", 9) == 0) opts.output_path = a + 9;
		else if (strcmp(a, "--output") == 0 && i + 1 < argc) opts.output_path = argv[++i];
		else if (strncmp(a, "--size=", 7) == 0) {
			if (!parse_size(a + 7, &opts.width, &opts.height)) {
				fprintf(stderr, "bad --size (want WxH)\n"); free(keyframes); return 1;
			}
		}
		else if (strncmp(a, "--width=", 8) == 0) opts.width = atoi(a + 8);
		else if (strcmp(a, "-w") == 0 && i + 1 < argc) opts.width = atoi(argv[++i]);
		else if (strncmp(a, "--height=", 9) == 0) opts.height = atoi(a + 9);
		else if (strcmp(a, "-h") == 0 && i + 1 < argc) opts.height = atoi(argv[++i]);
		else if (strncmp(a, "--fps=", 6) == 0) opts.fps = atoi(a + 6);
		else if (strncmp(a, "--total-frames=", 15) == 0) opts.total_frames = atoi(a + 15);
		else if (strncmp(a, "--duration=", 11) == 0) opts.duration = atof(a + 11);
		else if (strncmp(a, "--frames-per-transition=", 24) == 0) opts.frames_per_transition = atoi(a + 24);
		else if (strncmp(a, "--interpolation=", 16) == 0) {
			const char* v = a + 16;
			if (strcmp(v, "linear") == 0) opts.interp_mode = 0;
			else if (strcmp(v, "catmull-rom") == 0) opts.interp_mode = 1;
			else { fprintf(stderr, "bad --interpolation\n"); free(keyframes); return 1; }
		}
		else if (strncmp(a, "--via=", 6) == 0) {
			const char* v = a + 6;
			if (strcmp(v, "none") == 0) opts.via_bounce = 0;
			else if (strcmp(v, "bounce") == 0) opts.via_bounce = 1;
			else { fprintf(stderr, "bad --via\n"); free(keyframes); return 1; }
		}
		else if (strncmp(a, "--bounce-factor=", 16) == 0) opts.bounce_factor = atof(a + 16);
		else if (strncmp(a, "--bounce-min=", 13) == 0) opts.bounce_min = atof(a + 13);
		else if (strncmp(a, "--easing=", 9) == 0) {
			const char* v = a + 9;
			if      (strcmp(v, "linear") == 0) opts.easing = 0;
			else if (strcmp(v, "ease-in") == 0) opts.easing = 1;
			else if (strcmp(v, "ease-out") == 0) opts.easing = 2;
			else if (strcmp(v, "ease-in-out") == 0) opts.easing = 3;
			else if (strcmp(v, "smoothstep") == 0) opts.easing = 4;
			else { fprintf(stderr, "bad --easing\n"); free(keyframes); return 1; }
		}
		else if (strncmp(a, "--discrete=", 11) == 0) {
			const char* v = a + 11;
			if      (strcmp(v, "cut") == 0) opts.discrete_policy = 0;
			else if (strcmp(v, "crossfade") == 0) opts.discrete_policy = 1;
			else if (strcmp(v, "hold-a") == 0) opts.discrete_policy = 2;
			else if (strcmp(v, "hold-b") == 0) opts.discrete_policy = 3;
			else { fprintf(stderr, "bad --discrete\n"); free(keyframes); return 1; }
		}
		else if (strncmp(a, "--codec=", 8) == 0) {
			const char* v = a + 8;
			if (strcmp(v, "h264") == 0) opts.codec = 0;
			else if (strcmp(v, "vp9") == 0) opts.codec = 1;
			else { fprintf(stderr, "bad --codec\n"); free(keyframes); return 1; }
		}
		else if (strncmp(a, "--crf=", 6) == 0) opts.crf = atoi(a + 6);
		else if (strncmp(a, "--pixfmt=", 9) == 0) {
			const char* v = a + 9;
			if (strcmp(v, "yuv420p") == 0) opts.pixfmt = 0;
			else if (strcmp(v, "yuv444p") == 0) opts.pixfmt = 1;
			else { fprintf(stderr, "bad --pixfmt\n"); free(keyframes); return 1; }
		}
		else if (strncmp(a, "--jobs=", 7) == 0) opts.jobs = atoi(a + 7);
		else if (strcmp(a, "--keepframes") == 0) opts.keep_frames = 1;
		else if (strcmp(a, "--overwrite") == 0) opts.overwrite = 1;
		else if (strcmp(a, "--ignorebad") == 0) opts.ignore_bad = 1;
		else if (strcmp(a, "--dry-run") == 0) opts.dry_run = 1;
		else if (strcmp(a, "--render-only") == 0) opts.render_only = 1;
		else if (strncmp(a, "--single-frame=", 15) == 0) opts.single_frame = atoi(a + 15);
		else if (strncmp(a, "--config=", 9) == 0) opts.config_path = a + 9;
		else if (strncmp(a, "--save-config=", 14) == 0) opts.save_config_path = a + 14;
		else {
			fprintf(stderr, "unknown option: %s\n", a);
			print_usage(argv[0]);
			free(keyframes);
			return 1;
		}
	}

	if (nk < 2) {
		fprintf(stderr, "need at least two keyframes (got %d)\n", nk);
		print_usage(argv[0]);
		free(keyframes);
		return 1;
	}
	opts.keyframe_paths = keyframes;
	opts.keyframe_count = nk;

	int rc = anim_run(&opts);
	free(keyframes);
	return rc;
}
