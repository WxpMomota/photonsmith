#pragma once
// launcher_args.h -- Command-line argument parsing for the ray tracer launcher.
//
// Provides LaunchArgs struct and parse_launch_args() function.
// Call parse_launch_args(argc, argv) to get a fully populated LaunchArgs.
// Returns false if the program should exit (e.g., --help was passed).

#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>

#include "../src/shared/video_preset.h"
#include "../src/shared/cartoon/cartoons.h"   // --camera-path cartoon:<key>
#include "../src/shared/render_flag_names.h"
#include "../src/shared/lens_look.h"   // --bloom/--vignette/--warmth/--saturation
#include "../src/shared/scene_slugs.h"

namespace {
	constexpr int kDefaultWidth = 600;
	constexpr int kDefaultHeight = 600;
	constexpr int kDefaultSamplesPerPixel = 500;
	constexpr int kDefaultMaxDepth = 20;
	constexpr const char* kDefaultSceneId = "A1";
	constexpr double kCornellBoxCenter = 278.0;
	constexpr double kDefaultCameraX = 278.0;
	constexpr double kDefaultCameraY = 278.0;
	constexpr double kDefaultCameraZ = -800.0;
	constexpr int kDefaultSppmIterations = render_defaults::kSppmIterations;
	constexpr int kDefaultSppmPhotons = render_defaults::kSppmPhotons;
	// BDPT: smaller than kDefaultMaxDepth deliberately -- BDPT's per-sample
	// cost is O(maxDepth^2) (every (s,t) strategy pair up to that depth is
	// connected and MIS-weighted, unlike the path tracer's O(maxDepth) single
	// walk), so pbrt-v4's own CLI default (5) is a much better starting point
	// than reusing the path tracer's 20.
	constexpr int kDefaultBdptMaxDepth = render_defaults::kBdptMaxDepth;
	// MLT: pbrt-v4's own ballpark defaults (Render.cpp), scaled down for a
	// CPU-only render in this codebase's existing time budget -- see
	// launcher_args.h's --mlt-* help text for how to raise these for a
	// higher-quality (slower) render.
	constexpr int kDefaultMltBootstrap = render_defaults::kMltBootstrap;
	constexpr long long kDefaultMltMutations = render_defaults::kMltMutations;
	constexpr int kDefaultMltMaxDepth = render_defaults::kMltMaxDepth;
	// Round 6 Phase 2 debug integrators: RandomWalk/SimplePath share
	// kDefaultMaxDepth's own value (20) rather than BDPT/MLT's smaller
	// kDefaultBdptMaxDepth -- both are single-subpath integrators (like the
	// default path tracer, O(maxDepth) cost), not bidirectional, so there's
	// no O(maxDepth^2) cost blowup to defend against the way BDPT/MLT have.
	constexpr double kDefaultAoMaxDist = render_defaults::kAoMaxDist;   // pbrt-v4's own "effectively unbounded" default
	constexpr double kDefaultAoIllumScale = render_defaults::kAoIllumScale;

	// std::stoi/stoll/stod that take the whole token or throw std::invalid_argument: the std ones read a number off the front and ignore the rest, so "12abc" was 12 and "1,5" was 1.
	template <class T, class F>
	inline T parseWhole(const std::string &s, F parse) {
		std::size_t used = 0;
		const T v = parse(s, &used);
		if (used != s.size()) throw std::invalid_argument("not a number: " + s);
		return v;
	}
	// "--name", or "-x" (a letter after one dash: "-3.5", a negative camera coordinate, is a number)
	inline bool looks_like_flag(const std::string &arg) {
		return arg.size() >= 2 && arg[0] == '-' && (arg[1] == '-' || std::isalpha(static_cast<unsigned char>(arg[1])) != 0);
	}
	inline int strict_stoi(const std::string &s) { return parseWhole<int>(s, [](const std::string &t, std::size_t *n) { return std::stoi(t, n); }); }
	inline long long strict_stoll(const std::string &s) { return parseWhole<long long>(s, [](const std::string &t, std::size_t *n) { return std::stoll(t, n); }); }
	inline double strict_stod(const std::string &s) { return parseWhole<double>(s, [](const std::string &t, std::size_t *n) { return std::stod(t, n); }); }

	// Shared by --sampler/--lightsampler/--accelerator/--splitmethod below:
	// lowercase the raw argv value, check it against the flag's own valid
	// set, assign on a hit or print an "invalid, falling back to X" warning
	// naming every valid value on a miss. Consolidates what was 4
	// independently hand-copied ~8-line blocks (one per flag) into one.
	inline void parseEnumFlag(const std::string &rawValue, const std::set<std::string> &validValues,
							   std::string &outField, const std::string &flagName,
							   const std::string &invalidMessage) {
		std::string name = rawValue;
		std::transform(name.begin(), name.end(), name.begin(),
					   [](unsigned char c) { return std::tolower(c); });
		if (validValues.count(name)) {
			outField = name;
		} else {
			std::cerr << "Invalid " << flagName << " \"" << rawValue << "\", " << invalidMessage << "\n";
		}
	}

	// Shared by --denoise-blend/--exposure/--adaptive-threshold/--time-limit/
	// --maxcomponentvalue below: parse a single double-valued flag argument
	// (argv[i+1]) via std::stod, always consuming both argv[i] and argv[i+1]
	// (even on a parse failure, so an invalid value doesn't fall through into
	// positional argument parsing and shift width/spp/max_depth/scene_id),
	// and advancing i past the consumed value. Consolidates what was 5
	// independently hand-copied ~15-line try/std::stod/catch blocks (one per
	// flag) into one.
	//
	// `validate` receives the raw parsed value and returns the value to
	// actually store - either it unchanged, or a substituted fallback (after
	// printing its own "Warning: ..." message, matching this project's
	// existing "warn and use a sane default" convention rather than
	// rejecting the whole render). On a parse failure (non-numeric
	// argv[i+1]), `validate` is never called: `parseFailureMessage` is
	// printed, and outField is reset to `failureDefault` only if
	// `resetOnFailure` is true - some flags (--exposure/--maxcomponentvalue)
	// intentionally leave a prior successful value alone on a later failed
	// re-parse, matching their pre-existing behavior.
	inline void parseDoubleFlag(char **argv, int &i, std::set<int> &consumed_args,
								 double &outField, const std::string &parseFailureMessage,
								 bool resetOnFailure, double failureDefault,
								 const std::function<double(double)> &validate) {
		consumed_args.insert(i);
		consumed_args.insert(i + 1);
		try {
			outField = validate(strict_stod(argv[i + 1]));
		} catch (const std::exception&) {
			std::cerr << parseFailureMessage;
			if (resetOnFailure) outField = failureDefault;
		}
		++i;
	}
}

// (which flags a cartoon video's frame runs repeat: launcher/flag_scopes.h)

