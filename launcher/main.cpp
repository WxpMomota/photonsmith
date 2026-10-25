// ============================================================================
// Ray Tracer Unified Launcher
// ============================================================================
// This is the main entry point for the ray tracer executable (ray_tracer.exe)
// 
// Features:
//   - Detects CUDA GPU availability at runtime
//   - Routes to GPU or CPU renderer based on command-line flags or availability
//   - Accepts camera position parameters for configurable viewpoints
//   - Command-line driven; the Qt GUI (RayTracerGUI) launches it as a subprocess
//   - Converts output to multiple formats (PPM, PNG)
//
// Camera System:
//   - Camera position (lookfrom) is configurable via command-line args
//   - Camera target (lookat)/up/field of view come from the selected
//     scene's own CameraConfig (scene_registry.h) unless overridden
//   - Default camera position for the Cornell Box scene: (278, 278, -800)
//
// Command-line format:
//   ray_tracer.exe [--gpu|--cpu] [--output path] width spp depth scene_id [cam_x cam_y cam_z]
//
// Example:
//   ray_tracer.exe --gpu --output out.png 800 100 50 A1 278 500 200
//   (GPU mode, 800x800 pixels, 100 samples/pixel, 50 ray depth, Cornell Box, camera at (278,500,200))
// ============================================================================

#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>
#include <cmath>
#include <cctype>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif
#include "cpu_renderer/cpu_interface.h"
#ifdef RT_HAVE_OPTIX
#include "gpu/optix/optix_interface.h"
#else
#include "launcher/optix_stub.h"
#endif
// RT_HAVE_OPTIX and RT_HAVE_METAL are never both defined for the same
// build (CUDA/OptiX needs Windows, Metal needs macOS - see root
// CMakeLists.txt's own RT_BUILD_GPU/RT_BUILD_METAL blocks), so this
// doesn't need a stub header the way optix_interface.h does above - a
// build with neither defined simply never calls metal_render_main() at
// all (see the use_gpu dispatch below), so nothing needs to link a no-op
// stand-in for it.
#ifdef RT_HAVE_METAL
#include "gpu/metal/metal_interface.h"
#endif
#include "src/external/image_writer.h"
#include "src/shared/output_format.h"
#include "src/TheRestOfYourLife/error_codes.h"
#include "src/TheRestOfYourLife/thread_count.h"
#include "src/shared/accelerator_override.h"
#include "src/shared/exr_writer.h"
#include "src/shared/output_crop.h"
#include "src/shared/render_stats.h"
#include "launcher/camera_path.h"
#include "src/shared/camera_keyframes.h"
#include "src/shared/backend_capabilities.h"   // which options each renderer reads
#include "launcher/gpu_backend.h"      // the GPU renderer this build has (Metal or OptiX), behind one call
#include "launcher/launcher_args.h"   // Argument parsing
#include "launcher/cartoon_render.h"
#include "src/shared/cartoon/film_subjects.h"
#include "src/shared/cartoon/film_checks.h"
#include "src/shared/cartoon/film_baseline.h"   // --film-baseline: every film, shot by shot, against a recorded baseline   // --film-feet: planted feet that slide   // --film-subjects: where a film's subjects fall in its frame
#include "launcher/cartoon_still.h"  // one frame of a film as a temporary scene file  // cartoon video presets: one child run per frame
#include <fstream>
#include "src/shared/path_probe.h"    // --probe X Y
#include "launcher/diagnostics.h"     // --diagnose
#include "launcher/process.h"
#include "launcher/render_setup.h"
#include "launcher/background_mode.h"   // --background
#include "launcher/cartoon_worker.h"
#include "launcher/video_render.h"   // --video, --assemble-video
#ifdef RT_HAVE_AVFOUNDATION_ENCODER
#include "launcher/video_encoder_mac.h"   // Video -> MP4 via AVFoundation, no ffmpeg needed
#endif

// Rejects combinations of render modes that cannot work together (returns an error code) and warns about flags a mode ignores (returns SUCCESS).
static int check_render_mode_options(const LaunchArgs &args, const RenderSetup &s) {
	const bool use_gpu = s.use_gpu;
	const std::string &scene_id = s.scene_id;
	const bool video_mode = s.video_mode;
	const bool use_sppm = s.use_sppm;
	const bool use_bdpt = s.use_bdpt;
	const bool use_mlt = s.use_mlt;
	const bool use_randomwalk = s.use_randomwalk;
	const bool use_ao = s.use_ao;
	const bool use_simplepath = s.use_simplepath;
	const bool use_simplevolpath = s.use_simplevolpath;
	const bool use_lightpath = s.use_lightpath;
	const bool use_debug_integrator = s.use_debug_integrator;

	// SPPM has no defined per-frame semantics (its progressive radius state
	// doesn't reset cleanly frame-to-frame the way the path tracer's
	// stateless per-pixel sampling does) - reject the combination outright
	// rather than silently mis-rendering a video.
	if (use_sppm && video_mode) {
		std::cerr << ErrorInfo(ERR_INVALID_ARGUMENTS).to_string()
				  << " - --sppm and --video cannot be combined" << std::endl;
		return ERR_INVALID_ARGUMENTS;
	}

	// BDPT/MLT are each their own distinct render mode, same footing as
	// SPPM - reject any combination of two-or-more special modes outright
	// (which one would even "win" is ambiguous) rather than silently
	// picking one, mirroring --sppm/--video's own rejection above. Neither
	// has a defined per-frame video semantics either (a fresh independent
	// render per frame would technically work for either, unlike SPPM's
	// genuinely stateful radius, but wiring that per-frame path through
	// main.cpp's video loop is unimplemented scope here) - rejected for the
	// same "don't silently do something unexpected" reasoning.
	if ((use_bdpt && use_mlt) || (use_bdpt && use_sppm) || (use_mlt && use_sppm)) {
		std::cerr << ErrorInfo(ERR_INVALID_ARGUMENTS).to_string()
				  << " - --bdpt, --mlt, and --sppm are mutually exclusive render modes" << std::endl;
		return ERR_INVALID_ARGUMENTS;
	}
	if ((use_bdpt || use_mlt) && video_mode) {
		std::cerr << ErrorInfo(ERR_INVALID_ARGUMENTS).to_string()
				  << " - --bdpt/--mlt and --video cannot be combined" << std::endl;
		return ERR_INVALID_ARGUMENTS;
	}
	// --bdpt/--mlt have no GPU/OptiX implementation at all (CPU only - see
	// cpu_interface.h's cpu_render_main_bdpt()/cpu_render_main_mlt() doc
	// comments) - always render via the CPU path below regardless of
	// use_gpu, but only warn about it when the user actually typed --gpu
	// (use_gpu's own struct default is true, so warning unconditionally
	// would fire on the plain, common `--bdpt` invocation too).
	if ((use_bdpt || use_mlt) && args.gpu_flag_explicit) {
		std::cerr << "WARNING: --gpu is ignored under --bdpt/--mlt (CPU-only, no GPU/OptiX implementation exists) - rendering on CPU" << std::endl;
	}

	// Round 6 Phase 2 debug integrators - same "one special render mode at a
	// time, no --video, CPU-only" shape as --bdpt/--mlt above.
	if (use_debug_integrator && (use_bdpt || use_mlt || use_sppm)) {
		std::cerr << ErrorInfo(ERR_INVALID_ARGUMENTS).to_string()
				  << " - --randomwalk/--ao/--simplepath/--simplevolpath/--lightpath cannot be combined with --bdpt/--mlt/--sppm" << std::endl;
		return ERR_INVALID_ARGUMENTS;
	}
	if (use_randomwalk + use_ao + use_simplepath + use_simplevolpath + use_lightpath > 1) {
		std::cerr << ErrorInfo(ERR_INVALID_ARGUMENTS).to_string()
				  << " - --randomwalk, --ao, --simplepath, --simplevolpath, and --lightpath are mutually exclusive render modes" << std::endl;
		return ERR_INVALID_ARGUMENTS;
	}
	if (use_debug_integrator && video_mode) {
		std::cerr << ErrorInfo(ERR_INVALID_ARGUMENTS).to_string()
				  << " - --randomwalk/--ao/--simplepath/--simplevolpath/--lightpath and --video cannot be combined" << std::endl;
		return ERR_INVALID_ARGUMENTS;
	}
	if (use_debug_integrator && args.gpu_flag_explicit) {
		std::cerr << "WARNING: --gpu is ignored under --randomwalk/--ao/--simplepath/--simplevolpath/--lightpath (CPU-only, no GPU/OptiX implementation exists) - rendering on CPU" << std::endl;
	}

	// An animated-camera scene (camera.h's camera_is_animated, e.g. D13)
	// already defines its own camera motion across one shutter interval -
	// --video's per-frame camera-path flythrough has no sensible way to
	// combine with that (which motion wins? does each frame get its own
	// sub-interval of the shutter, or does the scene's motion just repeat
	// identically every frame?). Rather than silently picking an answer
	// nobody asked for, reject the combination outright at argument-parsing
	// time - same "don't silently do something unexpected" shape as
	// --sppm/--bdpt/--mlt/the debug integrators combined with --video above.
	if (video_mode && cpu_scene_camera_is_animated_by_id(scene_id.c_str())) {
		std::cerr << ErrorInfo(ERR_INVALID_ARGUMENTS).to_string()
				  << " - --video cannot be combined with an animated-camera scene "
				     "(this scene already defines its own camera motion across "
				     "one shutter interval - use a single-frame render instead)" << std::endl;
		return ERR_INVALID_ARGUMENTS;
	}

    // --sampler/--spectral only reach cpu_render_main() (the CPU default
    // path tracer) - checked here, before the video_mode branch below,
    // rather than further down near --exposure's warning, because --video
    // takes its own early return (see the `if (video_mode) { ... return...`
    // block) well before that later code runs. Checking only there meant
    // `--video --gpu --spectral`/`--sampler` silently dropped the flag with
    // no warning at all, unlike the identical combination without --video.
    {
        // Which renderer will run decides which of these flags it reads: one table (src/shared/backend_capabilities.h) instead of a copy of the rule per flag.
        using namespace backend_caps;
        const Renderer renderer = (use_bdpt || use_mlt || use_sppm || use_debug_integrator) ? Renderer::CpuOtherIntegrator
                                  : use_gpu ? (gpu_backend::kIsMetal ? Renderer::GpuMetal : Renderer::GpuOptix)
                                            : Renderer::CpuDefault;
        // Every option some renderer ignores, asked of the table: the warnings for --exposure, --tonemap, --seed, --denoise and --accelerator were hand-written
        // conditions of their own (here, and in a function of their own for the integrators), which could (and did) disagree with the table the GUI greys its controls by.
        const struct { Option option; bool requested; } flags[] = {
            {Option::Sampler, !args.sampler.empty()},
            {Option::LightSampler, !args.lightsampler.empty()},
            {Option::AdaptiveSampling, args.adaptive_sampling},
            {Option::TimeLimit, args.time_limit_seconds > 0.0},
            {Option::Spectral, args.spectral},
            {Option::Regularize, args.regularize},
            {Option::Denoise, args.denoise},
            {Option::DofOverride, args.aperture_override >= 0.0 || args.focus_distance_override >= 0.0},
            {Option::Seed, args.seed >= 0},   // (worth saying: a render that looks as if it honoured --seed would not be reproducible)
            {Option::Exposure, args.exposure != 1.0},
            {Option::Tonemap, args.tonemap != "aces"},
            {Option::Accelerator, !args.accelerator.empty() || !args.splitmethod.empty()},
            {Option::OptixValidate, args.optix_validate},
        };
        for (const auto &f : flags) {
            if (f.requested && !supports(renderer, f.option)) {
                std::cerr << "Warning: " << flagName(f.option) << " has no effect under " << rendererName(renderer)
                          << " - ignoring.\n";
            }
        }
    }
    // A native (non-.pbrt) scene has no Accelerator directive to override at
    // all - see SceneDescriptor::is_pbrt_backed's own comment.
    if ((!args.accelerator.empty() || !args.splitmethod.empty()) && !use_gpu &&
        !cpu_scene_is_pbrt_backed_by_id(scene_id.c_str())) {
        std::cerr << "Warning: --accelerator/--splitmethod have no effect on scene '" << scene_id
                  << "' (it is a native, non-.pbrt scene with no Accelerator directive to "
                     "override) - ignoring.\n";
    }
    if (args.draft_step > 1 && !(video_mode && cartoon::isCartoonPath(s.camera_path.c_str())))
        std::cerr << "Warning: --draft is for the video of a cartoon film (--video with --camera-path cartoon:<key>) - ignoring.\n";
    // --denoise-blend only means anything alongside an actual --denoise
    // pass - same "flag has no effect without its own gate" shape as
    // --maxcomponentvalue's checkbox/spinbox pairing (GUI) has, just for
    // the CLI's own --denoise/--denoise-blend pair.
    if (args.denoise_blend != 0.0 && !args.denoise) {
        std::cerr << "Warning: --denoise-blend has no effect without --denoise - ignoring.\n";
    }
	return SUCCESS;
}

