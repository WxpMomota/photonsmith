#pragma once
// render_setup.h -- what every render mode of the launcher reads: the settings resolved from the command line once (RenderSetup), the renderer options built from the arguments, and the
// environment variables some options set for the renderers.

#include <cstdlib>
#include <string>

#include "cpu_renderer/cpu_interface.h"
#include "launcher/launcher_args.h"

// _putenv_s (MSVC CRT) has no POSIX equivalent - setenv() takes the same
// name/value pair with an added overwrite flag, always 1 here to match
// _putenv_s's unconditional-overwrite behavior.
inline void set_env_var(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

// Builds the RenderOptions the plain path-tracer branches pass to
// cpu_render_main()/optix_render_main() from the parsed CLI args. Shared by
// both the video-mode per-frame loop and the single-image dispatch below so
// the two call sites can't drift out of sync on which LaunchArgs fields feed
// RenderOptions.
inline RenderOptions render_options_from_args(const LaunchArgs& args) {
    RenderOptions render_opts;
    render_opts.exposure = args.exposure;
    render_opts.sampler = args.sampler.empty() ? nullptr : args.sampler.c_str();
    render_opts.adaptive_sampling = args.adaptive_sampling;
    render_opts.adaptive_threshold = args.adaptive_threshold;
    render_opts.time_limit_seconds = args.time_limit_seconds;
    render_opts.lightsampler = args.lightsampler.empty() ? nullptr : args.lightsampler.c_str();
    render_opts.regularize = args.regularize;
    render_opts.max_component_value = args.max_component_value;
    render_opts.crop_x0 = args.crop_x0;
    render_opts.crop_y0 = args.crop_y0;
    render_opts.crop_x1 = args.crop_x1;
    render_opts.crop_y1 = args.crop_y1;
    render_opts.seed = args.seed;
    render_opts.aperture_override = args.aperture_override;
    render_opts.focus_distance_override = args.focus_distance_override;
    render_opts.spectral = args.spectral;
    render_opts.tonemap = args.tonemap.empty() ? nullptr : args.tonemap.c_str();
    render_opts.denoise = args.denoise;
    render_opts.denoise_guides = args.denoise_guides;
    render_opts.denoise_blend = static_cast<float>(args.denoise_blend);
    render_opts.isolate_pbrt_lighting = args.isolate_pbrt_lighting;
    return render_opts;
}

// The settings every render mode reads, resolved from the command line once in main().
struct RenderSetup {
	bool use_gpu = false;
	int image_width = 0, image_height = 0, samples_per_pixel = 0, max_ray_depth = 0;
	std::string scene_id;
	double cam_x = 0.0, cam_y = 0.0, cam_z = 0.0;
	bool video_mode = false;
	int video_frames = 0, video_fps = 0;
	double video_speed = 1.0;
	std::string camera_path;
	std::string camera_keyframes_file;   // --camera-keyframes: the video's camera goes through the places in this file
	bool use_sppm = false, use_bdpt = false, use_mlt = false;
	bool use_randomwalk = false, use_ao = false, use_simplepath = false, use_simplevolpath = false, use_lightpath = false;
	bool use_debug_integrator = false;   // any one of the five debug integrators
	std::string out_path;
	std::string exe_path;   // this program, for a cartoon video's per-frame child runs
};