struct LaunchArgs {
	bool use_gpu            = true;
	bool force_cpu          = false;
	// GPU-only: use the wavefront (queue-based) path tracer instead of the
	// default recursive (megakernel) one - see gpu/optix/wavefront_path_tracer.h.
	// Ignored under --cpu/--sppm.
	bool use_wavefront      = false;
	// GPU-only: enable OptiX validation mode (extra device-side checks, real
	// per-launch cost) - see OptiXRenderer::createContext()'s own comment.
	// Ignored under --cpu/--sppm.
	bool optix_validate     = false;
	// Metal backend only (--gpu on macOS): skip the hardcoded demo room's
	// own lights (sun/fill/area/projection/goniometric, all unconditionally
	// composited under every scene by MetalPocApp::buildScene() so 80+
	// scene IDs can share one code path) when rendering a loaded pbrt
	// scene. Exists purely for isolating a pbrt scene's OWN lighting during
	// a comparison render - see docs/history/METAL_GPU_FEASIBILITY.md section 197's
	// own finding that the demo room's undying, no-falloff "sun" swamped
	// C9's tiny, correctly-calibrated real photometric light. Off by
	// default so every one of the 81 kSupported scene IDs' existing
	// hash-sweep-verified batch-render output is unaffected; deliberately
	// NOT listed in src/shared/render_flag_names.h since the GUI has no
	// control for it, same "CLI-only, not GUI-exposed" scope as --diagnose.
	bool isolate_pbrt_lighting = false;
	// GPU-only: run the OptiX AI denoiser on the finished render - see
	// OptiXRenderer::enableDenoise()'s comment (albedo/normal-guided AOV
	// denoising). Real support on both GPU backends (recursive and
	// wavefront - WavefrontPathTracer has its own independent denoiser
	// implementation, see that class's setDenoiseEnabled() comment);
	// ignored under --cpu/--sppm/--bdpt/--mlt like use_wavefront/
	// optix_validate above.
	bool denoise            = false;
	bool denoise_guides     = false;   // --denoise-guides: the CPU denoiser always gets the surface colour and normal (by default only for scenes whose visible surfaces are all matte)
	// OptiX's own blend between the noisy input and the fully denoised
	// output - see gpu/optix/optix_denoiser.h's runDenoiser() for the
	// value range/rationale. Only consulted when denoise is true; same
	// scope cut as denoise above. 0.0 (default) matches this project's
	// prior, only-ever-100%-denoised behavior exactly.
	double denoise_blend    = render_defaults::kDenoiseBlend;
	// Print a small end-of-render stats block (rays cast, bounces, shadow
	// rays, samples/sec) - see main.cpp's own block right after "RENDER
	// TIME". Observation-only: never changes what gets rendered, only what
	// gets printed after it's done. Wavefront already computes real
	// per-bounce counts (WavefrontRenderStats) for its own launch sizing;
	// this just decides whether to also print them. CPU counts real rays
	// via thread-local accumulators in camera.h's worker lambda. The
	// recursive GPU backend has no per-ray counting infra yet, so it only
	// prints the always-exact ray count (width*height*samples) here -
	// Russian Roulette (optix_raygen.h) means a bounce count can't be
	// inferred from launch parameters alone without one.
	bool stats              = false;
	// Flat post-multiply on linear color right before tone-mapping, applied
	// on both CPU and GPU output paths (camera.h / optix_interface.cpp) -
	// the default path tracer only. Mirrors pbrt-v4's PixelSensor::
	// imagingRatio = exposureTime * ISO / 100 (film.cpp) collapsed to a
	// single scalar - the only brightness knob this project has outside of
	// changing scene light intensity itself. Default 1.0 matches pbrt's own
	// passthrough default (no-op). Has no effect under --bdpt/--mlt/--sppm
	// (main.cpp warns) - none of those entry points take an exposure
	// parameter, same scope cut as denoise's own --wavefront exclusion above.
	double exposure         = 1.0;
	// --bloom/--vignette/--warmth/--saturation: the camera's look (src/shared/lens_look.h), over the scene file's own `# @rt-look` line; unset values keep the file's.
	lens_look::Given look;
	// Which ported pbrt-v4 sampler (src/shared/sobol_sampler.h,
	// stratified_sampler.h, pmj02_sampler.h, halton_sampler.h,
	// independent_sampler.h) drives camera.h's random decisions this
	// render - see camera.h's SamplerKind/
	// sampler_kind_from_name(). Empty (default) means "use sobol", this
	// project's pre-existing hardcoded default - CPU only, same "only the
	// default path tracer supports it" scope cut as exposure above (no GPU
	// sampler-selection exists yet, and BDPT/MLT/SPPM each have their own
	// sampling scheme already).
	std::string sampler     = "";
	// Stops sampling a pixel early once camera.h's pixel_convergence::
	// has_converged() decides its running luminance estimate has converged,
	// instead of always spending the full samples_per_pixel budget on every
	// pixel - see camera::adaptive_sampling's own comment (camera.h) and
	// src/shared/adaptive_sampling.h. Off by default: renders exactly
	// samples_per_pixel samples per pixel. The CPU path tracer and both
	// GPU renderers (GpuCameraParams::adaptiveThreshold); a film video's
	// frames turn it on by themselves on a GPU (cartoon_render.h, frameFlags).
	bool adaptive_sampling  = false;
	// --no-adaptive: a film video's frames sample every pixel samples_per_pixel times.
	bool no_adaptive = false;
	// Also write the render's passes (albedo, normal, depth, uv, coverage) - see src/TheRestOfYourLife/aov_pass.h. Into the same file when the output is an .exr
	// (a multilayer EXR with the beauty's R/G/B), else into <stem>.aovs.exr beside the image. Any backend; single images only.
	bool aovs = false;
	// The toon look on the picture (src/shared/toon_look.h, launcher/toon_frame.h): cel shading, ink lines, stronger colour - the scene's own `# @rt-toon` style, or the default.
	// A scene with that line has the look without the flag.
	bool toon = false;
	// The noise a pixel may keep, in display units (pixel_convergence_rule.h):
	// 0.003 (default) is under one level of 255. Only consulted when
	// adaptive_sampling is true.
	double adaptive_threshold = render_defaults::kAdaptiveThreshold;
	// Stops claiming new scanlines once this many seconds have elapsed
	// since the render started - see camera::time_limit_seconds's own
	// comment. <= 0.0 (default) means "no limit", unchanged prior
	// behavior. Same "CPU default path tracer only" scope cut as
	// adaptive_sampling above.
	double time_limit_seconds = 0.0;
	// pbrt-v4 Integrator "string lightsampler" - one of "uniform"/"power"/
	// "bvh", or "auto" (resolved per-scene in cpu_interface.cpp to the
	// loaded scene's own recommendation, falling back to bvh if it made
	// none - see that resolution's own comment). Empty (default) means
	// "use bvh", pbrt-v4's own real default - same "CPU default path
	// tracer only" scope cut as sampler above.
	std::string lightsampler = "";
	// Accelerator "bvh"/"kdtree" - overrides a loaded .pbrt scene's own
	// Accelerator directive (empty/default leaves it at whatever the scene
	// itself asked for, or "bvh" if it asked for nothing) - see
	// src/shared/accelerator_override.h and render_options.h's own comment.
	// Has no effect on a native (non-.pbrt) scene, which has no Accelerator
	// directive to override (cpu_interface.cpp warns rather than silently
	// ignoring it). All choices render the same converged image - a pure
	// perf/build-strategy knob, unlike sampler/lightsampler above.
	std::string accelerator = "";
	// Accelerator "bvh" "string splitmethod" - one of "sah"/"middle"/
	// "equal"/"hlbvh" - only consulted when the resolved accelerator type is
	// "bvh" (kdtree has no split method). Same override/scope shape as
	// `accelerator` just above.
	std::string splitmethod = "";
	// pbrt-v4 Integrator "bool regularize" (camera.h's camera_t::regularize)
	// as an explicit CLI request. A loaded .pbrt scene can already set this
	// itself via its own directive - this flag ONLY ever forces it ON, on
	// top of whatever the scene requested; it never turns a scene's own
	// "true" back off, since (unlike sampler/lightsampler/exposure above)
	// this is a genuine scene-authored behavior toggle, not a pure perf/
	// debug knob meant to be freely overridden either direction. Default
	// path tracer only, both backends (see render_options.h's own comment).
	bool regularize = false;
	// pbrt-v4 Film "maxcomponentvalue" (camera.h's camera_t::
	// max_component_value) - the firefly-clamp threshold. 1e9 (the
	// class's own default, "effectively unbounded") means "not explicitly
	// requested", so a scene's own Film directive still applies unless
	// this differs from 1e9 - same "explicit CLI value wins outright"
	// shape as --exposure, unlike --regularize's OR-only shape above,
	// since a numeric clamp threshold has an unambiguous "more/less
	// aggressive" ordering a boolean toggle doesn't. CPU default path
	// tracer only - GPU support now exists too - recursive is an exact per-sample clamp, wavefront a per-contribution approximation (GpuCameraParams::maxComponentValue's own comment, optix_types.h).
	double max_component_value = 1e9;
	// pbrt-v4 Film "cropwindow" - four NDC fractions in [0,1] (x0 y0 x1 y1)
	// restricting which pixels actually render; {0,0,1,1} (the full frame)
	// means "not explicitly requested", same "differs from the neutral
	// default" convention as max_component_value above, so a scene's own
	// cropwindow/pixelbounds directive still applies unless this differs.
	// Default path tracer only, both backends (crop is real on GPU too,
	// unlike max_component_value).
	double crop_x0 = 0.0, crop_y0 = 0.0, crop_x1 = 1.0, crop_y1 = 1.0;
	// --probe X Y: instead of an image, report where the light of ONE pixel comes from (src/shared/path_probe.h). -1 = not asked for. CPU default path tracer only.
	int probe_x = -1, probe_y = -1;
	// An explicit request for reproducible renders. -1 (default) means
	// "not requested" - see RenderOptions::seed's own comment
	// (render_options.h) for the full CPU/GPU determinism story this
	// fixes. Both backends, default path tracer only. parse_launch_args()
	// (below) clamps this to [0, 2147483647] at parse time - every actual
	// consumer (GpuCameraParams::userSeed, camera_t's alternate-sampler
	// seeding) narrows to a 32-bit int, so a value is validated here
	// rather than silently wrapping (possibly negative) further downstream.
	long long seed = -1;
	// --gpu-device N: which GPU an NVIDIA renderer uses (the CUDA device number, 0 = the first; see `nvidia-smi`); -1 = not given (RAY_TRACER_GPU_DEVICE, else 0). Passed to the renderer
	// as RAY_TRACER_GPU_DEVICE, which the programs a video starts inherit.
	int gpu_device = -1;
	// --worker-max-mb N: a cartoon video's worker that holds more than N megabytes is replaced between two frames; 0 = never; -1 = not given (RAY_TRACER_WORKER_MAX_MB, else never).
	int worker_max_mb = -1;
	// An explicit override for the scene's own camera lens diameter
	// ("lensradius"*2, world units) / focus distance - see
	// RenderOptions::aperture_override's own comment (render_options.h)
	// for the full "-1 = not requested, unlike max_component_value's 1e9"
	// reasoning and the scene-file-only scope cut. parse_launch_args()
	// (below) rejects a negative --aperture value at parse time (a
	// negative lens diameter is meaningless, unlike --focus-distance
	// which only needs to reject <= 0 since focus distance 0 is
	// meaningless too, not just negative).
	double aperture_override = -1.0;
	double focus_distance_override = -1.0;
	// Real hero-wavelength spectral rendering (camera.h's ray_color_spectral(),
	// see its own comment) instead of the default flat-RGB ray_color() -
	// CPU default path tracer only, same scope cut as exposure/sampler
	// above. Only lambertian/metal/dielectric/rough_dielectric/conductor/
	// diffuse_light scenes are supported - cpu_interface.cpp scans the
	// loaded scene tree and refuses to render (with a named-material error,
	// not silent RGB fallback) before this is ever consulted if the scene
	// uses anything else.
	bool spectral           = false;
	// Which tone-mapping operator write_color() applies before the sRGB
	// OETF (src/shared/tone_map.h's ToneMapMode) - "aces" (default, this
	// project's pre-existing hardcoded behavior)/"reinhard"/"none". Unlike
	// exposure/sampler/spectral above, this reaches BOTH the CPU default
	// path tracer and GPU (gpu/optix/optix_interface.cpp's pixel-writeback
	// loop is the single shared output path for both the recursive and
	// wavefront backends) - see main.cpp's threading of this value into
	// cpu_render_main()/optix_render_main(). Still has no effect under
	// --bdpt/--mlt/--sppm/the debug integrators (main.cpp warns), same
	// scope cut as exposure/sampler.
	std::string tonemap     = "aces";
	bool video_mode         = false;
	// Stochastic Progressive Photon Mapping - a separate render mode (see
	// cpu_renderer/cpu_interface.h's cpu_render_main_sppm() doc comment for
	// CPU, gpu/optix/optix_interface.cpp's optix_render_main_sppm() for
	// GPU), not a flag on the existing --cpu/--gpu path tracer -- --gpu
	// selects which SPPM backend runs, same as it does for the path tracer.
	// Takes priority over use_gpu/force_cpu when set (main.cpp checks it
	// first).
	bool   use_sppm         = false;
	int    sppm_iterations  = kDefaultSppmIterations;
	int    sppm_photons     = kDefaultSppmPhotons;
	// Bidirectional Path Tracing - another separate CPU-only render mode
	// (see cpu_renderer/cpu_interface.h's cpu_render_main_bdpt() doc
	// comment), same "takes priority over use_gpu/force_cpu, mutually
	// exclusive with the other special render modes" shape as use_sppm.
	// CPU only - no GPU/OptiX implementation exists (see --gpu handling in
	// main.cpp and this struct's own validation below).
	bool   use_bdpt         = false;
	int    bdpt_max_depth   = kDefaultBdptMaxDepth;
	// Metropolis Light Transport - same shape as use_bdpt/use_sppm above.
	bool   use_mlt          = false;
	int    mlt_bootstrap    = kDefaultMltBootstrap;
	long long mlt_mutations = kDefaultMltMutations;
	int    mlt_max_depth    = kDefaultMltMaxDepth;
	// Round 6 Phase 2 -- 5 debug/reference integrators (see
	// src/TheRestOfYourLife/bdpt_adapter.h's own "Round 6 Phase 2" comment
	// for the render-loop drivers, cpu_interface.h's cpu_render_main_*
	// doc comments for each integrator's own scope). Same "separate
	// CPU-only render mode, mutually exclusive with the others, takes
	// priority over use_gpu/force_cpu" shape as use_bdpt/use_mlt/use_sppm.
	bool   use_randomwalk    = false;
	bool   use_ao            = false;
	double ao_max_dist       = kDefaultAoMaxDist;
	bool   ao_cosine         = true;    // pbrt-v4 AmbientOcclusionIntegrator's own default (cossample=true)
	double ao_illum_scale    = kDefaultAoIllumScale;
	double ao_illum_r        = 1.0;
	double ao_illum_g        = 1.0;
	double ao_illum_b        = 1.0;
	bool   use_simplepath    = false;
	bool   simplepath_sample_lights = true;   // pbrt-v4 SimplePathIntegrator's own default
	bool   simplepath_sample_bsdf   = true;   // pbrt-v4 SimplePathIntegrator's own default
	bool   use_simplevolpath = false;
	bool   use_lightpath     = false;
	// System-compatibility report (see launcher/diagnostics.h) instead of a
	// render - prints OS/CPU/GPU/CUDA/OptiX/disk/scene-asset info and exits.
	// Reuses custom_output_path (below) for --output rather than a second
	// flag: main.cpp checks this before any of the render-mode fields are
	// otherwise used.
	bool   diagnose         = false;
	int  video_frames       = 120;
	int  video_fps          = 30;
	double video_speed      = 1.0;
	std::string camera_path = "orbit";
	// True if --camera-path/-p or --video-preset was actually passed (both
	// set camera_path explicitly, below) - lets main.cpp tell "the user
	// picked this" apart from "still sitting at the hardcoded default
	// above", so it can fall back to the SELECTED scene's own curated
	// recommendation (scene_registry.h's recommended_camera_path_for(),
	// via cpu_scene_recommended_camera_path_by_id()) only when nothing
	// more specific was requested, without ever overriding a real user
	// choice - including an explicit `--camera-path orbit` that happens to
	// spell out the same value the hardcoded default already has.
	bool camera_path_explicit = false;
	// --camera-keyframes FILE: the video's camera goes through the places listed in this file (src/shared/camera_keyframes.h) instead of one of the named paths;
	// the first frame is the first place and the last frame the last. Empty when not given.
	std::string camera_keyframes_file;
	std::string custom_output_path;
	// Every command-line token the parser consumed (flags and their values, in order, positional arguments left out): what a cartoon video repeats on each frame's own run
	// (launcher/cartoon_render.h).
	std::vector<std::string> consumed_tokens;