// The "Launching renderer" / "VIDEO GENERATION MODE" banner.
static void print_render_banner(const LaunchArgs &args, const RenderSetup &s) {
	const bool use_gpu = s.use_gpu;
	const bool video_mode = s.video_mode;
	const int video_frames = s.video_frames;
	const int video_fps = s.video_fps;
	const double video_speed = s.video_speed;
	const std::string &camera_path = s.camera_path;
	const bool use_sppm = s.use_sppm;
	const bool use_bdpt = s.use_bdpt;
	const bool use_mlt = s.use_mlt;
	const bool use_randomwalk = s.use_randomwalk;
	const bool use_ao = s.use_ao;
	const bool use_simplepath = s.use_simplepath;
	const bool use_simplevolpath = s.use_simplevolpath;
	const bool use_lightpath = s.use_lightpath;

    if (video_mode) {
        std::cout << "\n========================================" << std::endl;
        std::cout << "VIDEO GENERATION MODE" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Frames: " << video_frames << std::endl;
        std::cout << "FPS: " << video_fps << std::endl;
        std::cout << "Speed: " << video_speed << "x" << std::endl;
        std::cout << "Camera path: " << camera_path << std::endl;
        std::cout << "Renderer: " << (use_gpu ? (args.use_wavefront ? "GPU (wavefront)" : "GPU") : "CPU") << std::endl;
    } else {
        std::cout << "\nLaunching renderer ("
                   << (use_bdpt ? "CPU BDPT"
                       : use_mlt ? "CPU MLT"
                       : use_sppm ? (use_gpu ? "GPU SPPM" : "CPU SPPM")
                       : use_randomwalk ? "CPU RandomWalk"
                       : use_ao ? "CPU AO"
                       : use_simplepath ? "CPU SimplePath"
                       : use_simplevolpath ? "CPU SimpleVolPath"
                       : use_lightpath ? "CPU LightPath"
                                  : (use_gpu ? (args.use_wavefront ? "GPU wavefront" : "GPU") : "CPU"))
                   << " mode)..." << std::endl;
    }
}

// --output, or <executable folder>/output/image.ppm; makes sure its folder exists and drops a run marker beside it.
static std::string prepare_output_path(const LaunchArgs &args, const char *argv0) {
    std::string out_path;

    if (!args.custom_output_path.empty()) {
        // Use custom output path from command line
        out_path = args.custom_output_path;
    } else {
        // Default: <executable_directory>/output/image.ppm
        std::filesystem::path exe_path = std::filesystem::absolute(argv0);
        std::filesystem::path exe_dir = exe_path.parent_path();
        std::filesystem::path outPath = exe_dir / "output" / "image.ppm";
        out_path = outPath.string();
    }

    // Ensure the output directory exists (create if needed)
    try {
        std::filesystem::path out_path_obj(out_path);
        if (!out_path_obj.parent_path().empty() && !std::filesystem::exists(out_path_obj.parent_path())) {
            std::filesystem::create_directories(out_path_obj.parent_path());
        }
    } catch (...) {
        // Directory creation errors are non-fatal (will fail later if needed)
    }

    // Write a runtime marker file to verify executable is running correctly
    // This is useful for debugging build/deployment issues
    try {
        std::string marker_path = out_path + ".run_marker.txt";
        FILE* mf = std::fopen(marker_path.c_str(), "w");
        if (mf) {
            std::fprintf(mf, "launcher started\n");
            std::fflush(mf);
            std::fclose(mf);
        }
    } catch (...) {
        // Marker creation is optional, ignore errors
    }
	return out_path;
}

// --probe X Y: renders ONE pixel with the path tracer reporting into a path_probe::Sink, then prints where that pixel's light came from (src/shared/path_probe.h). The CPU default path
// tracer only: the pixel is a 1 x 1 crop window of the image the other arguments describe, the samples-per-pixel argument is the number of paths averaged.
static int run_probe(const LaunchArgs &args, const RenderSetup &s) {
    if (s.use_gpu) std::cout << "Note: --probe runs on the CPU path tracer (the GPU backends do not report where light comes from)." << std::endl;
    if (s.use_sppm || s.use_bdpt || s.use_mlt || s.use_debug_integrator) {
        std::cerr << "ERROR: --probe reports on the default path tracer; it cannot be combined with --sppm, --bdpt, --mlt or the debug integrators." << std::endl;
        return ERR_INVALID_ARGUMENTS;
    }
    if (args.probe_x >= s.image_width || args.probe_y >= s.image_height) {
        std::cerr << "ERROR: --probe " << args.probe_x << " " << args.probe_y << " is outside the " << s.image_width << " x " << s.image_height << " image." << std::endl;
        return ERR_INVALID_ARGUMENTS;
    }
    RenderOptions opts = render_options_from_args(args);
    opts.crop_x0 = static_cast<double>(args.probe_x) / s.image_width;
    opts.crop_x1 = static_cast<double>(args.probe_x + 1) / s.image_width;
    opts.crop_y0 = static_cast<double>(args.probe_y) / s.image_height;
    opts.crop_y1 = static_cast<double>(args.probe_y + 1) / s.image_height;
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "photonsmith_probe_pixel.ppm";
    path_probe::Sink sink;
    path_probe::install(&sink);
    const int rc = cpu_render_main(s.image_width, s.image_height, s.samples_per_pixel, s.max_ray_depth, scratch.string().c_str(), s.scene_id.c_str(), s.cam_x, s.cam_y, s.cam_z, 1, opts);
    path_probe::install(nullptr);
    std::error_code ec;
    std::filesystem::remove(scratch, ec);
    std::filesystem::remove(scratch.string() + ".run_marker.txt", ec);
    if (rc != SUCCESS) {
        ErrorInfo err(rc);
        std::cerr << err.to_string() << std::endl;
        return rc;
    }
    const std::string text = path_probe::report(sink, args.probe_x, args.probe_y, s.image_width, s.image_height);
    std::cout << "\n========================================\nPIXEL PROBE\n========================================\n" << text << "========================================" << std::endl;
    if (!args.custom_output_path.empty()) {
        std::ofstream f(args.custom_output_path + ".probe.txt");
        f << text;
    }
    return SUCCESS;
}

// What every render entry point does with its return value: logs it, then the output path on success or the failure banner. `failure_note`, when
// given, replaces the error description in the banner (Metal prints its own explanation). Returns true on success.
static bool report_render_result(int render_result, const char *entry_point, const char *rendered_with, const char *failed_label,
                                 const std::string &out_path, const char *failure_note = nullptr) {
    std::cout << entry_point << " returned: " << render_result << std::endl;
    if (render_result == SUCCESS) {
        std::cout << "Rendered with " << rendered_with << " renderer, output: " << out_path << std::endl;
        return true;
    }
    std::cerr << "\n" << std::string(60, '=') << std::endl;
    std::cerr << failed_label << " RENDER FAILED" << std::endl;
    std::cerr << std::string(60, '=') << std::endl;
    if (failure_note) {
        std::cerr << failure_note;
    } else {
        ErrorInfo err(render_result);
        std::cerr << err.to_string() << std::endl;
    }
    std::cerr << std::string(60, '=') << "\n" << std::endl;
    return false;
}

// The default mode: one image from the chosen integrator and backend, then the timing, the --stats block and the PNG conversion.
// --aovs: the passes of the scene (cpu_render_main_aovs) next to the image. For an .exr image they are merged into it (R, G, B from the beauty plus the passes,
// one multilayer file); otherwise they go to <stem>.aovs.exr. A failure here is a warning: the image itself is already written.
static void write_aov_passes(const std::string &out_path, int width, int height, int samples, const std::string &scene_id, double cam_x, double cam_y, double cam_z) {
	namespace fs = std::filesystem;
	const fs::path image(out_path);
	const bool merge = is_exr_output_path(out_path);
	const fs::path passes = merge ? image.parent_path() / (image.stem().string() + ".aov-tmp.exr") : image.parent_path() / (image.stem().string() + ".aovs.exr");
	std::cout << "\nWriting the render passes (albedo, normal, depth, uv, coverage)..." << std::endl;
	const int result = cpu_render_main_aovs(width, height, std::min(samples, 16), passes.string().c_str(), scene_id.c_str(), cam_x, cam_y, cam_z, 1);
	if (result != 0) {
		std::cerr << "WARNING: the render passes could not be written (error " << result << "); the image itself is unaffected." << std::endl;
		return;
	}
	if (!merge) {
		std::cout << "  - " << passes.filename().string() << " (EXR - render passes)" << std::endl;
		return;
	}
	char error[512] = {0};
	if (cpu_merge_exr_passes(out_path.c_str(), passes.string().c_str(), error, static_cast<int>(sizeof error)) != 0) {
		std::cerr << "WARNING: could not merge the render passes into " << image.filename().string() << ": " << error << "; they are in "
		          << passes.filename().string() << " instead." << std::endl;
		return;
	}
	std::error_code ec;
	fs::remove(passes, ec);
	std::cout << "  - " << image.filename().string() << " now also holds the render passes (multilayer EXR: R G B + albedo, normal, depth, uv, A)" << std::endl;
}

// Runs the integrator the flags chose (BDPT, MLT, the debug integrators, SPPM, the GPU path tracer or the CPU path tracer) and reports its result.
// False when the render failed or the GPU is missing: `render_result` is then the code to exit with.
static bool runIntegrator(const LaunchArgs &args, const RenderSetup &s, double cam_x, double cam_y, double cam_z, const RenderOptions &render_opts, int &render_result) {
    const bool use_gpu = s.use_gpu;
    const int image_width = s.image_width;
    const int image_height = s.image_height;
    const int samples_per_pixel = s.samples_per_pixel;
    const int max_ray_depth = s.max_ray_depth;
    const std::string &scene_id = s.scene_id;
    const bool use_sppm = s.use_sppm;
    const bool use_bdpt = s.use_bdpt;
    const bool use_mlt = s.use_mlt;
    const bool use_randomwalk = s.use_randomwalk;
    const bool use_ao = s.use_ao;
    const bool use_simplepath = s.use_simplepath;
    const bool use_simplevolpath = s.use_simplevolpath;
    const bool use_lightpath = s.use_lightpath;
    const std::string &out_path = s.out_path;

    if (use_bdpt) {
        // BDPT Renderer, CPU path (Bidirectional Path Tracing). Implemented
        // in cpu_renderer/cpu_interface_bdpt.cpp - checked before use_sppm/
        // use_gpu below since it's a distinct render mode, not a path-tracer
        // variant (same shape as use_sppm's own priority - see this file's
        // earlier mutual-exclusion validation, which already rejected
        // --bdpt combined with --sppm/--mlt/--video, and the gpu_flag_explicit
        // warning above, which already covered --bdpt --gpu).
        std::cout << "Calling cpu_render_main_bdpt(...) in-process..." << std::endl;
        render_result = cpu_render_main_bdpt(image_width, image_height, samples_per_pixel,
                                              args.bdpt_max_depth, out_path.c_str(),
                                              scene_id.c_str(), cam_x, cam_y, cam_z, 1);  // force_camera_override
        if (!report_render_result(render_result, "cpu_render_main_bdpt", "BDPT", "BDPT", out_path))
            return false;
    } else if (use_mlt) {
        // MLT Renderer, CPU path (Metropolis Light Transport). Implemented
        // in cpu_renderer/cpu_interface_bdpt.cpp - same priority reasoning
        // as use_bdpt above.
        std::cout << "Calling cpu_render_main_mlt(...) in-process..." << std::endl;
        render_result = cpu_render_main_mlt(image_width, image_height, args.mlt_bootstrap,
                                             args.mlt_mutations, args.mlt_max_depth, out_path.c_str(),
                                             scene_id.c_str(), cam_x, cam_y, cam_z, 1);  // force_camera_override
        if (!report_render_result(render_result, "cpu_render_main_mlt", "MLT", "MLT", out_path))
            return false;
    } else if (use_randomwalk) {
        std::cout << "Calling cpu_render_main_randomwalk(...) in-process..." << std::endl;
        render_result = cpu_render_main_randomwalk(image_width, image_height, samples_per_pixel,
                                                     max_ray_depth, out_path.c_str(),
                                                     scene_id.c_str(), cam_x, cam_y, cam_z, 1);
        if (!report_render_result(render_result, "cpu_render_main_randomwalk", "RandomWalk", "RANDOMWALK", out_path))
            return false;
    } else if (use_ao) {
        std::cout << "Calling cpu_render_main_ao(...) in-process..." << std::endl;
        render_result = cpu_render_main_ao(image_width, image_height, samples_per_pixel,
                                            args.ao_max_dist, args.ao_cosine ? 1 : 0, args.ao_illum_scale,
                                            args.ao_illum_r, args.ao_illum_g, args.ao_illum_b,
                                            out_path.c_str(), scene_id.c_str(), cam_x, cam_y, cam_z, 1);
        if (!report_render_result(render_result, "cpu_render_main_ao", "AO", "AO", out_path))
            return false;
    } else if (use_simplepath) {
        std::cout << "Calling cpu_render_main_simplepath(...) in-process..." << std::endl;
        render_result = cpu_render_main_simplepath(image_width, image_height, samples_per_pixel,
                                                     max_ray_depth, args.simplepath_sample_lights ? 1 : 0,
                                                     args.simplepath_sample_bsdf ? 1 : 0, out_path.c_str(),
                                                     scene_id.c_str(), cam_x, cam_y, cam_z, 1);
        if (!report_render_result(render_result, "cpu_render_main_simplepath", "SimplePath", "SIMPLEPATH", out_path))
            return false;
    } else if (use_simplevolpath) {
        std::cout << "Calling cpu_render_main_simplevolpath(...) in-process..." << std::endl;
        render_result = cpu_render_main_simplevolpath(image_width, image_height, samples_per_pixel,
                                                        max_ray_depth, out_path.c_str(),
                                                        scene_id.c_str(), cam_x, cam_y, cam_z, 1);
        if (!report_render_result(render_result, "cpu_render_main_simplevolpath", "SimpleVolPath", "SIMPLEVOLPATH", out_path))
            return false;
    } else if (use_lightpath) {
        std::cout << "Calling cpu_render_main_lightpath(...) in-process..." << std::endl;
        render_result = cpu_render_main_lightpath(image_width, image_height, samples_per_pixel,
                                                    max_ray_depth, out_path.c_str(),
                                                    scene_id.c_str(), cam_x, cam_y, cam_z, 1);
        if (!report_render_result(render_result, "cpu_render_main_lightpath", "LightPath", "LIGHTPATH", out_path))
            return false;
    } else if (use_sppm && use_gpu) {
        // SPPM Renderer, GPU/OptiX path. Scope (which scenes are actually
        // supported) is determined dynamically from the built scene's real
        // materials/geometry/lights -- see gpu/optix/optix_interface.cpp's
        // sppm_gpu_unsupported_reason() for the exact, current rule set and
        // optix_render_main_sppm() returns a reason-specific
        // ERR_GPU_UNSUPPORTED_SCENE for anything still out of scope. Checked
        // before the CPU-only use_sppm branch below so --sppm --gpu actually
        // reaches the GPU path instead of silently falling back to CPU.
        if (!optix_is_available()) {
            std::cerr << "ERROR: OptiX is not available! (--sppm --gpu requires OptiX)" << std::endl;
            render_result = ERR_GPU_NO_DEVICE;
            return false;
        }
        std::cout << "Calling optix_render_main_sppm(...) in-process (OptiX)..." << std::endl;
        render_result = optix_render_main_sppm(image_width, image_height, args.sppm_iterations,
                                                args.sppm_photons, max_ray_depth, out_path.c_str(),
                                                scene_id.c_str(), cam_x, cam_y, cam_z, 1);  // force_camera_override
        if (render_result == ERR_GPU_UNSUPPORTED_SCENE) {
            // GPU SPPM only has hit records for spheres and quads (the reason is printed above): the CPU's SPPM renders every scene, so say so and use it rather than fail
            std::cerr << "WARNING: GPU SPPM cannot render this scene (see above); rendering it with CPU SPPM instead." << std::endl;
            render_result = cpu_render_main_sppm(image_width, image_height, args.sppm_iterations, args.sppm_photons, max_ray_depth, out_path.c_str(), scene_id.c_str(), cam_x, cam_y, cam_z, 1);
            if (!report_render_result(render_result, "cpu_render_main_sppm", "SPPM", "SPPM", out_path))
                return false;
        } else if (!report_render_result(render_result, "optix_render_main_sppm", "GPU SPPM", "GPU SPPM", out_path)) {
            return false;
        }
    } else if (use_sppm) {
        // SPPM Renderer, CPU path (Stochastic Progressive Photon Mapping).
        // Implemented in cpu_renderer/cpu_interface.cpp - takes priority
        // over --gpu/--cpu since SPPM is a distinct render mode, not a
        // path-tracer variant.
        std::cout << "Calling cpu_render_main_sppm(...) in-process..." << std::endl;
        render_result = cpu_render_main_sppm(image_width, image_height, args.sppm_iterations,
                                              args.sppm_photons, max_ray_depth, out_path.c_str(),
                                              scene_id.c_str(), cam_x, cam_y, cam_z, 1);  // force_camera_override
        if (!report_render_result(render_result, "cpu_render_main_sppm", "SPPM", "SPPM", out_path))
            return false;
    } else if (use_gpu) {
        // The GPU renderer this build has: Metal on a Mac (gpu/metal/host/metal_poc.mm renders a pbrt-file-backed scene_id and, for any other, prints a clear
        // message and returns non-zero), OptiX on Windows. Both are called through launcher/gpu_backend.h.
        if (!gpu_backend::available()) {
            std::cerr << "ERROR: " << gpu_backend::name() << " is not available!" << std::endl;
            render_result = ERR_GPU_NO_DEVICE;
            return false;
        }
        std::cout << "Calling " << gpu_backend::entryPoint() << "(...) in-process (" << gpu_backend::name() << ")..." << std::endl;
        std::cout << "[DEBUG] Camera: (" << cam_x << ", " << cam_y << ", " << cam_z << ")" << std::endl;
        render_result = gpu_backend::render(
            image_width,
            image_height,
            samples_per_pixel,
            max_ray_depth,
            out_path.c_str(),
            scene_id.c_str(),
            cam_x,
            cam_y,
            cam_z,
            1,  // force_camera_override (Metal honours it through MetalPocApp::applyCameraOverride() when the scene has a pbrt camera)
            render_opts
        );
        if (!report_render_result(render_result, gpu_backend::entryPoint(), gpu_backend::name(), gpu_backend::reportTag(), out_path, gpu_backend::failureHint()))
            return false;
    } else {
        // CPU Renderer (multithreaded C++)
        // Implemented in cpu_renderer/cpu_interface.cpp
        std::cout << "Calling cpu_render_main(...) in-process..." << std::endl;
        render_result = cpu_render_main(image_width, image_height, samples_per_pixel, max_ray_depth, out_path.c_str(), scene_id.c_str(), cam_x, cam_y, cam_z, 1, render_opts);  // force_camera_override
        if (!report_render_result(render_result, "cpu_render_main", "in-process CPU", "CPU", out_path))
            return false;
    }
    return true;
}