	int    image_width       = kDefaultWidth;
	int    image_height      = kDefaultHeight;
	int    samples_per_pixel = kDefaultSamplesPerPixel;
	int    max_ray_depth     = kDefaultMaxDepth;
	std::string scene_id     = kDefaultSceneId;
	// A .pbrt file named in the scene_id position instead of an id (a scene made in the GUI's Scene Builder, or any pbrt-v4 file). main.cpp registers it
	// with cpu_register_scene_file() and sets scene_id to the id it gets; empty when the scene was named by id.
	std::string scene_file;
	// A cartoon frame named in the scene_id position ("cartoon:fireflies" or "cartoon:fireflies@13.5": src/shared/cartoon/cartoons.h, parseStill). main.cpp writes that frame's scene
	// to a temporary .pbrt file and renders it as scene_file; empty when the scene was named any other way.
	std::string cartoon_still;
	// --cartoon-jobs N: how many frames of a cartoon video are rendered at once; 0 = automatic (launcher/cartoon_render.h, jobsFor).
	int cartoon_jobs = 0;
	// --cartoon-timeout S: a frame of a cartoon video that has not finished after S seconds is stopped and tried once more; 0 = never; -1 = automatic (launcher/cartoon_render.h, timeoutFor).
	int cartoon_timeout = -1;
	// --resume: continue an interrupted cartoon video from the frames it already rendered (when the settings and the film are the same), instead of starting again.
	bool cartoon_resume = false;
	// --draft N: a cartoon video renders one frame in N and repeats it in the others (launcher/video_render.h, renderCartoonFrames); 1 = every frame.
	int draft_step = 1;
	// --temporal-denoise [R]: a cartoon video's frames are denoised with the R frames either side in their shot (launcher/film_temporal.h; R 1 to 3, 2 when not given); 0 = not.
	int temporal_radius = 0;
	// --ppm-only: write the picture as a PPM and no PNG beside it. A cartoon video's frame runs are started with it (the video turns each PPM into its numbered PNG itself and deletes the
	// frame run's own PNG, so making one was a second encode of every frame); people have no use for it.
	bool ppm_only = false;
	// --preview: a quick look at a film: its sculpted characters three times coarser (RAY_TRACER_FILM_PREVIEW, cartoon_lib.h previewCoarsening; set for the programs the
	// video starts too), its frames in a folder of their own (frames-<key>-preview). With a small size, few samples and --draft: a whole film in minutes (film.py preview).
	bool preview = false;
	// --background: be a polite guest on the computer (launcher/background_mode.h): lower CPU and GPU priority, half the CPU cores, one worker and one frame at a time. Passed on to every program
	// a video starts. RAY_TRACER_BACKGROUND=1 does the same.
	bool background = false;
	// --cartoon-worker JOBFILE: render the frames the job file lists, one after the other in this one process (launcher/cartoon_worker.h). Started by a cartoon video, not by people.
	std::string cartoon_worker;
	// --cartoon-workers N: render a cartoon video's frames with N long-lived worker processes (the default on a GPU) instead of a process per frame; 0 = a process per frame; -1 = automatic.
	int cartoon_workers = -1;
	double cam_x             = kDefaultCameraX;
	double cam_y             = kDefaultCameraY;
	double cam_z             = kDefaultCameraZ;
	// True only if the user actually passed cam_x/y/z positional args - lets
	// main.cpp tell "user explicitly chose this camera position" (honor it
	// regardless of scene) apart from "cam_x/y/z are just sitting at their
	// generic Cornell-Box-scale struct defaults" (fall back to the selected
	// scene's own recommended camera instead of forcing this default onto
	// every scene, which would misplace it just as badly as a stale GUI
	// spinbox value would for a much smaller scene).
	//
	// How this relates to the renderer's other two camera-authority
	// concepts, so all three don't have to be puzzled out independently:
	//   - CameraConfig::mode (Fixed/UserControlled, scene_registry.h) is
	//     the SCENE's own default posture: does this scene want manual
	//     camera control at all, absent any override?
	//   - cam_explicit (here) and force_camera_override (cpu_interface.h /
	//     optix_interface.h / scene_builder.h) are the same question - "does
	//     THIS render call override that default?" - asked at two different
	//     layers: cam_explicit at CLI-parse time (did the user supply
	//     cam_x/y/z at all), force_camera_override at render-call time
	//     (main.cpp sets it from cam_explicit for single images, and
	//     unconditionally for every video frame, which must always honor
	//     its own per-frame animated camera regardless of scene mode).
	// The Qt GUI (qt_gui/mainwindow_slots.cpp's onRenderClicked) computes
	// its own explicit-ness by comparing the camera spinboxes against the
	// scene's recommended default (via scene_metadata.dll) and only sends
	// cam_x/y/z when they actually differ - otherwise this fallback path
	// would never run for GUI-launched renders, since every GUI render
	// would otherwise arrive with cam_x/y/z always present.
	bool   cam_explicit      = false;

	// True only if the user actually passed --gpu/-gpu - lets main.cpp warn
	// specifically when --gpu was a deliberate choice that --bdpt/--mlt (no
	// GPU implementation at all - see cpu_interface.h's cpu_render_main_bdpt()
	// doc comment) can't honor, without spamming that warning on the common
	// `--bdpt` invocation, where use_gpu just sits at its own struct default.
	bool   gpu_flag_explicit = false;
};

// What a group of flag handlers made of one argument.
enum class FlagResult { NotMine, Handled, Error };

// Backend, output, diagnose, denoise and stats flags: returns whether it handled `arg` (advancing `i` past a value it consumed).
inline FlagResult handle_basic_flag(const std::string& arg, int argc, char** argv, int& i, std::set<int>& consumed_args, LaunchArgs& out) {
	if (arg == render_flags::kCpu || arg == "-cpu") {
		out.force_cpu = true;
		out.use_gpu   = false;
		consumed_args.insert(i);
	} else if (arg == render_flags::kGpu || arg == "-gpu") {
		out.use_gpu   = true;
		out.force_cpu = false;
		out.gpu_flag_explicit = true;
		consumed_args.insert(i);
	} else if ((arg == render_flags::kOutput || arg == "-o") && i + 1 < argc) {
		out.custom_output_path = argv[i + 1];
		consumed_args.insert(i);
		consumed_args.insert(i + 1);
		++i;
	} else if (arg == render_flags::kDiagnose) {
		out.diagnose = true;
		consumed_args.insert(i);
	} else if (arg == render_flags::kWavefront) {
		out.use_wavefront = true;
		consumed_args.insert(i);
	} else if (arg == render_flags::kOptixValidate) {
		out.optix_validate = true;
		consumed_args.insert(i);
	} else if (arg == "--isolate-pbrt-lighting") {
		out.isolate_pbrt_lighting = true;
		consumed_args.insert(i);
	} else if (arg == render_flags::kDenoise) {
		out.denoise = true;
		consumed_args.insert(i);
	} else if (arg == render_flags::kDenoiseGuides) {
		out.denoise_guides = true;
		consumed_args.insert(i);
	} else if (arg == render_flags::kDenoiseBlend && i + 1 < argc) {
		parseDoubleFlag(argv, i, consumed_args, out.denoise_blend,
						"Invalid --denoise-blend value, using default (0.0)\n", true, 0.0,
						[](double v) {
							if (v < 0.0 || v > 1.0) {
								std::cerr << "Warning: --denoise-blend " << v
										  << " is outside [0,1] (0 = fully denoised, 1 = original noisy "
											 "image) - using default (0.0)\n";
								return 0.0;
							}
							return v;
						});
	} else if (arg == render_flags::kStats) {
		out.stats = true;
		consumed_args.insert(i);
	} else {
		return FlagResult::NotMine;
	}
	return FlagResult::Handled;
}