// "RENDER TIME: 850 ms / 12.34 seconds / 2 min 3.4 sec" between two rules.
static void printRenderTime(std::chrono::milliseconds duration, double seconds) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "RENDER TIME: ";
    if (seconds < 1.0) {
        std::cout << duration.count() << " ms" << std::endl;
    } else if (seconds < 60.0) {
        std::cout << std::fixed << std::setprecision(2) << seconds << " seconds" << std::endl;
    } else {
        int minutes = (int)(seconds / 60);
        double remainingSeconds = seconds - (minutes * 60);
        std::cout << minutes << " min " << std::fixed << std::setprecision(1) << remainingSeconds << " sec" << std::endl;
    }
    std::cout << "========================================" << std::endl;
}

// --stats on the CPU path tracer: the ray counts (render_stats.h) and samples per second.
static void printCpuStats(const LaunchArgs &args, const RenderSetup &s, double seconds) {
    const bool use_gpu = s.use_gpu;
    const bool use_bdpt = s.use_bdpt;
    const bool use_mlt = s.use_mlt;
    const bool use_sppm = s.use_sppm;
    const bool use_debug_integrator = s.use_debug_integrator;
    const int image_width = s.image_width;
    const int image_height = s.image_height;
    const int samples_per_pixel = s.samples_per_pixel;
    // Stats reporting (--stats, opt-in - see src/shared/render_stats.h)
    // ========================================================================
    // Same "default path tracer only" scope cut as --exposure/--sampler
    // above: BDPT/MLT/SPPM/the Round 6 debug integrators have their own
    // render loops, none of which are wired into render_stats.h's counters.
    // Both GPU backends print their own real, more detailed stats block
    // directly (wavefront_path_tracer.cpp's "[WF-STATS]",
    // optix_renderer_render.cpp's "[REC-STATS]" - both gated by the same
    // RAY_TRACER_STATS env var set above) - this block only covers CPU,
    // whose counters (render_stats.h) are only reachable from here.
    if (args.stats && !use_gpu && !use_bdpt && !use_mlt && !use_sppm && !use_debug_integrator) {
        // render_stats::primary_rays() is the REAL count of samples actually
        // taken - always equal to the render's true non-adaptive ceiling
        // unless --adaptive stopped some pixels early (camera::
        // adaptive_sampling's own comment), in which case using the old
        // width*height*samples_per_pixel calculation here would overstate
        // the work actually done (and Samples/sec along with it).
        const uint64_t primary_rays = render_stats::primary_rays().load(std::memory_order_relaxed);
        // The real per-pixel ceiling is sqrt_spp*sqrt_spp (camera.h's own
        // truncating int(sqrt(samples_per_pixel)) - see that field's own
        // comment), which is <= samples_per_pixel for any non-perfect-
        // square spp - using samples_per_pixel directly here would report
        // a nonzero "% fewer" purely from that truncation, even when
        // --adaptive triggers zero real early stops. Also scaled by the
        // requested --crop fraction (args.crop_x0/x1/y0/y1, NDC fractions,
        // default the full [0,1] frame) so a cropped render's naturally
        // smaller ray count isn't misattributed to adaptive sampling
        // either - approximate (camera.h does its own pixel-rounding/
        // empty-range fallback this can't see), but far closer than
        // ignoring crop entirely.
        const int sqrt_spp_stat = static_cast<int>(std::sqrt(static_cast<double>(samples_per_pixel)));
        const double crop_fraction = std::max(0.0, std::min(1.0,
            (args.crop_x1 - args.crop_x0) * (args.crop_y1 - args.crop_y0)));
        const long long theoretical_max_rays = static_cast<long long>(
            (double)image_width * (double)image_height * crop_fraction *
            (double)sqrt_spp_stat * (double)sqrt_spp_stat);
        const uint64_t total_rays = render_stats::bounce_rays().load(std::memory_order_relaxed);
        const uint64_t shadow_rays = render_stats::shadow_rays().load(std::memory_order_relaxed);
        std::cout << "[STATS] ── Render Statistics ──────────────────────────\n";
        std::cout << "[STATS] Primary rays          : " << primary_rays << "\n";
        if (args.adaptive_sampling && theoretical_max_rays > 0) {
            const double saved_pct = 100.0 * (1.0 - (double)primary_rays / (double)theoretical_max_rays);
            std::cout << "[STATS] Adaptive sampling     : " << saved_pct << "% fewer samples than the "
                       << theoretical_max_rays << " a fixed " << samples_per_pixel << " spp would have taken\n";
        }
        std::cout << "[STATS] Total rays (incl. bounces): " << total_rays << "\n";
        std::cout << "[STATS] Shadow rays (NEE)     : " << shadow_rays << "\n";
        std::cout << "[STATS] Samples/sec           : "
                   << (seconds > 0.0 ? (double)primary_rays / seconds : 0.0)
                   << "\n";
        std::cout << "[STATS] ─────────────────────────────────────────────\n";
    }
}

// A scene whose file's crop window is its picture (the pbrt-v4-scenes Head, SceneDescriptor::crop_is_picture): the renderers drew only the window, into a full-size frame with black
// around it; the finished image is cut to the window here, whichever renderer drew it, as pbrt writes it. An explicit --crop means the black-surrounded window and is left alone, and
// so is a render with extra passes (they would be left the wrong size) and a PNG written directly (Metal).
static void cutToScenePicture(const LaunchArgs &args, const std::string &scene_id, const std::string &out_path) {
    if (args.crop_x0 != 0.0 || args.crop_y0 != 0.0 || args.crop_x1 != 1.0 || args.crop_y1 != 1.0) return;
    double crop[4];
    if (!cpu_scene_crop_picture_by_id(scene_id.c_str(), crop)) return;
    const bool done = is_exr_output_path(out_path) ? output_crop::cropExrFile(out_path, crop) : output_crop::cropPpmFile(out_path, crop);
    if (!done) std::cerr << "Note: the scene's crop window is its picture, but '" << out_path << "' could not be cut to it; the full frame is kept." << std::endl;
}

// After a successful render: PPM to PNG (Metal and EXR output are already final) and the list of files to open. False when the PNG could not be written: the
// render then fails (the GUI trusts the exit code, and reported a render whose picture was missing as done).
static bool announceRenderedFile(const std::string &out_path, bool use_gpu, bool ppm_only) {
    (void)use_gpu;  // (read only by the Metal branch)
        std::filesystem::path ppm_path_obj(out_path);

#ifdef RT_HAVE_METAL
        if (use_gpu && !is_exr_output_path(out_path)) {
            // metal_render_main() (gpu/metal/metal_interface.h) writes a
            // real PNG directly at out_path via stbi_write_png for any
            // non-".exr" path (its ".exr" path writes a linear-HDR EXR
            // instead, handled by the is_exr_output_path() branch below,
            // same as CPU/OptiX) and never writes PPM. The
            // convert_ppm_to_png() call below would misparse those PNG
            // bytes as a PPM header and fail, so skip it here, the same
            // way the is_exr_output_path() branch below skips it for
            // tinyexr's own already-final output format.
            // The GUI (and users) expect the image at <stem>.png, but the
            // caller may have asked for <stem>.ppm: Metal writes PNG bytes
            // to whatever path it's given, so a .ppm name would hold PNG
            // data under the wrong extension and nothing would exist at
            // .png. Rename it to match the actual content.
            std::filesystem::path png_path = ppm_path_obj.parent_path() / (ppm_path_obj.stem().string() + ".png");
            if (ppm_path_obj != png_path) {
                std::error_code ec;
                std::filesystem::rename(ppm_path_obj, png_path, ec);
                if (ec) {
                    std::cerr << "WARNING: could not rename Metal output to " << png_path
                              << ": " << ec.message() << std::endl;
                    png_path = ppm_path_obj;
                }
            }
            std::cout << "\nRender complete! You can now open:" << std::endl;
            std::cout << "  - " << png_path.filename()
                       << " (PNG - written directly by the Metal renderer)" << std::endl;
        } else
#endif
        if (is_exr_output_path(out_path)) {
            // Every CLI render entry point (cpu_render_main()/optix_render_main()
            // and the BDPT/MLT/SPPM CPU+GPU entry points) is extension-aware
            // and already wrote a real EXR directly at out_path - see
            // camera.h's exr_output field comment, optix_interface.cpp's own
            // extension-detection branch, and sppm_write_exr()/bdpt_write_exr().
            // convert_ppm_to_png() below assumes PPM input, which an EXR is
            // not (its own PPM-header parser fails loudly on EXR's binary
            // magic bytes), and EXR is already a directly viewable/
            // compositable format that doesn't need a PNG derivative anyway.
            std::cout << "\nRender complete! You can now open:" << std::endl;
            std::cout << "  - " << ppm_path_obj.filename() << " (EXR - linear HDR, full float precision)" << std::endl;
        } else if (file_is_png(out_path)) {
            // (the GPU renderer wrote the PNG itself: it was asked for a .png file)
            std::cout << "\nRender complete! You can now open:" << std::endl;
            std::cout << "  - " << ppm_path_obj.filename() << " (PNG - lossless, widely supported)" << std::endl;
        } else if (ppm_only) {
            std::cout << "\nRender complete! You can now open:" << std::endl;
            std::cout << "  - " << ppm_path_obj.filename() << " (PPM - raw data)" << std::endl;
        } else {
            std::cout << "\nConverting to PNG format..." << std::endl;

            // Generate output path for PNG
            std::filesystem::path png_path = ppm_path_obj.parent_path() / (ppm_path_obj.stem().string() + ".png");

            // Convert PPM to PNG using external image_writer library
            if (convert_ppm_to_png(out_path.c_str(), png_path.string().c_str())) {
                std::cout << "✓ PNG saved: " << png_path << std::endl;
            } else {
                std::cerr << "ERROR: the picture could not be written as " << png_path << " (the PPM is " << ppm_path_obj << ")" << std::endl;
                return false;
            }

            std::cout << "\nRender complete! You can now open:" << std::endl;
            std::cout << "  - " << png_path.filename() << " (PNG - lossless, widely supported)" << std::endl;
            std::cout << "  - " << ppm_path_obj.filename() << " (PPM - raw data)" << std::endl;
        }
        return true;
}

static int render_single_image(const LaunchArgs &args, const RenderSetup &s) {
	const bool use_gpu = s.use_gpu;
	const int image_width = s.image_width;
	const int image_height = s.image_height;
	const int samples_per_pixel = s.samples_per_pixel;
	const std::string &scene_id = s.scene_id;
	double cam_x = s.cam_x;
	double cam_y = s.cam_y;
	double cam_z = s.cam_z;
	const std::string &out_path = s.out_path;

    // If the caller didn't explicitly pass cam_x/y/z, cam_x/y/z are just
    // sitting at LaunchArgs' generic Cornell-Box-scale struct defaults
    // (278,278,-800) - forcing that onto every scene regardless of its own
    // actual coordinate scale would misplace the camera exactly like a
    // stale leftover GUI spinbox value would (e.g. scene 1's spheres sit
    // within roughly +-15 units of the origin). Use the selected scene's
    // own recommended camera instead in that case. Either way,
    // force_camera_override=1 below makes sure the renderer actually uses
    // whichever of these two is now in cam_x/y/z, regardless of the
    // scene's CameraConfig.mode (Fixed scenes would otherwise silently
    // ignore it and use their own registry lookfrom unconditionally).
    if (!args.cam_explicit) {
        cpu_scene_recommended_camera(scene_id.c_str(), &cam_x, &cam_y, &cam_z, nullptr, nullptr, nullptr);
    }

    // Start timing for performance measurement
    auto start_time = std::chrono::high_resolution_clock::now();

    int render_result = -1; // 0 = success, non-zero = error

    // Used by the plain path-tracer branches only (use_gpu/else below) -
    // BDPT/MLT/SPPM/the debug integrators have no RenderOptions parameter
    // at all (see the warnings just above). See src/shared/
    // render_options.h's own field comments.
    RenderOptions render_opts = render_options_from_args(args);
    render_opts.output.look = look_for_scene(args, scene_id);
    // A GPU render asked for a .png file writes the PNG itself, straight from the picture (OutputOptions::write_png), with no text PPM for this program to read back and convert, unless
    // the picture is cut afterwards (a crop window, the scene's own or --crop: cutToScenePicture reads a PPM or an EXR).
    {
        const bool cropped = args.crop_x0 != 0.0 || args.crop_y0 != 0.0 || args.crop_x1 != 1.0 || args.crop_y1 != 1.0;
        double sceneCrop[4];
        if (!cropped && !cpu_scene_crop_picture_by_id(scene_id.c_str(), sceneCrop)) render_opts.output.write_png = &write_png;
    }

    if (!runIntegrator(args, s, cam_x, cam_y, cam_z, render_opts, render_result)) return render_result;

    // ========================================================================
    // Performance Reporting
    // ========================================================================
    // Calculate and display render time in appropriate units

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    double seconds = duration.count() / 1000.0;

    printRenderTime(duration, seconds);

    printCpuStats(args, s, seconds);

    if (render_result == 0 && !args.aovs) cutToScenePicture(args, scene_id, out_path);
    if (render_result == 0 && !announceRenderedFile(out_path, use_gpu, args.ppm_only)) render_result = ERR_FILE_WRITE_FAILED;

    if (render_result == 0 && args.aovs) write_aov_passes(out_path, image_width, image_height, samples_per_pixel, scene_id, cam_x, cam_y, cam_z);
    return render_result;
}

// A cartoon worker (launcher/cartoon_worker.h) renders a list of frames in this process: the first frame's scene is written now and named as the scene file, as for one frame, and every
// later frame rewrites that file. False (and says why) when the job cannot be started.
static bool prepare_cartoon_worker(LaunchArgs &args, cartoon_worker::Job &job) {
	if (const std::string problem = job.load(args.cartoon_worker); !problem.empty()) {
		std::cerr << problem << std::endl;
		return false;
	}
	std::error_code dir_ec;
	std::filesystem::create_directories(job.scenes, dir_ec);
	double first_eye[3] = {0, 0, 0};
	cartoon_worker::Job opening = job;   // (always written, even a frame that is only to be compared: the renderer starts from this file)
	opening.keep.clear();
	std::string fingerprint;
	bool unchanged = false;
	double share = 1.0;
	if (!opening.writeScene(job.list.front(), first_eye, fingerprint, unchanged, share)) {
		std::cerr << "Cannot write the scene of frame " << job.list.front() << " to " << job.scenePath() << std::endl;
		return false;
	}
	args.scene_file = job.scenePath();
	return true;
}

// `ray_tracer --film-check [FILE.film ...]`: checks film files (cartoon/film_file.h): each is loaded with its scene and either described or reported with the file and line of what is wrong. With
// no file it lists the films found in the scene folders and the files that were left out. Exit status 1 when anything is wrong.
static int film_check_command(int argc, char** argv) {
	int bad = 0;
	for (int i = 2; i < argc; ++i) {
		(void)cartoon::films();   // (the film registry knows the kinds of character a file can bring in)
		cartoon::film_file::DataFilm film;
		const std::string problem = film.load(argv[i]);
		if (!problem.empty()) {
			std::cout << "PROBLEM  " << problem << std::endl;
			++bad;
			continue;
		}
		std::cout << "OK       " << argv[i] << ": film \"" << film.spec.key << "\", " << film.spec.length << " s at " << film.spec.fps << " fps, " << film.shotStarts().size() << (film.shotStarts().size() == 1 ? " shot" : " shots")
				  << ", " << film.spec.anims.size() << " animation line" << (film.spec.anims.size() == 1 ? "" : "s") << ", " << film.scene.names.size() << " animated block" << (film.scene.names.size() == 1 ? "" : "s")
				  << (film.spec.characters.empty() ? std::string() : ", " + std::to_string(film.spec.characters.size()) + " character" + (film.spec.characters.size() == 1 ? "" : "s"))
				  << (film.spec.lines.empty() ? std::string() : ", " + std::to_string(film.spec.lines.size()) + " line" + (film.spec.lines.size() == 1 ? "" : "s") + " of dialogue") << std::endl;
	}
	if (argc == 2) {
		for (const cartoon::Film *f : cartoon::films())
			if (f->meta.id >= 1000) std::cout << "FOUND    " << f->key << " (" << cartoon::presetIdOf(*f) << "): " << f->name << ", " << cartoon::lengthText(*f) << std::endl;
		for (const std::string &p : cartoon::filmFileProblems()) {
			std::cout << "PROBLEM  " << p << std::endl;
			++bad;
		}
	}
return bad == 0 ? EXIT_SUCCESS : 1;
}