// Sampling, exposure, accelerator, crop, seed, tone map and other quality flags: returns whether it handled `arg` (advancing `i` past a value it consumed).
inline FlagResult handle_quality_flag(const std::string& arg, int argc, char** argv, int& i, std::set<int>& consumed_args, LaunchArgs& out) {
	// The camera's look (lens_look.h): each value is clamped to its range there; a value that is not a number is ignored with a warning.
	struct LookFlag { const char *name; double *field; double lo, hi; };
	const LookFlag lookFlags[] = {{render_flags::kBloom, &out.look.bloom, 0.0, 1.0}, {render_flags::kVignette, &out.look.vignette, 0.0, 1.0},
								  {render_flags::kWarmth, &out.look.warmth, -1.0, 1.0}, {render_flags::kSaturation, &out.look.saturation, 0.0, 3.0}};
	for (const LookFlag &f : lookFlags) {
		if (arg == f.name && i + 1 < argc) {
			const double lo = f.lo, hi = f.hi;
			parseDoubleFlag(argv, i, consumed_args, *f.field, std::string("Invalid ") + f.name + " value, ignored\n", false, 0.0,
							[lo, hi](double v) { return (std::min)(hi, (std::max)(lo, v)); });
			return FlagResult::Handled;
		}
	}
	if (arg == render_flags::kExposure && i + 1 < argc) {
		// exposure <= 0 is a valid double but not a valid exposure -
		// linear_to_srgb clamps non-positive input to 0, so this would
		// otherwise silently render solid black with nothing telling
		// the user their value was nonsensical.
		parseDoubleFlag(argv, i, consumed_args, out.exposure,
						"Invalid --exposure value, using default\n", false, 0.0,
						[](double v) {
							if (v <= 0.0) {
								std::cerr << "Warning: --exposure " << v
										  << " is <= 0, image will render solid black\n";
							}
							return v;
						});
	} else if (arg == render_flags::kSampler && i + 1 < argc) {
		static const std::set<std::string> kValidSamplers = {
			"sobol", "zsobol", "paddedsobol", "stratified", "pmj02bn", "halton", "independent"};
		parseEnumFlag(argv[i + 1], kValidSamplers, out.sampler, "--sampler",
					  "using default (sobol). Valid: sobol, zsobol, paddedsobol, stratified, "
					  "pmj02bn, halton, independent");
		consumed_args.insert(i);
		consumed_args.insert(i + 1);
		++i;
	} else if (arg == render_flags::kAovs) {
		out.aovs = true;
		consumed_args.insert(i);
	} else if (arg == render_flags::kToon) {
		out.toon = true;
		consumed_args.insert(i);
	} else if (arg == render_flags::kAdaptive) {
		out.adaptive_sampling = true;
		consumed_args.insert(i);
	} else if (arg == render_flags::kNoAdaptive) {
		out.no_adaptive = true;
		out.adaptive_sampling = false;
		consumed_args.insert(i);
	} else if (arg == render_flags::kAdaptiveThreshold && i + 1 < argc) {
		// On a parse failure, still reset to the default the error
		// message claims (a prior --adaptive-threshold's value must not
		// survive this one failing to parse) - resetOnFailure=true.
		parseDoubleFlag(argv, i, consumed_args, out.adaptive_threshold,
						"Invalid --adaptive-threshold value, using default (0.003)\n", true, render_defaults::kAdaptiveThreshold,
						[](double v) {
							if (v <= 0.0) {
								std::cerr << "Warning: --adaptive-threshold " << v
										  << " is <= 0, every pixel would need a perfectly zero-variance "
											 "estimate to ever stop early - using default (0.003)\n";
								return render_defaults::kAdaptiveThreshold;
							}
							return v;
						});
	} else if (arg == render_flags::kTimeLimit && i + 1 < argc) {
		parseDoubleFlag(argv, i, consumed_args, out.time_limit_seconds,
						"Invalid --time-limit value, disabling the time limit\n", true, 0.0,
						[](double v) {
							if (v <= 0.0) {
								std::cerr << "Warning: --time-limit " << v
										  << " is <= 0, disabling the time limit (renders until every "
											 "scanline is done)\n";
								return 0.0;
							}
							return v;
						});
	} else if (arg == "--lightsampler" && i + 1 < argc) {
		// "auto" isn't a real light-sampler implementation - it means
		// "use whatever the scene's own Integrator \"string lightsampler\"
		// parameter requested" (falling back to bvh if it made no
		// request), resolved per-scene in cpu_render_main() once the
		// scene's own recommendation is known. See that resolution's own
		// comment for why this is opt-in rather than the default: an
		// explicit --lightsampler (or none at all) still always wins,
		// matching maxdepth/samplerType's own "CLI decides, scene's
		// request is only advisory" precedent - "auto" is what lets a
		// CLI user opt into the scene's request instead, the same
		// convenience the GUI's "Apply recommended settings" button
		// already gives GUI users.
		static const std::set<std::string> kValidLightSamplers = {"uniform", "power", "bvh", "auto"};
		parseEnumFlag(argv[i + 1], kValidLightSamplers, out.lightsampler, "--lightsampler",
					  "using default (bvh). Valid: uniform, power, bvh, auto");
		consumed_args.insert(i);
		consumed_args.insert(i + 1);
		++i;
	} else if (arg == render_flags::kAccelerator && i + 1 < argc) {
		static const std::set<std::string> kValidAccelerators = {"bvh", "kdtree"};
		parseEnumFlag(argv[i + 1], kValidAccelerators, out.accelerator, "--accelerator",
					  "leaving the scene's own Accelerator directive (or its bvh default) "
					  "untouched. Valid: bvh, kdtree");
		consumed_args.insert(i);
		consumed_args.insert(i + 1);
		++i;
	} else if (arg == render_flags::kSplitMethod && i + 1 < argc) {
		static const std::set<std::string> kValidSplitMethods = {"sah", "middle", "equal", "hlbvh"};
		parseEnumFlag(argv[i + 1], kValidSplitMethods, out.splitmethod, "--splitmethod",
					  "leaving the scene's own splitmethod (or its sah default) untouched. "
					  "Valid: sah, middle, equal, hlbvh");
		consumed_args.insert(i);
		consumed_args.insert(i + 1);
		++i;
	} else if (arg == render_flags::kRegularize) {
		out.regularize = true;
		consumed_args.insert(i);
	} else if (arg == render_flags::kMaxComponentValue && i + 1 < argc) {
		parseDoubleFlag(argv, i, consumed_args, out.max_component_value,
						"Invalid --maxcomponentvalue value, using default\n", false, 0.0,
						[](double v) {
							if (v <= 0.0) {
								std::cerr << "Warning: --maxcomponentvalue " << v
										  << " is <= 0, every sample will clamp to black\n";
							}
							return v;
						});
	} else if (arg == render_flags::kAperture && i + 1 < argc) {
		parseDoubleFlag(argv, i, consumed_args, out.aperture_override,
						"Invalid --aperture value, ignoring\n", false, -1.0,
						[](double v) {
							if (v < 0.0) {
								std::cerr << "Warning: --aperture " << v
										  << " is negative (a lens diameter can't be), ignoring\n";
								return -1.0;
							}
							return v;
						});
	} else if (arg == render_flags::kFocusDistance && i + 1 < argc) {
		parseDoubleFlag(argv, i, consumed_args, out.focus_distance_override,
						"Invalid --focus-distance value, ignoring\n", false, -1.0,
						[](double v) {
							if (v <= 0.0) {
								std::cerr << "Warning: --focus-distance " << v
										  << " is <= 0, ignoring\n";
								return -1.0;
							}
							return v;
						});
	} else if (arg == render_flags::kCrop && i + 4 < argc) {
		try {
			const double x0 = strict_stod(argv[i + 1]);
			const double y0 = strict_stod(argv[i + 2]);
			const double x1 = strict_stod(argv[i + 3]);
			const double y1 = strict_stod(argv[i + 4]);
			if (x0 < 0.0 || y0 < 0.0 || x1 > 1.0 || y1 > 1.0 || x0 >= x1 || y0 >= y1) {
				std::cerr << "Invalid --crop " << x0 << " " << y0 << " " << x1 << " " << y1
						  << " (need 0 <= x0 < x1 <= 1 and 0 <= y0 < y1 <= 1), using full frame\n";
			} else {
				out.crop_x0 = x0;
				out.crop_y0 = y0;
				out.crop_x1 = x1;
				out.crop_y1 = y1;
			}
			consumed_args.insert(i);
			consumed_args.insert(i + 1);
			consumed_args.insert(i + 2);
			consumed_args.insert(i + 3);
			consumed_args.insert(i + 4);
			i += 4;
		} catch (const std::exception&) {
			std::cerr << "Invalid --crop value, using full frame\n";
		}
	} else if (arg == render_flags::kProbe && i + 2 < argc) {
		try {
			out.probe_x = strict_stoi(argv[i + 1]);
			out.probe_y = strict_stoi(argv[i + 2]);
			if (out.probe_x < 0 || out.probe_y < 0) {
				std::cerr << "Invalid --probe " << out.probe_x << " " << out.probe_y << " (the pixel's column and row, counted from the top left, must not be negative)\n";
				return FlagResult::Error;
			}
			consumed_args.insert(i);
			consumed_args.insert(i + 1);
			consumed_args.insert(i + 2);
			i += 2;
		} catch (const std::exception&) {
			std::cerr << "Invalid --probe value (expected two whole numbers: the pixel's column and row)\n";
			return FlagResult::Error;
		}
	} else if (arg == "--worker-max-mb" && i + 1 < argc) {
		try {
			out.worker_max_mb = (std::max)(0, strict_stoi(argv[i + 1]));
			consumed_args.insert(i);
			consumed_args.insert(i + 1);
			++i;
		} catch (const std::exception&) {
			std::cerr << "Invalid --worker-max-mb value, using the automatic limit\n";
		}
	} else if (arg == "--gpu-device" && i + 1 < argc) {
		try {
			out.gpu_device = strict_stoi(argv[i + 1]);
			if (out.gpu_device < 0) {
				std::cerr << "Invalid --gpu-device " << out.gpu_device << " (must be 0 or more), using the first GPU\n";
				out.gpu_device = -1;
			}
			consumed_args.insert(i);
			consumed_args.insert(i + 1);
			++i;
		} catch (const std::exception&) {
			std::cerr << "Invalid --gpu-device value, using the first GPU\n";
		}
	} else if (arg == render_flags::kSeed && i + 1 < argc) {
		try {
			out.seed = strict_stoll(argv[i + 1]);
			// Upper bound matches every actual consumer of this value:
			// GpuCameraParams::userSeed (gpu/optix/optix_types.h) is a
			// 32-bit int, and camera_t's alternate-sampler seeding
			// (camera.h's alt_sampler_seed) also narrows to int - a
			// seed above INT32_MAX would otherwise silently wrap to a
			// negative value there, which then reads as "no seed
			// requested" and gets silently dropped instead of used
			// (the bug this bound exists to prevent). The GUI's own
			// spinbox (mainwindow_tabs_render.cpp) already caps input
			// at this same 2147483647, so this just brings the raw CLI
			// path in line with it.
			if (out.seed < 0 || out.seed > 2147483647LL) {
				std::cerr << "Invalid --seed " << out.seed << " (must be in [0, 2147483647]), ignoring\n";
				out.seed = -1;
			}
			consumed_args.insert(i);
			consumed_args.insert(i + 1);
			++i;
		} catch (const std::exception&) {
			std::cerr << "Invalid --seed value, ignoring\n";
		}
	} else if (arg == render_flags::kTonemap && i + 1 < argc) {
		std::string name = argv[i + 1];
		std::transform(name.begin(), name.end(), name.begin(),
						[](unsigned char c) { return std::tolower(c); });
		static const std::set<std::string> kValidTonemaps = {"aces", "reinhard", "none"};
		if (kValidTonemaps.count(name)) {
			out.tonemap = name;
		} else {
			std::cerr << "Invalid --tonemap \"" << argv[i + 1] << "\", using default (aces). "
						 "Valid: aces, reinhard, none\n";
		}
		consumed_args.insert(i);
		consumed_args.insert(i + 1);
		++i;
	} else if (arg == render_flags::kSpectral) {
		out.spectral = true;
		consumed_args.insert(i);
	} else {
		return FlagResult::NotMine;
	}
	return FlagResult::Handled;
}