int main(int argc, char** argv) {
	// `ray_tracer --prepare-gpu`: builds the GPU renderer for this machine and exits (a Mac's first Metal render otherwise waits about a minute and a half for it). The GUI runs
	// this in the background at its start. Exit code 0 when it is ready or there is nothing to prepare.
	if (argc >= 2 && std::string(argv[1]) == "--prepare-gpu") {
		if (!gpu_backend::available()) { std::cerr << "No GPU renderer on this machine: nothing to prepare.\n"; return 0; }
		const bool ok = gpu_backend::prepare();
		std::cerr << (ok ? "The GPU renderer is ready.\n" : "The GPU renderer could not be prepared.\n");
		return ok ? 0 : 1;
	}
	// `ray_tracer --assemble-video <frames folder> <frame count> <frames per second> <out.mp4>`: puts a sequence of pictures (enc_0000.png, enc_0001.png, ... in that folder) together as an MP4, the way --video
	// does at its end (AVFoundation on a Mac, ffmpeg elsewhere). For a program that rendered the frames itself (the Scene Builder's Day cycle video, where the sun moves, not the camera). Exit 0 on success.
	if (argc >= 6 && std::string(argv[1]) == "--assemble-video") {
		const int count = std::atoi(argv[3]), fps = std::atoi(argv[4]);
		if (count < 1 || fps < 1) {
			std::cerr << "usage: " << argv[0] << " --assemble-video <frames folder> <frame count (>= 1)> <frames per second (>= 1)> <out.mp4>\n";
			return EXIT_FAILURE;
		}
		return assembleVideoFile(std::filesystem::path(argv[2]), count, fps, std::filesystem::path(argv[5]));
	}
	// `ray_tracer --export-gltf <scene id, name or .pbrt path> <out.glb>`: the scene as a binary glTF (src/shared/gltf_export.h), for Blender, a viewer or an engine. Notes on what was
	// approximated or left out go to standard output. Exit 0 on success.
	if (argc >= 2 && std::string(argv[1]) == "--export-gltf") {
		if (argc < 4) {
			std::cerr << "usage: " << argv[0] << " --export-gltf <scene id, name or .pbrt path> <out.glb>\n";
			return EXIT_FAILURE;
		}
		return cpu_export_gltf(argv[2], argv[3]) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	// `ray_tracer --scene-report <scene id, name or .pbrt path>`: what the renderers do not draw as written for that scene (the loader's warnings grouped by kind, the film, what it is pinned against in the
	// reference pack). Handled before the banner and the argument parser, like --print-scene-size below. Exit 0 when it loaded.
	if (argc >= 2 && std::string(argv[1]) == "--scene-report") {
		if (argc < 3) {
			std::cerr << "usage: " << argv[0] << " --scene-report <scene id, name or .pbrt path>\n";
			return EXIT_FAILURE;
		}
		return cpu_scene_report(argv[2]) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	// `ray_tracer --print-scene-size <scene id, name or .pbrt path>`: prints the scene's size (the largest side of its bounding box, in its own units) and
	// nothing else, for scripts/stamp_scene_sizes.py and for people who want the "# @rt-size" line for a scene of their own. Handled before the banner and the
	// argument parser: it is a measurement, not a render. Exit 0 and a number on success (0 means a scene with nothing to measure), exit 1 if it cannot be loaded.
	if (argc >= 2 && std::string(argv[1]) == "--print-scene-size") {
		if (argc < 3) {
			std::cerr << "usage: " << argv[0] << " --print-scene-size <scene id, name or .pbrt path>\n";
			return EXIT_FAILURE;
		}
		const double size = cpu_scene_measure_size(argv[2]);
		if (size < 0.0) return EXIT_FAILURE;
		std::printf("%.6g\n", size);
		return EXIT_SUCCESS;
	}
	// `ray_tracer --list-video-presets`: every video preset (camera-path flythroughs and cartoons, src/shared/video_preset.h), one per line, and nothing else.
	if (argc >= 2 && std::string(argv[1]) == "--list-video-presets") {
		std::cout << video_preset::listText();
		return EXIT_SUCCESS;
	}
	// `ray_tracer --film-table`: the cartoon films as the markdown table of docs/VIDEO_GENERATION.md (key, name, length, shots, tags, story), and nothing else.
	if (argc >= 2 && std::string(argv[1]) == "--film-table") {
		std::cout << cartoon::markdownTable();
		return EXIT_SUCCESS;
	}
	if (argc >= 2 && std::string(argv[1]) == "--film-check") return film_check_command(argc, argv);
	// `ray_tracer --film-shots <key>`: what a tool driving the films needs of one (scripts/anim/film.py doctor), as tab-separated lines: a `film` line (key, name, seconds, fps,
	// width, height, samples, measured seconds a frame) and a `shot` line for each shot (start, name, its share of the samples: the film's cameras file's `samples`).
	if (argc >= 2 && std::string(argv[1]) == "--film-shots") {
		const cartoon::Film *film = argc >= 3 ? cartoon::find(argv[2]) : nullptr;
		if (!film) {
			std::cerr << "usage: " << argv[0] << " --film-shots <film key>   (the keys: --list-video-presets)\n";
			return EXIT_FAILURE;
		}
		std::cout << "film\t" << film->key << "\t" << film->name << "\t" << film->duration << "\t" << film->fps << "\t" << film->meta.width << "\t" << film->meta.height << "\t"
				  << film->meta.samples << "\t" << film->meta.secondsPerFrame << "\n";
		const std::vector<double> starts = film->shotStarts();
		const std::vector<std::string> names = cartoon::shotNamesOf(*film);
		for (std::size_t i = 0; i < starts.size(); ++i) {
			const double middle = 0.5 * (starts[i] + (i + 1 < starts.size() ? starts[i + 1] : film->duration));
			std::cout << "shot\t" << starts[i] << "\t" << (i < names.size() ? names[i] : std::string()) << "\t" << cartoon::shot_cameras::sampleShareOf(film->headerAt(middle)) << "\n";
		}
		return EXIT_SUCCESS;
	}
	// `ray_tracer --film-subjects <key> <seconds> <width> <height>`: where the moment's subjects (Film::subjectsAt) fall in a picture of that size, one tab-separated line each:
	// `subject  i  x  y  distance  in-frame (1/0)  in-front (1/0)` (film_subjects.h). No line when the film names none.
	if (argc >= 2 && std::string(argv[1]) == "--film-subjects") {
		const cartoon::Film *film = argc >= 6 ? cartoon::find(argv[2]) : nullptr;
		if (!film) {
			std::cerr << "usage: " << argv[0] << " --film-subjects <film key> <seconds> <width> <height>\n";
			return EXIT_FAILURE;
		}
		const double t = std::atof(argv[3]);
		const int w = std::atoi(argv[4]), h = std::atoi(argv[5]);
		cartoon::Vec3 eye, target;
		double fov = 90.0;
		if (!film->subjectsAt || !film_subjects::cameraOf(film->headerAt(t), eye, target, fov)) return EXIT_SUCCESS;
		const std::vector<cartoon::Vec3> subjects = film->subjectsAt(t);
		for (std::size_t i = 0; i < subjects.size(); ++i) {
			const film_subjects::View v = film_subjects::project(eye, target, fov, subjects[i], w, h);
			std::cout << "subject\t" << i << "\t" << v.x << "\t" << v.y << "\t" << v.distance << "\t" << (v.inFrame ? 1 : 0) << "\t" << (v.inFront ? 1 : 0) << "\n";
		}
		return EXIT_SUCCESS;
	}
	// `ray_tracer --film-events <key>`: what the film's motion does that its soundtrack hears (Film::events), one tab-separated line each, in time order:
	// `event  seconds  kind  strength  pan  seed`. No line when the film reports none.
	if (argc >= 2 && std::string(argv[1]) == "--film-events") {
		const cartoon::Film *film = argc >= 3 ? cartoon::find(argv[2]) : nullptr;
		if (!film) {
			std::cerr << "usage: " << argv[0] << " --film-events <film key>\n";
			return EXIT_FAILURE;
		}
		static const char *kinds[] = {"footfall", "impact", "flash", "touch"};
		if (film->events)
			for (const cartoon::FilmEvent &e : film->events())
				std::cout << "event\t" << e.t << "\t" << kinds[static_cast<int>(e.kind)] << "\t" << e.strength << "\t" << e.pan << "\t" << e.seed << "\n";
		return EXIT_SUCCESS;
	}
	// `ray_tracer --film-baseline FILE [--write] [KEY ...]`: every film (or the ones named), shot by shot, against the baseline file (film_baseline.h): a tab-separated line for each
	// shot that differs (`changed`, `new` or `gone`, the film, the shot, its name) and the exit code 1; `--write` records them as they are now instead. The scenes are built at
	// preview coarseness (set here, before any film's code runs).
	if (argc >= 2 && std::string(argv[1]) == "--film-baseline") {
		if (argc < 3) {
			std::cerr << "usage: " << argv[0] << " --film-baseline FILE [--write] [KEY ...]" << std::endl;
			return EXIT_FAILURE;
		}
#ifdef _WIN32
		_putenv_s("RAY_TRACER_FILM_PREVIEW", "3");
#else
		setenv("RAY_TRACER_FILM_PREVIEW", "3", 1);
#endif
		const std::string file = argv[2];
		bool write = false;
		std::vector<std::string> keys;
		for (int i = 3; i < argc; ++i) {
			if (std::string(argv[i]) == "--write") write = true;
			else keys.push_back(argv[i]);
		}
		std::string text;
		{
			std::ifstream in(file, std::ios::binary);
			std::stringstream all;
			all << in.rdbuf();
			text = all.str();
		}
		const std::vector<cartoon::film_baseline::Entry> recorded = cartoon::film_baseline::parse(text);
		std::vector<cartoon::film_baseline::Entry> now;
		for (const cartoon::Film *film : cartoon::films()) {
			if (!keys.empty() && std::find(keys.begin(), keys.end(), std::string(film->key)) == keys.end()) continue;
			std::cerr << film->key << "..." << std::endl;
			const std::vector<cartoon::film_baseline::Entry> e = cartoon::film_baseline::entriesOf(*film, [](const std::string &scene) { return frame_fingerprint::of(scene); });
			now.insert(now.end(), e.begin(), e.end());
		}
		if (write) {
			std::ofstream out(file, std::ios::binary | std::ios::trunc);
			out << cartoon::film_baseline::merged(recorded, now);
			std::cout << "recorded " << now.size() << " shots in " << file << std::endl;
			return out ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		const std::vector<cartoon::film_baseline::Difference> diffs = cartoon::film_baseline::compare(recorded, now);
		for (const auto &d : diffs) std::cout << d.kind << '\t' << d.entry.film << '\t' << d.entry.shot << '\t' << d.entry.name << std::endl;
		std::cout << (diffs.empty() ? "the same" : std::to_string(diffs.size()) + " shot(s) differ") << " (" << now.size() << " shots looked at)" << std::endl;
		return diffs.empty() ? EXIT_SUCCESS : 1;
	}
	// `ray_tracer --film-cues <key>`: the film's cues file read and placed (music_cues.h), one tab-separated line a cue: `cue  seconds  instrument  notes (comma-separated)`,
	// and a `problem` line for each thing in it that cannot be played. Nothing when the film has no cues file.
	// `ray_tracer --film-lines <key>`: what a film file's characters say (film_speech.h), one tab-separated line each: `line  start  end  who  syllables  text`.
	if (argc >= 2 && std::string(argv[1]) == "--film-lines") {
		(void)cartoon::films();
		const cartoon::film_file::DataFilm *film = argc >= 3 ? cartoon::filmFileOf(argv[2]) : nullptr;
		if (!film) {
			std::cerr << "usage: " << argv[0] << " --film-lines <key of a film made from a film file>" << std::endl;
			return EXIT_FAILURE;
		}
		for (std::size_t i = 0; i < film->spec.lines.size(); ++i) {
			const cartoon::speech::Line &l = film->spec.lines[i];
			std::cout << "line" << '	' << l.t << '	' << cartoon::speech::endOf(film->spoken[i], l.t) << '	' << l.who << '	' << film->spoken[i].size() << '	' << l.text << std::endl;
		}
		return EXIT_SUCCESS;
	}
	// `ray_tracer --film-sound <key> OUT.wav`: a film's soundtrack alone (its music, sounds and lines), to listen to without rendering a frame.
	if (argc >= 2 && std::string(argv[1]) == "--film-sound") {
		const cartoon::Film *film = argc >= 4 ? cartoon::find(argv[2]) : nullptr;
		if (!film || !film->soundtrack) {
			std::cerr << "usage: " << argv[0] << " --film-sound <key of a film with a soundtrack> OUT.wav" << std::endl;
			return EXIT_FAILURE;
		}
		if (!cartoon_render::writeSoundtrack(*film, argv[3])) {
			std::cerr << "cannot write " << argv[3] << std::endl;
			return EXIT_FAILURE;
		}
		std::cout << "wrote " << argv[3] << std::endl;
		return EXIT_SUCCESS;
	}
	if (argc >= 2 && std::string(argv[1]) == "--film-cues") {
		const cartoon::Film *film = argc >= 3 ? cartoon::find(argv[2]) : nullptr;
		if (!film) {
			std::cerr << "usage: " << argv[0] << " --film-cues <film key>" << std::endl;
			return EXIT_FAILURE;
		}
		const std::string file = cartoon::music_cues::fileOf(film->key);
		if (file.empty()) return EXIT_SUCCESS;
		const cartoon::music_cues::Sheet sheet = cartoon::music_cues::read(file);
		const std::vector<double> starts = film->shotStarts ? film->shotStarts() : std::vector<double>();
		const std::vector<std::string> names = cartoon::shotNamesOf(*film);
		const std::map<std::string, cartoon::CueInstrument> own = film->instruments ? film->instruments() : std::map<std::string, cartoon::CueInstrument>();
		for (const std::string &p : sheet.problems) std::cout << "problem" << '\t' << p << std::endl;
		for (const cartoon::music_cues::Cue &c : sheet.cues) {
			double t = 0.0;
			std::string why;
			if (!cartoon::music_cues::timeOf(c, starts, names, t, why)) {
				std::cout << "problem" << '\t' << "line " << c.line << ": " << why << std::endl;
				continue;
			}
			if (c.instrument == "sample") {   // a recording: it must read
				const cartoon::music_cues::Recording &r = cartoon::music_cues::recording(cartoon::music_cues::recordingPath(sheet, c.file));
				if (r.sound.empty()) std::cout << "problem" << '\t' << "line " << c.line << ": the recording " << c.file << " " << (r.why.empty() ? "is empty" : r.why) << std::endl;
				else std::cout << "cue" << '\t' << t << '\t' << "sample " << c.file << (c.by.empty() ? "" : " by " + c.by) << '\t' << r.sound.size() / static_cast<double>(cartoon::audio::kRate) << " s" << std::endl;
				continue;
			}
			if (!own.count(c.instrument) && !cartoon::music_cues::genericInstruments().count(c.instrument)) {
				std::cout << "problem" << '\t' << "line " << c.line << ": no instrument called \"" << c.instrument << "\"" << std::endl;
				continue;
			}
			std::string notes;
			for (double n : c.notes) notes += (notes.empty() ? "" : ",") + std::to_string(static_cast<int>(std::lround(n)));
			std::cout << "cue" << '\t' << t << '\t' << c.instrument << '\t' << notes << std::endl;
		}
		return EXIT_SUCCESS;
	}
	// `ray_tracer --film-cuts <key>`: the cuts where screen direction breaks (film_checks.h), one tab-separated line each: `cut  shot after it (1 = the first)  kind  before
	// after  name`, kind `flip` (the same moving thing in the same place reverses across the screen), `journey` (a traveller cut to another place reverses) or `line` (two
	// subjects swap sides). No line when nothing breaks or the film names no subjects.
	if (argc >= 2 && std::string(argv[1]) == "--film-cuts") {
		const cartoon::Film *film = argc >= 3 ? cartoon::find(argv[2]) : nullptr;
		if (!film) {
			std::cerr << "usage: " << argv[0] << " --film-cuts <film key>" << std::endl;
			return EXIT_FAILURE;
		}
		const std::vector<std::string> names = cartoon::shotNamesOf(*film);
		for (const cartoon::film_checks::CutIssue &c : cartoon::film_checks::screenDirection(*film))
			std::cout << "cut" << '\t' << c.shot + 1 << '\t' << (c.crossedLine ? "line" : (c.samePlace ? "flip" : "journey")) << '\t' << c.before << '\t' << c.after << '\t'
					  << (c.shot < static_cast<int>(names.size()) ? names[static_cast<std::size_t>(c.shot)] : std::string()) << std::endl;
		return EXIT_SUCCESS;
	}
	// `ray_tracer --film-feet <key>`: how fast the planted feet of the film's characters move in each shot that has any (film_checks.h), one tab-separated line a shot:
	// `slide  shot (1 = the first)  worst (units a second)  when  mean  planted samples  name`. No line when the film reports no feet.
	if (argc >= 2 && std::string(argv[1]) == "--film-feet") {
		const cartoon::Film *film = argc >= 3 ? cartoon::find(argv[2]) : nullptr;
		if (!film) {
			std::cerr << "usage: " << argv[0] << " --film-feet <film key>" << std::endl;
			return EXIT_FAILURE;
		}
		const std::vector<std::string> names = cartoon::shotNamesOf(*film);
		for (const cartoon::film_checks::Slide &s : cartoon::film_checks::slidingFeet(*film))
			std::cout << "slide" << '\t' << s.shot + 1 << '\t' << s.worst << '\t' << s.at << '\t' << s.mean << '\t' << s.planted << '\t'
					  << (s.shot < static_cast<int>(names.size()) ? names[static_cast<std::size_t>(s.shot)] : std::string()) << std::endl;
		return EXIT_SUCCESS;
	}
	std::cout << "========================================" << std::endl;
	std::cout << "RAY TRACER LAUNCHER (Unified GPU/CPU)" << std::endl;
	std::cout << "========================================" << std::endl;

	// Parse command-line arguments
	LaunchArgs args;
	bool help_requested = false;
	if (!parse_launch_args(argc, argv, args, &help_requested)) {
		// --help is a deliberate, benign exit; anything else is a real parse
		// error already reported to stderr (bad scene_id, invalid
		// --video-preset, etc.) - exiting 0 for that case used to make the
		// GUI's RenderController (which trusts the exit code) report the
		// render as successful even though no image was ever written.
		return help_requested ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	// --preview: this program's film code and every program the video starts (they inherit the variable, and are given the flag) build the characters coarser
	// (cartoon_lib.h previewCoarsening, read once: so before any film's scene is made)
	if (args.preview) {
#ifdef _WIN32
		_putenv_s("RAY_TRACER_FILM_PREVIEW", "3");
#else
		setenv("RAY_TRACER_FILM_PREVIEW", "3", 1);
#endif
	}

	// --background (or RAY_TRACER_BACKGROUND=1): lower priorities for this process, which the programs it starts inherit (launcher/background_mode.h, decided by RunPolicy)
	const RunPolicy run_policy = RunPolicy::from(args);
	if (run_policy.background) {
		args.background = true;   // (so that the programs a video starts are given the flag too)
		std::cout << "Background mode: " << background_mode::apply(run_policy) << "." << std::endl;
	}

	// --gpu-device N: the renderer reads RAY_TRACER_GPU_DEVICE (gpu/optix/optix_renderer_init.cpp), and so do the programs a video starts
	if (args.gpu_device >= 0) set_env_var("RAY_TRACER_GPU_DEVICE", std::to_string(args.gpu_device).c_str());

	// Unpack for readability in the rest of main
	bool use_gpu            = args.use_gpu;
#if !defined(RT_HAVE_OPTIX) && !defined(RT_HAVE_METAL)
	// This build has no CUDA/OptiX SDK AND no Metal GPU support compiled in
	// (see launcher/optix_stub.h and gpu/metal/metal_interface.h's own
	// RT_HAVE_METAL guard) - GPU rendering was never compiled in at all.
	// Deliberately applied here, after parsing, rather than inside
	// parse_launch_args() itself: that function's LaunchArgs::use_gpu
	// default (true) is covered by its own unit tests
	// (tests/unit/launcher_args_bdpt_mlt_tests.cpp) independent of platform,
	// so the platform-capability decision belongs at the call site, next to
	// where every other use_gpu-affecting concern in main is applied - not
	// baked into the parser itself. Force CPU rendering unconditionally
	// rather than letting the true default reach the optix_is_available()/
	// Metal dispatch below and hard-fail every plain invocation; only warn
	// when the user actually typed --gpu, so a build known ahead of time to
	// be CPU-only degrades gracefully instead of refusing to render at all.
	if (use_gpu) {
		if (args.gpu_flag_explicit) {
			std::cerr << "Warning: --gpu was requested, but this build has no "
						 "GPU support (neither OptiX nor Metal) - rendering on "
						 "CPU instead.\n";
		}
		use_gpu = false;
	}
#endif
#ifdef RT_HAVE_METAL
	// SPPM has a CPU and an OptiX implementation and no Metal one, so on a Mac --sppm renders on the CPU, with the same warning --bdpt/--mlt and the debug
	// integrators give (it used to fall into the OptiX-only branch below and fail with "OptiX is not available!"). docs/BACKEND_SUPPORT.md's Metal column says CPU.
	if (use_gpu && args.use_sppm) {
		if (args.gpu_flag_explicit) std::cerr << "WARNING: --gpu is ignored under --sppm on a Mac (SPPM has a CPU and an OptiX implementation, no Metal one) - rendering on CPU" << std::endl;
		use_gpu = false;
	}
#endif
	int  image_width        = args.image_width;
	int  image_height       = args.image_height;
	int  samples_per_pixel  = args.samples_per_pixel;
	int  max_ray_depth      = args.max_ray_depth;
	std::string scene_id    = args.scene_id;
	// A cartoon video renders its own scene at every frame (launcher/cartoon_render.h); the scene named here is only a placeholder that keeps the checks below content.
	const bool cartoon_video = args.video_mode && args.camera_path_explicit && cartoon::isCartoonPath(args.camera_path.c_str());
	if (cartoon_video) {
		scene_id = "A1";
		args.scene_file.clear();
		args.cartoon_still.clear();   // (a cartoon scene named with a cartoon video: the frames bring their own scenes)
	}
	// One frame of a cartoon (cartoon:<key>[@seconds]) is rendered as the .pbrt file its scene text makes (launcher/cartoon_still.h); the file is removed when the run ends.
	cartoon_still::SceneFile cartoon_still_file;
	if (!args.cartoon_still.empty()) {
		cartoon::Still still;
		std::string still_error;
		cartoon::parseStill(args.cartoon_still, still, still_error);   // already checked by the argument parser
		if (!cartoon_still_file.write(still, std::filesystem::temp_directory_path())) {
			std::cerr << "Cannot write the scene of " << args.cartoon_still << " to " << cartoon_still_file.dir << std::endl;
			return EXIT_FAILURE;
		}
		if (use_gpu && still.film->meta.wavefront) args.use_wavefront = true;   // the renderer the film's frames are made with (FilmMeta::wavefront)
		std::cout << still.film->name << " at " << std::fixed << std::setprecision(2) << still.t << " s" << std::defaultfloat << std::endl;
		args.scene_file = cartoon_still_file.path.string();
	}
	cartoon_worker::Job worker_job;
	if (!args.cartoon_worker.empty() && !prepare_cartoon_worker(args, worker_job)) return EXIT_FAILURE;
	if (!args.scene_file.empty()) {
		// A .pbrt file named instead of a scene id: give it an id, before anything below touches the scene registry.
		std::error_code exists_ec;  // the throwing overload would abort on a path the OS rejects
		if (!std::filesystem::exists(args.scene_file, exists_ec)) {
			std::cerr << "Scene file not found: " << args.scene_file << std::endl;
			return EXIT_FAILURE;
		}
		char file_scene_id[32] = {};
		const int reg = cpu_register_scene_file(args.scene_file.c_str(), file_scene_id, static_cast<int>(sizeof file_scene_id));
		if (reg != 0) {
			std::cerr << "Cannot render " << args.scene_file << ": "
					  << (reg == 3 ? "it could not be read as a pbrt-v4 scene (see the message above)" : "the scene list was already built")
					  << std::endl;
			return EXIT_FAILURE;
		}
		scene_id = file_scene_id;
		std::cout << "Scene file " << args.scene_file << " is scene " << scene_id << std::endl;
	}
	{
		// A scene may be named by its slug ("cornell-box") or its id ("A1"); everything below works with the id.
		char resolved_id[32] = {};
		if (cpu_resolve_scene_id(scene_id.c_str(), resolved_id, static_cast<int>(sizeof resolved_id))) {
			if (scene_id != resolved_id) {
				std::cout << "Scene " << scene_id << " is " << resolved_id << std::endl;
				scene_id = resolved_id;
			}
		} else {
			// Unknown: say which scenes it might have meant (the render below then fails with the usual error).
			std::vector<std::string> near_slugs;
			for (int i = 0, n = cpu_scene_count(); i < n && near_slugs.size() < 5; ++i) {
				const std::string slug = cpu_scene_slug_by_id(cpu_scene_id(i));
				if (!slug.empty() && (slug.find(scene_id) != std::string::npos || scene_id.find(slug) != std::string::npos)) near_slugs.push_back(slug);
			}
			std::cerr << "Unknown scene \"" << scene_id << "\"." << std::endl;
			if (!near_slugs.empty()) {
				std::cerr << "Did you mean:";
				for (const std::string& slug : near_slugs) std::cerr << " " << slug;
				std::cerr << std::endl;
			}
		}
	}
	// Set once, here, before any entry point's first scene lookup - see
	// accelerator_override.h's own comment for why this needs to be a
	// process-global rather than threaded through RenderOptions (BDPT/MLT/
	// SPPM/the debug integrators take no RenderOptions parameter at all).
	accelerator_override::set({args.accelerator, args.splitmethod});
	double cam_x            = args.cam_x;
	double cam_y            = args.cam_y;
	double cam_z            = args.cam_z;
	bool video_mode         = args.video_mode;
	int  video_frames       = args.video_frames;
	int  video_fps          = args.video_fps;
	double video_speed      = args.video_speed;
	// The scene's own curated default (scene_registry.h's
	// recommended_camera_path_for(), e.g. Large Scenes defaulting to a
	// "linear" flythrough rather than an "orbit" that could circle through
	// a room's walls) only when the user didn't ask for a specific path
	// via --camera-path/-p or --video-preset - never overrides an actual
	// choice, matches the GUI's own auto-apply (mainwindow_slots.cpp's
	// onSceneChanged()) for the case where nothing more specific was asked
	// for either.
	std::string camera_path = args.camera_path_explicit
		? args.camera_path
		: cpu_scene_recommended_camera_path_by_id(scene_id.c_str());
	bool use_sppm           = args.use_sppm;
	bool use_bdpt           = args.use_bdpt;
	bool use_mlt            = args.use_mlt;
	bool use_randomwalk     = args.use_randomwalk;
	bool use_ao             = args.use_ao;
	bool use_simplepath     = args.use_simplepath;
	bool use_simplevolpath  = args.use_simplevolpath;
	bool use_lightpath      = args.use_lightpath;
	// Round 6 Phase 2: any one of these 5 debug integrators being active,
	// collapsed to a single flag - every check below that already treats
	// use_bdpt/use_mlt as "another special CPU-only render mode" (mutual
	// exclusion, --video rejection, --gpu warning) needs the same
	// treatment for these, and OR-ing 5 more names into every one of those
	// conditions individually would bury the actual logic.
	bool use_debug_integrator = use_randomwalk || use_ao || use_simplepath ||
	                             use_simplevolpath || use_lightpath;

	// optix_render_main() reads this env var itself (gpu/optix/optix_interface.cpp)
	// to pick the wavefront GPU backend over the default recursive one - set it
	// once here so it's in effect for both single-image and per-frame video
	// renders below. Meaningless under CPU/SPPM, so only set for a plain GPU render.
	if (use_gpu && !use_sppm && args.use_wavefront) {
		set_env_var("RAY_TRACER_WAVEFRONT", "1");
	}
	if (use_gpu && !use_sppm && args.optix_validate) {
		set_env_var("RAY_TRACER_OPTIX_VALIDATION", "1");
	}
	// Same env-var pattern as RAY_TRACER_WAVEFRONT above, but read by
	// wavefront_path_tracer.cpp's own "[WF-STATS]" block - not gated by
	// use_gpu/use_sppm since render_stats.h (the CPU-side counterpart) also
	// reads this env var, unconditionally of backend. render_stats::reset()
	// clears the process-lifetime counters right before this render starts,
	// so a leftover count from an earlier render (e.g. --video's per-frame
	// loop below) never bleeds into this one's printed stats.
	if (args.stats) {
		set_env_var("RAY_TRACER_STATS", "1");
	}
	render_stats::reset();

	// System-compatibility report instead of a render - see
	// launcher/diagnostics.h. Runs before any scene-loading or
	// output-directory side effects below, so this has no effect beyond
	// what it itself prints/writes.
	if (args.diagnose) {
		return run_diagnostics(args);
	}

	RenderSetup setup;
	setup.use_gpu = use_gpu;
	setup.image_width = image_width;
	setup.image_height = image_height;
	setup.samples_per_pixel = samples_per_pixel;
	setup.max_ray_depth = max_ray_depth;
	setup.scene_id = scene_id;
	setup.cam_x = cam_x;
	setup.cam_y = cam_y;
	setup.cam_z = cam_z;
	setup.video_mode = video_mode;
	setup.video_frames = video_frames;
	setup.video_fps = video_fps;
	setup.video_speed = video_speed;
	setup.camera_path = camera_path;
	setup.camera_keyframes_file = args.camera_keyframes_file;
	setup.use_sppm = use_sppm;
	setup.use_bdpt = use_bdpt;
	setup.use_mlt = use_mlt;
	setup.use_randomwalk = use_randomwalk;
	setup.use_ao = use_ao;
	setup.use_simplepath = use_simplepath;
	setup.use_simplevolpath = use_simplevolpath;
	setup.use_lightpath = use_lightpath;
	setup.use_debug_integrator = use_debug_integrator;

	const int options_rc = check_render_mode_options(args, setup);
	if (options_rc != SUCCESS) return options_rc;

	print_render_banner(args, setup);

	const std::string out_path = prepare_output_path(args, argv[0]);
	setup.out_path = out_path;
	setup.exe_path = std::filesystem::absolute(argv[0]).string();

    // Print configuration summary before rendering
    std::cout << "Using command-line settings: width=" << image_width << " height=" << image_height << " spp=" << samples_per_pixel << " max_depth=" << max_ray_depth 
              << " scene_id=" << scene_id << " camera=(" << cam_x << "," << cam_y << "," << cam_z << ")" << std::endl;
    std::cout << "Writing output to: " << out_path << std::endl;

	if (args.probe_x >= 0) return run_probe(args, setup);
	if (!args.cartoon_worker.empty())
		return cartoon_worker::run(worker_job, args, setup, [](const LaunchArgs &a, const RenderSetup &s) { return render_single_image(a, s); });
	return video_mode ? run_video_render(args, setup) : render_single_image(args, setup);
}