// The integrator choices and their options (--sppm, --bdpt, --mlt, --ao, ...): returns whether it handled `arg` (advancing `i` past a value it consumed).
inline FlagResult handle_integrator_flag(const std::string& arg, int argc, char** argv, int& i, std::set<int>& consumed_args, LaunchArgs& out) {
	if (arg == "--sppm") {
		out.use_sppm = true;
		consumed_args.insert(i);
	} else if (arg == "--sppm-iterations" && i + 1 < argc) {
		try {
			out.sppm_iterations = strict_stoi(argv[i + 1]);
			consumed_args.insert(i);
			consumed_args.insert(i + 1);
			++i;
		} catch (const std::exception&) {
			std::cerr << "Invalid --sppm-iterations value, using default\n";
		}
	} else if (arg == "--sppm-photons" && i + 1 < argc) {
		try {
			out.sppm_photons = strict_stoi(argv[i + 1]);
			consumed_args.insert(i);
			consumed_args.insert(i + 1);
			++i;
		} catch (const std::exception&) {
			std::cerr << "Invalid --sppm-photons value, using default\n";
		}
	} else if (arg == "--bdpt") {
		out.use_bdpt = true;
		consumed_args.insert(i);
	} else if (arg == "--bdpt-max-depth" && i + 1 < argc) {
		try {
			out.bdpt_max_depth = strict_stoi(argv[i + 1]);
			consumed_args.insert(i);
			consumed_args.insert(i + 1);
			++i;
		} catch (const std::exception&) {
			std::cerr << "Invalid --bdpt-max-depth value, using default\n";
		}
	} else if (arg == "--mlt") {
		out.use_mlt = true;
		consumed_args.insert(i);
	} else if (arg == "--mlt-bootstrap" && i + 1 < argc) {
		try {
			out.mlt_bootstrap = strict_stoi(argv[i + 1]);
			consumed_args.insert(i);
			consumed_args.insert(i + 1);
			++i;
		} catch (const std::exception&) {
			std::cerr << "Invalid --mlt-bootstrap value, using default\n";
		}
	} else if (arg == "--mlt-mutations" && i + 1 < argc) {
		try {
			out.mlt_mutations = strict_stoll(argv[i + 1]);
			consumed_args.insert(i);
			consumed_args.insert(i + 1);
			++i;
		} catch (const std::exception&) {
			std::cerr << "Invalid --mlt-mutations value, using default\n";
		}
	} else if (arg == "--mlt-max-depth" && i + 1 < argc) {
		try {
			out.mlt_max_depth = strict_stoi(argv[i + 1]);
			consumed_args.insert(i);
			consumed_args.insert(i + 1);
			++i;
		} catch (const std::exception&) {
			std::cerr << "Invalid --mlt-max-depth value, using default\n";
		}
	} else if (arg == "--randomwalk") {
		out.use_randomwalk = true;
		consumed_args.insert(i);
	} else if (arg == "--ao") {
		out.use_ao = true;
		consumed_args.insert(i);
	} else if (arg == "--ao-max-dist" && i + 1 < argc) {
		try {
			out.ao_max_dist = strict_stod(argv[i + 1]);
			consumed_args.insert(i);
			consumed_args.insert(i + 1);
			++i;
		} catch (const std::exception&) {
			std::cerr << "Invalid --ao-max-dist value, using default\n";
		}
	} else if (arg == "--ao-uniform") {
		out.ao_cosine = false;
		consumed_args.insert(i);
	} else if (arg == "--ao-illum-scale" && i + 1 < argc) {
		try {
			out.ao_illum_scale = strict_stod(argv[i + 1]);
			consumed_args.insert(i);
			consumed_args.insert(i + 1);
			++i;
		} catch (const std::exception&) {
			std::cerr << "Invalid --ao-illum-scale value, using default\n";
		}
	} else if (arg == "--ao-illum-rgb" && i + 3 < argc) {
		try {
			out.ao_illum_r = strict_stod(argv[i + 1]);
			out.ao_illum_g = strict_stod(argv[i + 2]);
			out.ao_illum_b = strict_stod(argv[i + 3]);
			consumed_args.insert(i);
			consumed_args.insert(i + 1);
			consumed_args.insert(i + 2);
			consumed_args.insert(i + 3);
			i += 3;
		} catch (const std::exception&) {
			std::cerr << "Invalid --ao-illum-rgb value, using default\n";
		}
	} else if (arg == "--simplepath") {
		out.use_simplepath = true;
		consumed_args.insert(i);
	} else if (arg == "--simplepath-no-lights") {
		out.simplepath_sample_lights = false;
		consumed_args.insert(i);
	} else if (arg == "--simplepath-no-bsdf") {
		out.simplepath_sample_bsdf = false;
		consumed_args.insert(i);
	} else if (arg == "--simplevolpath") {
		out.use_simplevolpath = true;
		consumed_args.insert(i);
	} else if (arg == "--lightpath") {
		out.use_lightpath = true;
		consumed_args.insert(i);
	} else {
		return FlagResult::NotMine;
	}
	return FlagResult::Handled;
}

// Image height and the video flags: returns whether it handled `arg` (advancing `i` past a value it consumed).
inline FlagResult handle_image_and_video_flag(const std::string& arg, int argc, char** argv, int& i, std::set<int>& consumed_args, LaunchArgs& out, int& explicit_height) {
	if (arg == "--height" && i + 1 < argc) {
		try {
			const int h = strict_stoi(argv[i + 1]);
			if (h > 0) explicit_height = h;
			consumed_args.insert(i);
			consumed_args.insert(i + 1);
			++i;
		} catch (const std::exception&) {
			std::cerr << "Invalid --height, using the width\n";
		}
	} else if (arg == render_flags::kVideo) {
		out.video_mode = true;
		consumed_args.insert(i);
	} else if ((arg == render_flags::kFrames || arg == "-f") && i + 1 < argc) {
		try {
			out.video_frames = strict_stoi(argv[i + 1]);
			consumed_args.insert(i);
			consumed_args.insert(i + 1);
			++i;
		} catch (const std::exception&) {
			std::cerr << "Invalid frame count, using default\n";
		}
	} else if (arg == "--cartoon-jobs" && i + 1 < argc) {
		try {
			out.cartoon_jobs = strict_stoi(argv[i + 1]);
			consumed_args.insert(i);
			consumed_args.insert(i + 1);
			++i;
		} catch (const std::exception&) {
			std::cerr << "Invalid --cartoon-jobs, using the automatic number\n";
		}
	} else if (arg == "--cartoon-timeout" && i + 1 < argc) {
		try {
			out.cartoon_timeout = std::max(0, strict_stoi(argv[i + 1]));
			consumed_args.insert(i);
			consumed_args.insert(i + 1);
			++i;
		} catch (const std::exception&) {
			std::cerr << "Invalid --cartoon-timeout, using the automatic one\n";
		}
	} else if (arg == "--cartoon-worker" && i + 1 < argc) {
		out.cartoon_worker = argv[i + 1];
		consumed_args.insert(i);
		consumed_args.insert(i + 1);
		++i;
	} else if (arg == "--cartoon-workers" && i + 1 < argc) {
		try {
			out.cartoon_workers = std::max(0, strict_stoi(argv[i + 1]));
			consumed_args.insert(i);
			consumed_args.insert(i + 1);
			++i;
		} catch (const std::exception&) {
			std::cerr << "Invalid --cartoon-workers, using the automatic number\n";
		}
	} else if (arg == "--resume") {
		out.cartoon_resume = true;
		consumed_args.insert(i);
	} else if (arg == "--temporal-denoise") {
		out.temporal_radius = 2;
		consumed_args.insert(i);
		if (i + 1 < argc && std::strlen(argv[i + 1]) == 1 && argv[i + 1][0] >= '1' && argv[i + 1][0] <= '3') {   // (an optional radius: a single digit, never a size)
			out.temporal_radius = argv[i + 1][0] - '0';
			consumed_args.insert(i + 1);
			++i;
		}
	} else if (arg == "--draft" && i + 1 < argc) {
		try {
			const int n = strict_stoi(argv[i + 1]);
			if (n < 1 || n > 24) {
				std::cerr << "Error: --draft takes a number from 1 to 24 (one frame in N is rendered), not " << n << "\n";
				return FlagResult::Error;
			}
			out.draft_step = n;
			consumed_args.insert(i);
			consumed_args.insert(i + 1);
			++i;
		} catch (const std::exception&) {
			std::cerr << "Error: --draft takes a whole number (one frame in N is rendered)\n";
			return FlagResult::Error;
		}
	} else if (arg == "--ppm-only") {
		out.ppm_only = true;
		consumed_args.insert(i);
	} else if (arg == "--preview") {
		out.preview = true;
		consumed_args.insert(i);
	} else if (arg == "--background") {
		out.background = true;
		consumed_args.insert(i);
	} else if (arg == render_flags::kFps && i + 1 < argc) {
		try {
			const int fps = strict_stoi(argv[i + 1]);
			if (fps < 1 || fps > 1000) {   // (it was taken as it came, and ffmpeg refused it only once every frame had been rendered)
				std::cerr << "Error: " << render_flags::kFps << " must be from 1 to 1000, not " << fps << "\n";
				return FlagResult::Error;
			}
			out.video_fps = fps;
			consumed_args.insert(i);
			consumed_args.insert(i + 1);
			++i;
		} catch (const std::exception&) {
			std::cerr << "Invalid FPS, using default\n";
		}
	} else if (arg == render_flags::kSpeed && i + 1 < argc) {
		try {
			out.video_speed = strict_stod(argv[i + 1]);
			consumed_args.insert(i);
			consumed_args.insert(i + 1);
			++i;
		} catch (const std::exception&) {
			std::cerr << "Invalid speed, using default\n";
		}
	} else if ((arg == render_flags::kCameraPath || arg == "-p") && i + 1 < argc) {
		// camera_path.h's own path_type dispatch silently falls back to
		// "orbit" for anything it doesn't recognize (with no warning of
		// its own - and it runs once per rendered frame, so warning
		// there would spam rather than inform); validating once here,
		// the same way --sampler/--accelerator/--splitmethod already
		// do, catches a typo'd/stale preset id at parse time instead.
		static const std::set<std::string> kValidCameraPaths = {
			"orbit", "linear", "figure8", "spiral", "tour", "showcase"};
		if (cartoon::isCartoonPath(argv[i + 1])) {
			// A cartoon (src/shared/cartoon/cartoons.h): the video renders that film's own scene at every frame.
			if (cartoon::forCameraPath(argv[i + 1])) {
				out.camera_path = argv[i + 1];
			} else {
				std::cerr << "Unknown cartoon \"" << argv[i + 1] << "\" - valid cartoons:";
				for (const cartoon::Film *f : cartoon::films()) std::cerr << " " << cartoon::cameraPathOf(*f);
				std::cerr << cartoon::whyNoFilm(argv[i + 1]) << "\n";
				return FlagResult::Error;
			}
		} else {
			parseEnumFlag(argv[i + 1], kValidCameraPaths, out.camera_path, "--camera-path",
						  "using default (orbit). Valid: orbit, linear, figure8, spiral, tour, showcase, or a cartoon: cartoon:lamp-and-ball");
		}
		out.camera_path_explicit = true;
		consumed_args.insert(i);
		consumed_args.insert(i + 1);
		++i;
	} else if (arg == render_flags::kCameraKeyframes && i + 1 < argc) {
		out.camera_keyframes_file = argv[i + 1];
		out.camera_path_explicit = true;
		consumed_args.insert(i);
		consumed_args.insert(i + 1);
		++i;
	} else if (arg == "--video-preset" && i + 1 < argc) {
		// Sets video_mode plus all five video_preset::VideoPreset fields
		// in one shot - see src/shared/video_preset.h's own comment for
		// why this lives there rather than being duplicated per-CLI/GUI.
		// Applied immediately, same as every other flag here, so a flag
		// placed AFTER --video-preset on the command line overrides just
		// that one field - e.g. `--video-preset cornell-orbit --fps 60`
		// keeps the preset's scene/path/frames/speed but renders at 60fps.
		if (const video_preset::VideoPreset* preset = video_preset::find(argv[i + 1])) {
			out.video_mode = true;
			out.scene_id = preset->scene_id;
			out.camera_path = preset->camera_path;
			out.camera_path_explicit = true;
			out.video_frames = preset->frames;
			out.video_fps = preset->fps;
			out.video_speed = preset->speed;
		} else {
			std::cerr << "Invalid --video-preset \"" << argv[i + 1] << "\" - valid presets:\n";
			for (const video_preset::VideoPreset& p : video_preset::all())
				std::cerr << "  " << p.id << " / " << p.key << " - " << p.name << "\n";
			return FlagResult::Error;
		}
		consumed_args.insert(i);
		consumed_args.insert(i + 1);
		++i;
	} else {
		return FlagResult::NotMine;
	}
	return FlagResult::Handled;
}

// The --help text.
inline void print_launch_help(const char* program) {
		std::cout << "Usage: " << program
				  << " [--cpu|--gpu] [--output PATH] [width] [spp] [max_depth] [scene_id] [cam_x] [cam_y] [cam_z]\n"
				  << "  " << render_flags::kCpu << "      : Force CPU rendering\n"
				  << "  " << render_flags::kGpu << "      : Force GPU rendering (default)\n"
				  << "  " << render_flags::kDiagnose << " : Print a system-compatibility report (OS/CPU/RAM, GPU/CUDA/\n"
				  << "               OptiX, disk space, scene asset availability) instead of\n"
				  << "               rendering, and exit. Combine with --output PATH to also write\n"
				  << "               the report to a file.\n"
				  << "  " << render_flags::kWavefront << ": Use the wavefront (queue-based) GPU path tracer instead of\n"
				  << "               the default recursive one. GPU-only, ignored under --cpu/--sppm.\n"
				  << "  " << render_flags::kOptixValidate << ": Enable OptiX validation mode (extra device-side checks,\n"
				  << "               real per-launch cost - for debugging, not routine use).\n"
				  << "               GPU-only, ignored under --cpu/--sppm.\n"
				  << "  --isolate-pbrt-lighting: Skip the hardcoded demo room's own lights when\n"
				  << "               rendering a loaded pbrt scene - for isolating that scene's\n"
				  << "               OWN lighting. Metal now always does this for a pbrt scene, so\n"
				  << "               this flag is accepted but no longer has any effect.\n"
				  << "  " << render_flags::kDenoise << "  : Run the OptiX AI denoiser on the finished render, guided by\n"
				  << "               albedo + normal AOV buffers. GPU-only, both backends\n"
				  << "               (recursive and wavefront each have their own denoiser);\n"
				  << "               ignored under --cpu/--sppm. On a Mac the Metal renderer uses\n"
				  << "               Intel Open Image Denoise instead (colour only), if its library\n"
				  << "               is installed (docs/DENOISING.md; the GUI offers to download it);\n"
				  << "               without it the flag warns and the render goes on undenoised.\n"
				  << "  " << render_flags::kDenoiseGuides << " : With --denoise on the CPU, always give the denoiser each pixel's surface\n"
				  << "               colour and normal. By default they are used only when every visible\n"
				  << "               surface is matte (with glossy ones they smooth reflections).\n"
				  << "  " << render_flags::kDenoiseBlend << " VALUE: Blend between the noisy input and the fully\n"
				  << "               denoised output (default 0.0 = 100% denoised, 1.0 = original\n"
				  << "               noisy image unchanged). Full-strength denoising can over-smooth\n"
				  << "               fine texture/grain; lower this to preserve more of it. Blends in\n"
				  << "               the linear, pre-tonemap HDR color, not the final tonemapped\n"
				  << "               image. Only consulted when " << render_flags::kDenoise << " is passed. Same scope as " << render_flags::kDenoise << " above.\n"
				  << "  " << render_flags::kStats << "    : Print a small end-of-render stats block (rays cast, bounces,\n"
				  << "               shadow rays, samples/sec) after the normal RENDER TIME output.\n"
				  << "               Observation-only - never changes the rendered image.\n"
				  << "  " << render_flags::kExposure << " VALUE: Flat multiplier on linear color before tone-mapping\n"
				  << "               (default 1.0 = no-op). CPU and GPU default path tracer only.\n"
				  << "               E.g. 0.5 = darker, 2.0 = brighter. No effect under\n"
				  << "               --bdpt/--mlt/--sppm (warns).\n"
				  << "  " << render_flags::kBloom << " S, " << render_flags::kVignette << " V, " << render_flags::kWarmth << " W, " << render_flags::kSaturation << " S:\n"
				  << "               The camera's look, on the linear picture before the tone map (not an EXR): bloom 0-1 (the\n"
				  << "               glow bright lights spread), vignette 0-1 (darker corners), warmth -1 to 1 (blue to amber),\n"
				  << "               saturation 0-3 (1 = as rendered). A scene file's `# @rt-look bloom 0.2 ...` line sets them\n"
				  << "               too; a flag wins. CPU and GPU default path tracer.\n"
				  << "  " << render_flags::kSampler << " NAME: Which ported pbrt-v4 sampler drives random decisions\n"
				  << "               (default sobol, this project's pre-existing behavior).\n"
				  << "               One of sobol, zsobol, paddedsobol, stratified, pmj02bn, halton,\n"
				  << "               independent.\n"
				  << "               CPU default path tracer only.\n"
				  << "  " << render_flags::kAovs << "     : Also write the render passes - albedo.R/G/B, normal.X/Y/Z (world, facing the\n"
				  << "               camera), depth.Z (distance to the first hit), uv.U/V and A (coverage) -\n"
				  << "               from first-hit camera rays, for compositing. Into the same file for\n"
				  << "               an .exr output (a multilayer EXR with the image's R/G/B), else into\n"
				  << "               <name>.aovs.exr next to the image. Any backend; single images only.\n"
				  << "  " << render_flags::kToon << "     : A cartoon look - the light in a few flat bands, ink lines round shapes\n"
				  << "               and along creases, stronger colour - made after the render from the\n"
				  << "               picture and its passes. A scene file's `# @rt-toon bands 3 lines 2 ...`\n"
				  << "               line gives it by itself, with its own style (a film's `toon` line\n"
				  << "               writes one into every frame). Any backend; not for an EXR.\n"
				  << "  " << render_flags::kAdaptive << " : Stop sampling a pixel once it's converged instead of\n"
				  << "               always spending the full samples-per-pixel budget on every\n"
				  << "               pixel - samples_per_pixel becomes a ceiling, not a fixed count.\n"
				  << "               Off by default (on for a film video's frames on a GPU). The CPU\n"
				  << "               path tracer and both GPU renderers (on the GPU a pixel is\n"
				  << "               checked from an eighth of the samples on).\n"
				  << "  " << render_flags::kNoAdaptive << " : A film video samples every pixel the full count.\n"
				  << "  " << render_flags::kAdaptiveThreshold << " VALUE: The noise a pixel may keep, as it would show\n"
				  << "               in the picture (gamma 2.2, 0..1), for " << render_flags::kAdaptive << " to stop it\n"
				  << "               (default 0.003, under one level of 255). Lower = cleaner but\n"
				  << "               slower; only consulted when " << render_flags::kAdaptive << " is on.\n"
				  << "  " << render_flags::kTimeLimit << " SECONDS: Stop rendering once this many seconds have\n"
				  << "               elapsed, instead of always running until every scanline is\n"
				  << "               done - useful for previews or a fixed render-farm time budget.\n"
				  << "               Scanline-granular: whatever rows were already in flight at the\n"
				  << "               deadline finish normally; any never-started row is written\n"
				  << "               black rather than left out. Under --video, this is a budget for\n"
				  << "               the WHOLE video, not each frame - later frames get whatever's\n"
				  << "               left of it, and rendering stops (skipping any remaining frames)\n"
				  << "               once it runs out. Off by default (no limit). CPU default path\n"
				  << "               tracer only.\n"
				  << "  --lightsampler NAME: pbrt-v4 Integrator \"string lightsampler\" - which light\n"
				  << "               sampler picks the next-event-estimation light to sample\n"
				  << "               (default bvh, pbrt-v4's own real default). One of uniform,\n"
				  << "               power, bvh, auto. Affects convergence/variance, not the\n"
				  << "               converged image. \"auto\" uses whatever the loaded scene's own\n"
				  << "               Integrator \"string lightsampler\" parameter requested (bvh if\n"
				  << "               it made no request) instead of a fixed default. CPU default\n"
				  << "               path tracer only.\n"
				  << "  " << render_flags::kAccelerator << " NAME: Overrides a loaded .pbrt scene's own Accelerator\n"
				  << "               directive (default: leave the scene's own choice, or bvh if it\n"
				  << "               named none). One of bvh, kdtree. All choices render the same\n"
				  << "               converged image - a build-strategy/perf knob, not a quality one.\n"
				  << "               No effect on a native (non-.pbrt) scene, which has no Accelerator\n"
				  << "               directive to override (warns). CPU only.\n"
				  << "  " << render_flags::kSplitMethod << " NAME: Overrides a loaded .pbrt scene's own Accelerator\n"
				  << "               \"string splitmethod\" (default: leave the scene's own choice, or\n"
				  << "               sah if it named none). One of sah, middle, equal, hlbvh. Only\n"
				  << "               consulted when the resolved accelerator is bvh. Same scope/effect\n"
				  << "               shape as --accelerator above.\n"
				  << "  " << render_flags::kRegularize << " : pbrt-v4 Integrator \"bool regularize\" - widens a rough BSDF's\n"
				  << "               GGX alpha after the path's first non-specular bounce, taming\n"
				  << "               fireflies from hard caustic paths at the cost of some blur.\n"
				  << "               A loaded .pbrt scene requesting this itself already gets it\n"
				  << "               regardless of this flag - this only ever forces it ON, never\n"
				  << "               off. Default path tracer only, both backends.\n"
				  << "  " << render_flags::kMaxComponentValue << " VALUE: pbrt-v4 Film \"maxcomponentvalue\" - clamps any\n"
				  << "               pixel sample whose brightest channel exceeds VALUE, scaling\n"
				  << "               all channels down to preserve hue (default effectively\n"
				  << "               unbounded). Only overrides a loaded scene's own request when\n"
				  << "               explicitly passed. CPU and both GPU backends -\n"
				  << "               recursive matches CPU exactly, wavefront approximates it\n"
				  << "               per-contribution rather than per-sample-total.\n"
				  << "  " << render_flags::kCrop << " X0 Y0 X1 Y1: pbrt-v4 Film \"cropwindow\" - renders only the\n"
				  << "               rectangle from (X0,Y0) to (X1,Y1), each a fraction of the full\n"
				  << "               frame in [0,1] (e.g. \"0 0 0.5 0.5\" is the top-left quadrant).\n"
				  << "               Pixels outside the rectangle write black rather than shrinking\n"
				  << "               the output file. Only overrides a loaded scene's own\n"
				  << "               cropwindow/pixelbounds when explicitly passed. Default path\n"
				  << "               tracer only, both backends.\n"
				  << "  " << render_flags::kProbe << " X Y: instead of an image, print where ONE pixel's light comes from: its mean\n"
				  << "               radiance split by source (emitters hit, the sky, next-event estimates\n"
				  << "               toward area lights / the sky and portal lights / point lights), by\n"
				  << "               bounce, by area light, and what the camera rays hit first. X is the\n"
				  << "               column and Y the row, counted from the top left of the image the\n"
				  << "               other arguments describe; the samples-per-pixel argument is how many\n"
				  << "               paths are averaged (use a few thousand). CPU default path tracer\n"
				  << "               only. The report is also written to <--output>.probe.txt.\n"
				  << "  --worker-max-mb N: a cartoon video's worker that holds more than N MB is replaced between two\n"
				  << "               frames (default 0 = never; RAY_TRACER_WORKER_MAX_MB also sets it)\n"
				  << "  --gpu-device N: which GPU an NVIDIA renderer uses, as `nvidia-smi` numbers them (default 0, the\n"
				  << "               first; the environment variable RAY_TRACER_GPU_DEVICE also sets it)\n"
				  << "  " << render_flags::kSeed << " N: makes the render reproducible - the same scene, same\n"
				  << "               settings, and same seed always produce the same pixels. Without\n"
				  << "               this, CPU renders draw fresh randomness every run (never\n"
				  << "               reproducible); GPU renders already default to a fixed internal\n"
				  << "               seed, so this just lets you choose a different one. N must be in\n"
				  << "               [0, 2147483647]. Default path tracer only, both backends; has no\n"
				  << "               effect under --bdpt/--mlt/--sppm/--randomwalk/--ao/--simplepath/\n"
				  << "               --simplevolpath/--lightpath. --video reseeds each frame from N so\n"
				  << "               the whole video reproduces while frames still get distinct noise.\n"
				  << "  " << render_flags::kSpectral << " : Real hero-wavelength spectral rendering instead of flat RGB.\n"
				  << "               CPU default path tracer only. Only lambertian, metal,\n"
				  << "               dielectric, rough_dielectric, conductor, and diffuse_light\n"
				  << "               materials are supported - scenes using anything else fail\n"
				  << "               loudly at load time rather than silently rendering wrong\n"
				  << "               colors. Noticeably slower per-sample than the default RGB\n"
				  << "               path (spectral upsampling table lookups every bounce).\n"
				  << "  " << render_flags::kTonemap << " MODE: Which tone-mapping operator to apply before the sRGB\n"
				  << "               OETF (default aces, this project's pre-existing behavior).\n"
				  << "               One of aces, reinhard, none. Applies to both CPU and GPU\n"
				  << "               (recursive and wavefront). No effect under\n"
				  << "               --bdpt/--mlt/--sppm (warns).\n"
				  << "  --sppm     : Render with Stochastic Progressive Photon Mapping instead of\n"
				  << "               the path tracer (incompatible with --video). Best for hard\n"
				  << "               caustic/glass scenes. CPU: verified end-to-end on scene 11\n"
				  << "               (Cornell Rough Glass); other scenes are unverified and only\n"
				  << "               support lambertian + delta-BSDF materials. GPU (--sppm --gpu):\n"
				  << "               capability-checked per scene -- Lambertian/DiffuseLight,\n"
				  << "               RoughDielectric, Metal, Dielectric, Conductor, RoughMetal,\n"
				  << "               DiffuseTransmission, and Mix are supported (area lights only);\n"
				  << "               any scene using another material falls back to an error, use\n"
				  << "               CPU SPPM (--sppm without --gpu) instead.\n"
				  << "  --sppm-iterations N: SPPM iteration count (default " << kDefaultSppmIterations << ")\n"
				  << "  --sppm-photons N   : Photons shot per SPPM iteration (default " << kDefaultSppmPhotons << ")\n"
				  << "  --bdpt     : Render with Bidirectional Path Tracing instead of the path\n"
				  << "               tracer (incompatible with --video, --sppm, --mlt). CPU only -\n"
				  << "               no GPU/OptiX implementation exists; --gpu is ignored (with a\n"
				  << "               warning) if combined with --bdpt. Samples area, point, spot,\n"
				  << "               distant, and sky/infinite lights from one unified distribution\n"
				  << "               (goniometric/projection lights not yet included). Verified\n"
				  << "               end-to-end on scene A1 (Cornell Box) plus punctual/sky-only\n"
				  << "               scenes (C1-C4); other scenes are unverified.\n"
				  << "  --bdpt-max-depth N : Maximum BDPT path depth (default " << kDefaultBdptMaxDepth << ")\n"
				  << "  --mlt      : Render with Metropolis Light Transport instead of the path\n"
				  << "               tracer (incompatible with --video, --sppm, --bdpt). CPU only -\n"
				  << "               no GPU/OptiX implementation exists; --gpu is ignored (with a\n"
				  << "               warning) if combined with --mlt. Same light coverage and\n"
				  << "               verification scope as --bdpt (MLT is built directly on BDPT's\n"
				  << "               subpath machinery).\n"
				  << "  --mlt-bootstrap N  : MLT bootstrap samples per depth (default " << kDefaultMltBootstrap << ")\n"
				  << "  --mlt-mutations N  : Total Metropolis mutations, all chains combined\n"
				  << "                       (default " << kDefaultMltMutations << ")\n"
				  << "  --mlt-max-depth N  : Maximum BDPT path depth per MLT sample (default " << kDefaultMltMaxDepth << ")\n"
				  << "  --randomwalk : Render with RandomWalkIntegrator (pbrt-v4's unbiased\n"
				  << "               reference path tracer - uniform-sphere sampling, no NEE/MIS).\n"
				  << "               CPU only, incompatible with --video/--sppm/--bdpt/--mlt.\n"
				  << "  --ao       : Render with AOIntegrator (ambient occlusion only - no\n"
				  << "               indirect lighting or material color, a visualization/debug\n"
				  << "               mode, not a lit render). CPU only, same incompatibilities as\n"
				  << "               --randomwalk.\n"
				  << "  --ao-max-dist N    : Occlusion test distance (default " << kDefaultAoMaxDist << " = unbounded)\n"
				  << "  --ao-uniform       : Uniform-hemisphere sampling instead of the default\n"
				  << "                       cosine-hemisphere sampling\n"
				  << "  --ao-illum-scale N : Flat multiplier on the occlusion color (default " << kDefaultAoIllumScale << ")\n"
				  << "  --ao-illum-rgb R G B : Occlusion color (default 1 1 1 = white)\n"
				  << "  --simplepath : Render with SimplePathIntegrator (pbrt-v4's canonical\n"
				  << "               reference path tracer, optional NEE + optional BSDF\n"
				  << "               importance sampling - both on by default). CPU only, same\n"
				  << "               incompatibilities as --randomwalk. NEE (when enabled) samples\n"
				  << "               the same unified area/point/spot/distant/sky distribution as\n"
				  << "               --bdpt/--mlt.\n"
				  << "  --simplepath-no-lights : Disable NEE (direct light sampling)\n"
				  << "  --simplepath-no-bsdf   : Disable BSDF importance sampling (falls back to\n"
				  << "                           uniform hemisphere)\n"
				  << "  --simplevolpath : Render with SimpleVolPathIntegrator (pbrt-v4's simplest\n"
				  << "               volumetric path tracer - pure delta tracking, no NEE/MIS/\n"
				  << "               surface BSDFs). CPU only, same incompatibilities as\n"
				  << "               --randomwalk. Reachable but medium-FREE in this integration\n"
				  << "               (see cpu_interface.h's cpu_render_main_simplevolpath() doc\n"
				  << "               comment) - renders mostly black on ordinary solid-geometry\n"
				  << "               scenes except where camera rays land directly on a light,\n"
				  << "               matching pbrt-v4's own upstream behavior on medium-free scenes.\n"
				  << "  --lightpath : Render with LightPathIntegrator (a pure light tracer - every\n"
				  << "               sample starts at a light and splats camera-connection\n"
				  << "               contributions into the film, the opposite direction of every\n"
				  << "               other integrator here). CPU only, same incompatibilities as\n"
				  << "               --randomwalk. Area lights only - its own light-emission sample\n"
				  << "               was not extended alongside --bdpt/--mlt/--simplepath's NEE.\n"
				  << "  " << render_flags::kOutput << ",-o: Output file path (default: ./output/image.ppm). A \".exr\"\n"
				  << "               extension switches to linear HDR EXR output instead of\n"
				  << "               tonemapped PPM/PNG (both backends); combine with --denoise\n"
				  << "               to also write \"<name>_albedo.exr\"/\"<name>_normal.exr\" AOVs\n"
				  << "               (GPU recursive backend only). AOV export reuses the AI\n"
				  << "               denoiser's own guide-layer buffers, so there is currently no\n"
				  << "               way to get AOVs alongside a clean, non-AI-denoised beauty EXR -\n"
				  << "               requesting AOVs always denoises the beauty pass too.\n"
				  << "  " << render_flags::kVideo << "    : Enable video generation mode\n"
				  << "  " << render_flags::kFrames << ",-f: Number of frames for video (default: 120)\n"
				  << "  " << render_flags::kFps << "      : Frames per second for video (default: 30)\n"
				  << "  " << render_flags::kSpeed << "    : Camera movement speed multiplier for video (default: 1.0)\n"
				  << "  " << render_flags::kCameraPath << ",-p: Camera animation path "
			  << "(orbit|linear|figure8|spiral|tour|showcase) - "
			  << "defaults to the selected scene's own curated recommendation if omitted\n"
				  << "  " << render_flags::kCameraKeyframes << " FILE: With --video, fly the camera through the places listed in FILE (lines\n"
				  << "               'key px py pz tx ty tz': where the camera is and what it looks at; 'ease 0|1'; '#' comments)\n"
				  << "               instead of a named path. The first frame is the first key and the last frame the last.\n"
				  << "  --video-preset ID: Sets --video plus scene_id/camera-path/frames/fps/speed\n"
				  << "               together from one of src/shared/video_preset.h's named bundles.\n"
				  << "               --list-video-presets (as the only argument) prints them all: the\n"
				  << "               camera-path flythroughs and the cartoons; --film-table prints the\n"
				  << "               cartoon films (length, shots, tags) as the markdown table of the docs.\n"
				  << "  --cartoon-jobs N: how many frames of a cartoon video are rendered at once (default on a GPU: 2 to 4,\n"
				  << "               more for small or few-sample frames, as many as the card's free memory holds; 1 on the\n"
				  << "               CPU; the environment variable RAY_TRACER_CARTOON_JOBS also sets it)\n"
				  << "  --cartoon-timeout S: a frame of a cartoon video that takes longer than S seconds is stopped and\n"
				  << "               tried once more (default: 900 on a GPU, never on the CPU; 0 = never; the environment\n"
				  << "               variable RAY_TRACER_CARTOON_TIMEOUT also sets it)\n"
				  << "  --cartoon-workers N: render a cartoon video's frames with N long-lived worker processes (the default\n"
				  << "               on a GPU: as many as --cartoon-jobs) instead of starting a process for every frame, which\n"
				  << "               at a draft quality is most\n"
				  << "               of a frame's time; 0 = a process per frame\n"
				  << "  --background: use the computer lightly, for when you want to use it meanwhile: lower CPU and GPU\n"
				  << "               priority, half the CPU cores, one worker and one frame at a time for a cartoon video\n"
				  << "               (the environment variable RAY_TRACER_BACKGROUND=1 also sets it)\n"
				  << "  --resume:    continue an interrupted cartoon video from the frames already rendered, when the film and\n"
				  << "               the settings (size, samples, flags) are the same; otherwise it starts again\n"
				  << "  --preview:   a quick look at a cartoon: its characters built coarser (most of a frame's time), its frames\n"
				  << "               in frames-<key>-preview (python scripts/anim/film.py preview <key> sets the rest)\n"
				  << "  --draft N:   a cartoon video renders one frame in N (2 to 24) and repeats it in the others: the whole\n"
				  << "               film, its sound in step, in a fraction of the time; a full render with the same\n"
				  << "               settings and --resume then renders only the frames the draft left out\n"
				  << "  --temporal-denoise [R]: a cartoon video's frames denoised with the R (1 to 3, default 2) either side\n"
				  << "               in their shot where nothing moves: less flicker, or as smooth with fewer samples\n"
				  << "               Accepts either short id or descriptive key:\n"
				  << "               V1/cornell-orbit, V2/teapot-spin, V3/one-weekend-flyby,\n"
				  << "               V4/next-week-finale, V5/glass-dragon-caustics,\n"
				  << "               V6/sponza-flythrough. Any of those five flags placed AFTER\n"
				  << "               --video-preset on the command line overrides just that one field.\n"
				  << "  --help,-h  : Show this help message\n"
				  << "  width      : Image width (default " << kDefaultWidth << ", square unless --height is given)\n"
				  << "  --height N : Image height, for a non-square image (default: the width)\n"
				  << "  spp        : Samples per pixel (default " << kDefaultSamplesPerPixel << ")\n"
				  << "  max_depth  : Max ray depth (default " << kDefaultMaxDepth << ")\n"
				  << "  scene_id   : Scene selector: a scene name (e.g. \"cornell-box\") or an id, category letter +\n"
				  << "               number (e.g. \"A1\"=Cornell Box; names are stable, ids can shift),\n"
				  << "               default " << kDefaultSceneId << " - see src/TheRestOfYourLife/scene_registry.h),\n"
				  << "               or the path of a .pbrt file (e.g. one saved by the GUI's Scene Builder),\n"
				  << "               or one frame of a cartoon: cartoon:<key> or cartoon:<key>@<seconds>\n"
				  << "  cam_x/y/z  : Camera position - if omitted, uses the selected scene's own\n"
				  << "               recommended camera (see src/TheRestOfYourLife/scene_registry.h),\n"
				  << "               not a single fixed default across every scene\n";
}

// Parse command-line arguments into a LaunchArgs struct.
// Returns false if the caller should exit immediately - either because
// --help was printed (a benign, deliberate exit: *help_requested, when
// non-null, is set true) or because a real parse error was reported to
// stderr (--video-preset/scene_id/etc. - *help_requested stays false).
// `help_requested` defaults to nullptr so existing 3-argument call sites
// (including tests/unit/launcher_args_bdpt_mlt_tests.cpp) keep compiling
// unchanged; a caller that cares about the distinction (main.cpp, so it can
// exit non-zero on a real error instead of silently exiting 0 as if the
// render had happened - see main()'s own comment) passes a real pointer.
inline bool parse_launch_args(int argc, char** argv, LaunchArgs& out,
							   bool* help_requested = nullptr) {
	std::set<int> consumed_args;
	int explicit_height = 0;  // --height N: a non-square image (default: square, as high as the width is wide)

	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];

		FlagResult r = FlagResult::NotMine;
		r = handle_basic_flag(arg, argc, argv, i, consumed_args, out);
		if (r == FlagResult::NotMine) r = handle_quality_flag(arg, argc, argv, i, consumed_args, out);
		if (r == FlagResult::NotMine) r = handle_integrator_flag(arg, argc, argv, i, consumed_args, out);
		if (r == FlagResult::NotMine) r = handle_image_and_video_flag(arg, argc, argv, i, consumed_args, out, explicit_height);
		if (r == FlagResult::Error) return false;
		if (r == FlagResult::Handled) {
			// A flag whose value did not parse has said so and kept its default, without consuming anything; its value must not then become a positional argument (it
			// shifted the width, samples, depth and scene along)
			if (consumed_args.count(i) == 0 && i + 1 < argc) {
				consumed_args.insert(i);
				consumed_args.insert(i + 1);
				++i;
			}
			continue;
		}

		if (arg == "--help" || arg == "-h") {
			print_launch_help(argv[0]);
			if (help_requested) *help_requested = true;
			return false;
		} else if (looks_like_flag(arg)) {
			// Not a flag any handler above knows: a typo (--denoize), a value-taking flag whose value is missing (every value-taking flag above is gated on
			// `i + 1 < argc`), or one of the commands that must come first (--export-gltf ...). It used to be dropped without a word unless it was the last token, and the
			// render went ahead without what was asked for.
			static const std::set<std::string> kCommands = {"--prepare-gpu", "--assemble-video", "--export-gltf", "--scene-report", "--print-scene-size", "--list-video-presets",
															 "--film-table", "--film-check", "--film-shots", "--film-subjects", "--film-events", "--film-feet", "--film-media", "--film-cuts", "--film-cues", "--film-baseline"};
			if (kCommands.count(arg)) std::cerr << "Error: " << arg << " is a command of its own and must be the first argument (see --help)\n";
			else if (i + 1 >= argc) std::cerr << "Error: \"" << arg << "\" is not an option, or is missing the value it expects (see --help)\n";
			else std::cerr << "Error: \"" << arg << "\" is not an option (see --help)\n";
			return false;
		}
	}

	for (int i = 1; i < argc; ++i)
		if (consumed_args.count(i) > 0) out.consumed_tokens.push_back(argv[i]);

	// Parse positional arguments: width spp depth scene_id cam_x cam_y cam_z
	// Kept as raw strings rather than blindly std::stod-ing every token like
	// the old flat-int scheme did - scene_id (position 3) is a category
	// letter + number now (e.g. "A2"), not a number, so each position is
	// parsed according to what it's actually expected to hold - see
	// scene_registry.h's SceneDescriptor::id comment for the id format.
	std::vector<std::string> positional_args;
	for (int i = 1; i < argc; ++i) {
		if (consumed_args.count(i) > 0) continue;
		const std::string arg = argv[i];
		if (arg.size() >= 2 && arg[0] == '-' && arg[1] == '-') continue;
		positional_args.push_back(arg);
	}

	// WIDTH SAMPLES DEPTH: whole numbers from 1 to `most`. Anything else is an error: a value that did not parse used to be passed over in silence, so `ray_tracer my.pbrt` or
	// `ray_tracer 400 64 my.pbrt` rendered the Cornell box at the default settings.
	static const char *kPositionNames[] = {"width", "samples per pixel", "depth"};
	auto parse_positive_int = [&](std::size_t position, int most, int &out_value) {
		const std::string &s = positional_args[position];
		double v = 0.0;
		try {
			v = strict_stod(s);
		} catch (const std::exception &) {
			const bool sceneLike = s.find('.') != std::string::npos || scene_slugs::looksLikeSlug(s) || cartoon::isCartoonPath(s.c_str());
			std::cerr << "Error: the " << kPositionNames[position] << " (argument " << position + 1 << " after the options) must be a number, not \"" << s << "\""
					  << (sceneLike ? "; the scene comes fourth: WIDTH SAMPLES DEPTH SCENE" : "") << " (see --help)\n";
			return false;
		}
		if (!(v >= 1.0 && v <= most) || v != std::floor(v)) {
			std::cerr << "Error: the " << kPositionNames[position] << " must be a whole number from 1 to " << most << ", not \"" << s << "\"\n";
			return false;
		}
		out_value = static_cast<int>(v);
		return true;
	};

	if (positional_args.size() >= 1 && !parse_positive_int(0, 65536, out.image_width)) return false;
	out.image_height = explicit_height > 0 ? explicit_height : out.image_width;
	if (positional_args.size() >= 2 && !parse_positive_int(1, 100000000, out.samples_per_pixel)) return false;
	if (positional_args.size() >= 3 && !parse_positive_int(2, 100000, out.max_ray_depth)) return false;
	if (positional_args.size() >= 1 && positional_args.size() < 4 && out.cartoon_worker.empty() && !cartoon::isCartoonPath(out.camera_path.c_str()))
		std::cerr << "Note: no scene was given (it comes fourth: WIDTH SAMPLES DEPTH SCENE), so the default one is rendered.\n";

	if (positional_args.size() >= 4) {
		const std::string& id = positional_args[3];
		cartoon::Still still;
		std::string still_error;
		const bool is_still = cartoon::parseStill(id, still, still_error);
		if (!still_error.empty()) {
			std::cerr << still_error << "\n";
			return false;
		}
		if (is_still) out.cartoon_still = id;
		// Category letter + one or more digits (e.g. "A1", "B10").
		const bool valid = id.size() >= 2 && id[0] >= 'A' && id[0] <= 'Z' &&
			std::all_of(id.begin() + 1, id.end(),
				[](unsigned char c) { return std::isdigit(c) != 0; });
		// Or the path of a .pbrt file.
		std::string ext = id.size() >= 5 ? id.substr(id.size() - 5) : std::string();
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (is_still) {
			// a cartoon frame, recorded above
		} else if (!valid && ext == ".pbrt") {
			out.scene_file = id;
		} else if (valid || scene_slugs::looksLikeSlug(id)) {
			// An id ("A1") or a slug ("cornell-box"); main() resolves either to the scene.
			out.scene_id = id;
		} else {
			std::cerr << "Invalid scene \"" << id
				<< "\" - expected a scene name such as \"cornell-box\", an id such as \"A1\", or the path of a .pbrt file "
				   "(see src/shared/scene_slugs.h and src/TheRestOfYourLife/scene_registry.h)\n";
			return false;
		}
	}

	if (positional_args.size() >= 5) {
		try { out.cam_x = strict_stod(positional_args[4]); out.cam_explicit = true; }
		catch (const std::exception&) { std::cerr << "Invalid cam_x value, using default\n"; }
	}
	if (positional_args.size() >= 6) {
		try { out.cam_y = strict_stod(positional_args[5]); }
		catch (const std::exception&) { std::cerr << "Invalid cam_y value, using default\n"; }
	}
	if (positional_args.size() >= 7) {
		try { out.cam_z = strict_stod(positional_args[6]); }
		catch (const std::exception&) { std::cerr << "Invalid cam_z value, using default\n"; }
	}

	return true;
}
