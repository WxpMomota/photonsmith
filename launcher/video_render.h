#pragma once
// video_render.h -- the launcher's --video: the frames of a video rendered along a camera path (or, for a cartoon film, one child run per frame: cartoon_render.h), converted to PNG as they
// finish, resumable when interrupted (a cartoon's), and assembled into an MP4 with ffmpeg (or AVFoundation on a Mac), with a film's soundtrack when it has one. main() calls run_video_render
// and, for --assemble-video, assembleVideoFile.

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
#include "launcher/background_mode.h"
#ifdef RT_HAVE_AVFOUNDATION_ENCODER
#include "launcher/video_encoder_mac.h"   // Video -> MP4 via AVFoundation, no ffmpeg needed (used below)
#endif
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
#include "launcher/cartoon_still.h"  // one frame of a film as a temporary scene file  // cartoon video presets: one child run per frame
#include <fstream>
#include "src/shared/path_probe.h"    // --probe X Y
#include "launcher/diagnostics.h"     // --diagnose
#include "launcher/process.h"
#include "launcher/render_setup.h"
#include "launcher/cartoon_worker.h"

// Converts rendered PPM frames to PNG on a background thread while the main
// thread renders the next frame, instead of converting everything in a
// separate pass after the whole video has finished rendering. Especially
// valuable in GPU mode, where the CPU otherwise sits idle during rendering -
// this overlap is "free" wall-clock time. Frames are renumbered contiguously
// (enc_0000.png, enc_0001.png, ...) in the order they're pushed, independent
// of the original frame index, so a mid-run render failure (which just skips
// pushing that frame) doesn't leave a gap in the sequence - ffmpeg's
// sequential image2 demuxer stops at the first gap in a numbered sequence.
//
// Metal's own frame writer produces PNG directly (metal_render_main() only
// writes PNG or EXR), so on a Metal build a GPU video frame is already the
// final encoded file and needs no ppm->png conversion, only a rename into
// the same enc_NNNN.png sequence.
#if defined(RT_HAVE_METAL)
constexpr bool kMetalVideoFrames = true;
#else
constexpr bool kMetalVideoFrames = false;
#endif
class BackgroundPngConverter {
public:
    BackgroundPngConverter(std::filesystem::path frames_dir)
        : frames_dir_(std::move(frames_dir)), worker_(&BackgroundPngConverter::run, this) {}

    // Queue a successfully-rendered PPM for conversion. Safe to call from the
    // render loop while the worker thread is running.
    // `already_png`: the frame file is already PNG (a Metal frame), so the
    // worker moves it into the enc_NNNN.png sequence instead of converting.
    // `frame_index` >= 0 names the picture by its frame (a cartoon's, so that a frame that failed leaves a hole that can be filled and an interrupted video can be resumed); the default numbers
    // the pictures in the order pushed.
    void push(std::string frame_path, bool already_png = false, int frame_index = -1) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            queue_.push({std::move(frame_path), already_png, frame_index});
        }
        cv_.notify_one();
    }

    // Signals no more frames are coming, waits for the queue to drain, and
    // returns how many frames converted successfully.
    int finish() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            done_ = true;
        }
        cv_.notify_one();
        if (worker_.joinable()) worker_.join();
        return converted_;
    }

    // A render that gives up early (every frame failed, say) returns without calling finish(): a running std::thread that is destroyed ends the whole program abnormally.
    ~BackgroundPngConverter() { finish(); }

private:
    void run() {
        int next_index = 0;
        while (true) {
            QueuedFrame job;
            {
                std::unique_lock<std::mutex> lock(mtx_);
                cv_.wait(lock, [&] { return !queue_.empty() || done_; });
                if (queue_.empty() && done_) break;
                job = std::move(queue_.front());
                queue_.pop();
            }

            char enc_filename[32];
            std::snprintf(enc_filename, sizeof(enc_filename), "enc_%04d.png", job.index >= 0 ? job.index : next_index);
            const std::filesystem::path png_path = frames_dir_ / enc_filename;
            // written under another name and renamed when whole, so that a render stopped in the middle of a picture leaves no half picture that a resumed render would trust
            const std::filesystem::path tmp_path = frames_dir_ / (std::string(enc_filename) + ".tmp.png");
            bool ok;
            std::error_code ec;
            if (job.already_png) {
                std::filesystem::rename(job.path, tmp_path, ec);
                ok = !ec;
            } else {
                ok = convert_ppm_to_png(job.path.c_str(), tmp_path.string().c_str());
            }
            if (ok) {
                std::filesystem::rename(tmp_path, png_path, ec);
                ok = !ec;
            }
            if (ok) {
                if (job.index < 0) ++next_index;
                ++converted_;
                if (job.index >= 0) {
                    // a cartoon's rendered frame is kept as its picture only: the renderer's own files (a 1280 x 720 .ppm is 2.7 MB; a film has thousands) are not needed again
                    std::filesystem::remove(job.path, ec);
                    std::filesystem::path own_png = job.path;
                    own_png.replace_extension(".png");
                    std::filesystem::remove(own_png, ec);
                    std::filesystem::remove(job.path + ".run_marker.txt", ec);
                }
            } else {
                std::lock_guard<std::mutex> lock(mtx_);
                std::cerr << "\nWARNING: PNG conversion failed for " << job.path << std::endl;
            }
        }
    }

    std::filesystem::path frames_dir_;
    std::mutex mtx_;
    std::condition_variable cv_;
    struct QueuedFrame { std::string path; bool already_png; int index; };
    std::queue<QueuedFrame> queue_;
    bool done_ = false;
    int converted_ = 0;
    std::thread worker_;  // must be the last member so it's constructed last
};

// The camera of one frame of a --camera-keyframes video: where it is is the returned position; what it looks at, its field of view and its lens go into the render
// options (which the renderers read), so each frame is the whole camera and not only a moved position.
inline CameraPosition keyframe_camera_for_frame(const camera_keyframes::Path &keyframes, int frame, int frame_count, int fps, RenderOptions &render_opts) {
    // (the video's length in seconds is what the keyframes' holds are measured against)
    const camera_keyframes::Key key = camera_keyframes::at(keyframes, camera_keyframes::progressOf(frame, frame_count), static_cast<double>(frame_count) / std::max(fps, 1));
    render_opts.has_lookat_override = true;
    for (int a = 0; a < 3; ++a) render_opts.lookat_override[a] = key.target[a];
    if (key.fov > 0) render_opts.vfov_override = key.fov;
    if (key.hasLens()) {
        render_opts.aperture_override = 2.0 * key.lensRadius;   // the option is the aperture's diameter
        render_opts.focus_distance_override = key.focus;
    }
    return CameraPosition{key.pos[0], key.pos[1], key.pos[2], key.target[0], key.target[1], key.target[2], 0.0, 1.0, 0.0};
}

// --camera-keyframes: read and check the file before any frame is rendered. SUCCESS, or the error code to exit with.
inline int loadVideoKeyframes(const RenderSetup &s, camera_keyframes::Path &keyframes, bool &use_keyframes) {
    use_keyframes = !s.camera_keyframes_file.empty();
    if (use_keyframes) {
        const camera_keyframes::ParseResult parsed = camera_keyframes::load(s.camera_keyframes_file);
        if (!parsed.ok) {
            std::cerr << "ERROR: " << parsed.error << std::endl;
            return parsed.unreadable ? ERR_FILE_READ_FAILED : ERR_INVALID_ARGUMENTS;
        }
        keyframes = parsed.path;
        std::cout << "Camera path: " << keyframes.keys.size() << " keyframes from " << s.camera_keyframes_file << (keyframes.ease ? " (eased)" : "") << std::endl;
    }
    return SUCCESS;
}

// How many frames the video really renders: --frames spread by --speed, capped. SUCCESS, or the error code for a bad --frames or --speed.
inline int resolveVideoFrameCount(int video_frames, double video_speed, int video_fps, int &render_frame_count) {
    if (video_frames < 1) {
        std::cerr << "ERROR: --frames must be >= 1 (got " << video_frames << ")" << std::endl;
        return ERR_INVALID_ARGUMENTS;
    }
    if (video_speed <= 0.0) {
        std::cerr << "ERROR: --speed must be > 0 (got " << video_speed << ")" << std::endl;
        return ERR_INVALID_ARGUMENTS;
    }

    // --speed does not change the camera path itself (still always one
    // full baseline traversal - see camera_path.h) - it changes how many
    // frames that traversal is spread across, so a slower video takes
    // more frames (and more real time at the same fps) to cover the
    // exact same path, instead of covering less of the path in the same
    // frame count. --frames is the "1.0x" baseline frame count.
    render_frame_count = static_cast<int>(std::llround(video_frames / video_speed));
    if (render_frame_count < 1) render_frame_count = 1;
    const int kMaxVideoFrames = 20000;
    if (render_frame_count > kMaxVideoFrames) {
        std::cout << "WARNING: --speed " << video_speed << " with --frames " << video_frames
                   << " would need " << render_frame_count << " frames; capping at "
                   << kMaxVideoFrames << " frames." << std::endl;
        render_frame_count = kMaxVideoFrames;
    }
    if (render_frame_count != video_frames) {
        std::cout << "Speed " << video_speed << "x expands " << video_frames << " base frames to "
                   << render_frame_count << " actual frames ("
                   << std::fixed << std::setprecision(1)
                   << (static_cast<double>(render_frame_count) / video_fps) << "s at " << video_fps << " fps)."
                   << std::endl;
    }
    return SUCCESS;
}

// Pins the CPU thread count once for the whole video (see the comment).
inline void pinThreadsForVideo(bool use_gpu) {
    // cam.render() (called once per CPU frame, inside cpu_render_main())
    // auto-detects a "free core" count by sampling system idle time over
    // 200ms - fine for a single image, but that's 200ms wasted on every
    // single video frame for an answer that isn't going to meaningfully
    // change between frames of the same render. Sample it once here and
    // pin it via RAY_TRACER_THREADS so every frame's render() call hits
    // the explicit-override fast path instead of re-sampling. Skipped
    // entirely if the user already set RAY_TRACER_THREADS themselves.
    if (!use_gpu && !std::getenv("RAY_TRACER_THREADS")) {
        unsigned int nthreads = determine_render_thread_count();
        std::string nthreads_str = std::to_string(nthreads);
        set_env_var("RAY_TRACER_THREADS", nthreads_str.c_str());
        std::cout << "Pinned CPU thread count to " << nthreads << " for the whole video (skips per-frame detection)." << std::endl;
    }
}

struct VideoPaths {
    std::filesystem::path frames_dir;
    std::filesystem::path video_path;
};

// The temporary frame directory (emptied, unless `signature` says the frames in it are those of an interrupted run of this very video and `resume` asks to continue it) and the output video
// path. A cartoon's frames come with a signature file (cartoon_render.h, resumeSignature). SUCCESS, or ERR_FILE_WRITE_FAILED.
static int prepareVideoDirectories(const std::string &out_path, int render_frame_count, VideoPaths &paths, const std::string &signature = std::string(), bool resume = false, const std::string &frames_folder = "frames") {
    std::filesystem::path out_path_obj(out_path);
    std::filesystem::path frames_dir = out_path_obj.parent_path() / frames_folder;
    std::filesystem::path video_path = out_path_obj.parent_path() / (out_path_obj.stem().string() + "_video.mp4");
    paths.frames_dir = frames_dir;
    paths.video_path = video_path;

    try {
        bool keep = false;
        if (resume && !signature.empty() && std::filesystem::exists(frames_dir / "resume.txt")) {
            std::ifstream in(frames_dir / "resume.txt");
            const std::string saved((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            keep = saved == signature;
            if (!keep) std::cout << "The frames in " << frames_dir << " are not those of this video (another film, size, quality or flags); starting again." << std::endl;
        }
        if (keep) {
            // pictures that were being written when the earlier run stopped, and its working folders, are not frames
            for (const auto &entry : std::filesystem::directory_iterator(frames_dir)) {
                const std::string name = entry.path().filename().string();
                if (name.find(".tmp.png") != std::string::npos || entry.path().extension() == ".ppm" || entry.path().extension() == ".done") std::filesystem::remove(entry.path());
                else if (entry.is_directory()) std::filesystem::remove_all(entry.path());
            }
        } else {
            // Clear any frames left over from a previous video render - otherwise
            // stale files can mix with (or mask) this run's sequence, especially
            // when this run has fewer frames than the last one.
            if (std::filesystem::exists(frames_dir)) std::filesystem::remove_all(frames_dir);
            std::filesystem::create_directories(frames_dir);
        }
        if (!signature.empty()) {
            std::ofstream out(frames_dir / "resume.txt", std::ios::binary);
            out << signature;
        }
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Failed to create frames directory: " << e.what() << std::endl;
        std::cerr << "       Another render may still be using " << frames_dir << " (a render that is still running, or workers it left behind): stop it and try again." << std::endl;
        return ERR_FILE_WRITE_FAILED;
    }

    std::cout << "Temporary frame directory: " << frames_dir << std::endl;
    std::cout << "Output video: " << video_path << std::endl;
    std::cout << "Rendering " << render_frame_count << " frames..." << std::endl;
    return SUCCESS;
}

struct VideoCameraStart {
    double from_x, from_y, from_z;
    double at_x, at_y, at_z;
};

// Where the camera path starts: the scene's recommended camera, or the explicit --camera position.
inline VideoCameraStart videoCameraStart(const LaunchArgs &args, const RenderSetup &s) {
    const std::string &scene_id = s.scene_id;
    const double cam_x = s.cam_x, cam_y = s.cam_y, cam_z = s.cam_z;
    // Scale and phase-align the camera-path animation to this scene's
    // actual recommended camera, rather than every video using the same
    // Cornell-Box-scale orbit (radius 800 around (278,278,278), starting
    // at a fixed angle unrelated to any scene's actual default view)
    // regardless of scene - e.g. scene 1's spheres are clustered within
    // roughly +-15 units of the origin, so an 800-unit orbit radius
    // would show nothing but a tiny distant speck. Frame 0 of orbit/
    // linear/spiral lands exactly on this recommended camera position -
    // see camera_path.h's get_camera_position() - so switching between
    // Image and Video preview starts from the same view.
    double path_lookfrom_x = 278.0, path_lookfrom_y = 278.0, path_lookfrom_z = -800.0;
    double path_lookat_x = 278.0, path_lookat_y = 278.0, path_lookat_z = 278.0;
    if (cpu_scene_recommended_camera(scene_id.c_str(), &path_lookfrom_x, &path_lookfrom_y, &path_lookfrom_z,
                                      &path_lookat_x, &path_lookat_y, &path_lookat_z)) {
        std::cout << "Camera path starts at (" << path_lookfrom_x << ", " << path_lookfrom_y << ", " << path_lookfrom_z
                   << ") looking at (" << path_lookat_x << ", " << path_lookat_y << ", " << path_lookat_z
                   << ") (scene " << scene_id << "'s recommended camera)." << std::endl;
    }

    // If the user explicitly set a camera position (e.g. via the GUI's
    // X/Y/Z spinboxes or "Distance from Center" control), start the path
    // there instead of the scene's generic recommended camera - the
    // look-at point stays the scene's own, so this still preserves
    // get_camera_position()'s scale/height/start_angle derivation, just
    // decomposed from the user's chosen viewpoint rather than the
    // recommended one.
    if (args.cam_explicit) {
        path_lookfrom_x = cam_x;
        path_lookfrom_y = cam_y;
        path_lookfrom_z = cam_z;
        std::cout << "Camera path overridden to start at (" << path_lookfrom_x << ", " << path_lookfrom_y
                   << ", " << path_lookfrom_z << ") (explicit camera position)." << std::endl;
    }
    return {path_lookfrom_x, path_lookfrom_y, path_lookfrom_z, path_lookat_x, path_lookat_y, path_lookat_z};
}

// A worker as the pool sees it: a process (cartoon_render.h, runWorkers).
struct WorkerProcess : cartoon_render::WorkerHandle {
    Child child;
    bool running() override { return child.running(); }
    void kill() override { child.kill(); }
};

// A cartoon video's frames. With workers (the default on an OptiX GPU: cartoon_worker.h) a few long-lived processes each render a list of frames, which spares the second and a half every
// frame run spends starting up (most of a frame at a draft quality); otherwise one child run renders each frame, a few at once (cartoon_render.h). Either way a frame's picture is
// handed to `png_converter` in frame order, and a resumed video's finished frames are not rendered again. Returns the number of frames that are rendered.
inline int renderCartoonFrames(const LaunchArgs &args, const RenderSetup &s, const cartoon::Film &film, const std::filesystem::path &frames_dir, int render_frame_count,
                               BackgroundPngConverter &png_converter) {
    const bool use_gpu = s.use_gpu;
    const int image_width = s.image_width, image_height = s.image_height, samples_per_pixel = s.samples_per_pixel, max_ray_depth = s.max_ray_depth;
    const int workers = cartoon_render::workersFor(args, use_gpu, kMetalVideoFrames);
    const int jobs = cartoon_render::jobsFor(args, use_gpu);
    for (int w = 0; w < std::max(jobs, workers); ++w) std::filesystem::create_directories(frames_dir / ("work" + std::to_string(w)));
    const bool frame_is_png = use_gpu && kMetalVideoFrames;
    auto frame_file = [&](int frame) {
        char name[64];
        std::snprintf(name, sizeof(name), frame_is_png ? "frame_%04d.png" : "frame_%04d.ppm", frame);
        return frames_dir / name;
    };
    // frames a resumed run finds already rendered (their pictures are in the folder: prepareVideoDirectories kept them because the video is the same)
    std::vector<char> already(static_cast<std::size_t>(render_frame_count), 0);
    int resumed = 0;
    if (args.cartoon_resume) {
        for (int frame = 0; frame < render_frame_count; ++frame)
            if (std::filesystem::exists(frames_dir / cartoon_render::encName(frame))) already[static_cast<std::size_t>(frame)] = 1, ++resumed;
        if (resumed > 0) std::cout << "\nResuming: " << resumed << " of " << render_frame_count << " frames are already rendered." << std::endl;
    }
    const int timeout = cartoon_render::timeoutFor(args, use_gpu);
    // --time-limit is the whole video's budget (CPU only, as for any video): each frame gets its share, and `jobs` frames share the clock
    const double frameTimeLimit = (args.time_limit_seconds > 0.0 && !use_gpu) ? args.time_limit_seconds * jobs / std::max(1, render_frame_count - resumed) : 0.0;
    std::cout << "\nRendering " << (render_frame_count - resumed) << " frames of " << film.name << ", ";
    if (workers > 0) std::cout << workers << " workers";
    else std::cout << jobs << " at a time";
    if (timeout > 0) std::cout << " (a frame that takes more than " << timeout << " s is stopped and tried once more)";
    std::cout << "." << std::endl;
    int reported = resumed;
    auto completed = [&](int frame, int rc, double seconds) {   // as each frame finishes: one line, the count being how many are done
        if (already[static_cast<std::size_t>(frame)]) return;
        std::ostringstream line;
        line << "\n[" << ++reported << "/" << render_frame_count << "] Rendering " << frame_file(frame).filename().string() << " (" << film.name << " at " << std::fixed << std::setprecision(2)
             << film.duration * frame / std::max(1, render_frame_count) << " s)...";
        if (rc == 0) line << " \u2713 (" << std::setprecision(1) << seconds << "s)";
        else line << " \u2717 FAILED (error code: " << rc << ")";
        (rc == 0 ? std::cout : std::cerr) << line.str() << std::endl;
    };
    auto inOrder = [&](int frame, int rc) {   // in frame order: the video's picture sequence
        if (rc == 0 && !already[static_cast<std::size_t>(frame)]) png_converter.push(frame_file(frame).string(), frame_is_png, frame);
    };
    if (workers > 0) {
        std::vector<int> todo;
        for (int frame = 0; frame < render_frame_count; ++frame)
            if (!already[static_cast<std::size_t>(frame)]) todo.push_back(frame);
        cartoon_worker::Job job;
        job.film = film.key;
        job.frames = render_frame_count;
        job.out = frames_dir.string();
        cartoon_render::WorkerHooks hooks;
        hooks.start = [&](int worker, const std::vector<int> &frames) -> std::unique_ptr<cartoon_render::WorkerHandle> {
            const std::filesystem::path dir = frames_dir / ("work" + std::to_string(worker));
            cartoon_worker::Job mine = job;
            mine.scenes = dir.string();
            mine.list = frames;
            std::error_code ec;
            for (int f : frames) std::filesystem::remove(mine.markerPath(f), ec);   // (a marker left by an earlier worker is not this one's)
            const std::string file = (dir / "job.txt").string();
            {
                std::ofstream out(file, std::ios::trunc);
                out << mine.text();
            }
            std::vector<std::string> childArgs = cartoon_render::frameFlags(args, film, use_gpu);
            for (const std::string &a : {std::string("--cartoon-worker"), file, std::string("--output"), (dir / "unused.ppm").string(), std::string("--height"), std::to_string(image_height),
                                          std::to_string(image_width), std::to_string(samples_per_pixel), std::to_string(max_ray_depth)})
                childArgs.push_back(a);
            auto process = std::make_unique<WorkerProcess>();
            if (!process->child.start(s.exe_path, childArgs, "RAY_TRACER_PBRT_DIR", dir.string())) return nullptr;
            return process;
        };
        hooks.poll = [&](int frame, int &result) {
            const std::string marker = job.markerPath(frame);
            std::ifstream in(marker);
            if (!in) return false;
            int rc = -1;
            in >> rc;
            in.close();
            std::error_code ec;
            std::filesystem::remove(marker, ec);
            result = rc;
            return true;
        };
        return cartoon_render::runWorkers(todo, workers, timeout, hooks, completed, inOrder) + resumed;
    }
    return cartoon_render::runInParallel(
        render_frame_count, jobs,
        [&](int frame, int worker) {
            if (already[static_cast<std::size_t>(frame)]) return 0;
            const double film_time = film.duration * frame / std::max(1, render_frame_count);
            return cartoon_render::renderFrame(film, film_time, frames_dir / ("work" + std::to_string(worker)) / ("cartoon_" + std::to_string(frame) + ".pbrt"), s.exe_path, args, image_width,
                                               image_height, samples_per_pixel, max_ray_depth, frame_file(frame).string(), timeout, frameTimeLimit, use_gpu);
        },
        completed, inOrder);
}

// Renders every frame (the camera moving along the path), converts them to PNG on a background thread, and reports the counts. SUCCESS, or the error code to exit with;
// `converted` is the number of PNG frames the assembly will read.
inline int renderVideoFrames(const LaunchArgs &args, const RenderSetup &s, const VideoPaths &paths, int render_frame_count, const camera_keyframes::Path &keyframes,
                             bool use_keyframes, const VideoCameraStart &start, int &converted) {
    const bool use_gpu = s.use_gpu;
    const int image_width = s.image_width;
    const int image_height = s.image_height;
    const int samples_per_pixel = s.samples_per_pixel;
    const int max_ray_depth = s.max_ray_depth;
    const std::string &scene_id = s.scene_id;
    const int video_fps = s.video_fps;
    const std::string &camera_path = s.camera_path;
    const std::filesystem::path &frames_dir = paths.frames_dir;
    const double path_lookfrom_x = start.from_x, path_lookfrom_y = start.from_y, path_lookfrom_z = start.from_z;
    const double path_lookat_x = start.at_x, path_lookat_y = start.at_y, path_lookat_z = start.at_z;

    auto video_start_time = std::chrono::high_resolution_clock::now();
    int successful_frames = 0;

    // Converts each frame to PNG on a background thread as soon as it's
    // rendered, instead of after the whole video finishes - overlaps
    // conversion with the next frame's render (particularly valuable in
    // GPU mode, where the CPU is otherwise idle while the GPU renders).
    BackgroundPngConverter png_converter(frames_dir);

    // Built once rather than reconstructed every frame - args.* never
    // changes mid-video, so this is loop-invariant EXCEPT for
    // render_opts.seed, which the frame loop below deliberately
    // mutates in place each iteration when --seed was requested (see
    // that block's own comment for why a per-frame seed is needed).
    // See src/shared/render_options.h's own field comments.
    RenderOptions render_opts = render_options_from_args(args);
    // A cartoon preset (--camera-path cartoon:<key>): each frame is a scene of its own, rendered by a child run (launcher/cartoon_render.h).
    const cartoon::Film *cartoon_film = cartoon::forCameraPath(camera_path.c_str());

    // A cartoon's frames: by workers or by a child run each (renderCartoonFrames); the picture sequence is handed to the converter in frame order whatever order they finish in.
    if (cartoon_film) successful_frames = renderCartoonFrames(args, s, *cartoon_film, frames_dir, render_frame_count, png_converter);

    // Render each frame with animated camera position
    for (int frame = 0; frame < (cartoon_film ? 0 : render_frame_count); ++frame) {
        // --time-limit + --video: args.time_limit_seconds is a WHOLE-
        // VIDEO budget, not a per-frame one - re-derive each frame's
        // own render_opts.time_limit_seconds as whatever's LEFT of that
        // budget rather than reusing the same full value on every
        // frame (which would let an N-frame video take up to N times
        // the requested budget, since camera::render() captures its
        // own fresh render_start_time on every call and has no idea
        // this is frame K of a longer video). Skips (rather than
        // renders with an ~0 budget) any frame once the whole video's
        // time is already up, since a --time-limit-truncated frame is
        // mostly/entirely black and not worth spending a render call
        // on just to produce that.
        // CPU only - --time-limit has no effect under --gpu at all
        // (warned about separately, above, before video_mode dispatch),
        // so applying this per-frame budget logic under --gpu would
        // incorrectly cut a GPU video short based on a flag GPU never
        // actually consults.
        if (args.time_limit_seconds > 0.0 && !use_gpu) {
            const double elapsed = std::chrono::duration<double>(
                std::chrono::high_resolution_clock::now() - video_start_time).count();
            const double remaining = args.time_limit_seconds - elapsed;
            if (remaining <= 0.0) {
                std::cout << "\nTime limit (" << args.time_limit_seconds << "s) reached after "
                          << frame << " of " << render_frame_count
                          << " frames - stopping video early.\n";
                break;
            }
            render_opts.time_limit_seconds = remaining;
        }
        // Currently a no-op in practice - this branch always returns
        // before ever reaching the "[STATS]" print block below, which
        // is the only reader of these counters - but reset() here
        // keeps that true by construction rather than by accident:
        // without it, render_stats::bounce_rays()/shadow_rays()/
        // primary_rays() (incremented once per real sample inside
        // camera.h's render loop) would silently accumulate across
        // every frame of this loop, a trap for any future change that
        // prints or reuses these counters for a video render.
        render_stats::reset();
        // Get camera position for this frame
        CameraPosition cam_pos = get_camera_position(camera_path, frame, render_frame_count,
                                                        path_lookfrom_x, path_lookfrom_y, path_lookfrom_z,
                                                        path_lookat_x, path_lookat_y, path_lookat_z);
        if (use_keyframes) cam_pos = keyframe_camera_for_frame(keyframes, frame, render_frame_count, video_fps, render_opts);

        // Generate frame filename (e.g., frame_0001.ppm)
        // Metal's own writer only produces PNG (never PPM), so a Metal
        // frame is written directly as .png and handed to the converter
        // already encoded - see BackgroundPngConverter::push().
        const bool frame_is_png = use_gpu && kMetalVideoFrames;
        char frame_filename[256];
        std::snprintf(frame_filename, sizeof(frame_filename),
                      frame_is_png ? "frame_%04d.png" : "frame_%04d.ppm", frame);
        std::filesystem::path frame_path = frames_dir / frame_filename;

        // Progress indicator
        std::cout << "\n[" << (frame + 1) << "/" << render_frame_count << "] Rendering " << frame_filename << " (camera: " << std::fixed << std::setprecision(1) << cam_pos.lookfrom_x << ", "
                  << cam_pos.lookfrom_y << ", " << cam_pos.lookfrom_z << ")..." << std::flush;

        auto frame_start = std::chrono::high_resolution_clock::now();
        int render_result = -1;

        // --seed + --video: derive a per-frame seed from the base seed
        // and the frame index, instead of reusing the same literal
        // seed on every frame the way render_opts (built once, above)
        // otherwise would. Without this, every frame's RNG starts from
        // the exact same state (CPU: camera's private reseed_render_rng()
        // method is keyed only on scanline index, never frame number,
        // see its own comment, camera.h; GPU: frameNumber/frameNumber_ get overwritten
        // with the same literal seed on every render() call, see
        // optix_renderer_render.cpp/wavefront_path_tracer.cpp) -
        // producing a video with a visibly frozen, non-animating
        // grain/dither pattern instead of the natural per-frame
        // variation a video needs. Modular addition (not a re-hash)
        // is enough here: the same downstream mixing that already
        // decorrelates adjacent scanline/frame seeds (reseed_render_
        // rng()'s Hash(), GPU's pcg_hash() over frameNumber) does the
        // real work - this just needs to feed it a different starting
        // value per frame while staying reproducible (the same base
        // --seed always regenerates the same sequence of per-frame
        // seeds) and staying within the [0, 2147483647] range every
        // consumer expects (launcher_args.h's own parse-time bound).
        if (args.seed >= 0) {
            render_opts.seed = (args.seed + frame) % 2147483648LL;
        }

        // Render frame with GPU or CPU. force_camera_override=1: some
        // scenes (currently 1 and 2) otherwise ignore cam_x/y/z and
        // always use their own fixed single-image lookfrom - a video
        // must honor its per-frame animated camera regardless, or it
        // would render the same static frame video_frames times.
        if (use_gpu) {
            if (gpu_backend::available()) {   // Metal on a Mac, OptiX on Windows: launcher/gpu_backend.h
                render_result = gpu_backend::render(
                    image_width,
                    image_height,
                    samples_per_pixel,
                    max_ray_depth,
                    frame_path.string().c_str(),
                    scene_id.c_str(),
                    cam_pos.lookfrom_x,
                    cam_pos.lookfrom_y,
                    cam_pos.lookfrom_z,
                    1,  // force_camera_override
                    render_opts
                );
            } else {
                std::cerr << "\nERROR: " << gpu_backend::name() << " is not available!" << std::endl;
                return ERR_GPU_NO_DEVICE;
            }
        } else {
            render_result = cpu_render_main(
                image_width,
                image_height,
                samples_per_pixel,
                max_ray_depth,
                frame_path.string().c_str(),
                scene_id.c_str(),
                cam_pos.lookfrom_x,
                cam_pos.lookfrom_y,
                cam_pos.lookfrom_z,
                1,  // force_camera_override
                render_opts
            );
        }

        auto frame_end = std::chrono::high_resolution_clock::now();
        auto frame_duration = std::chrono::duration_cast<std::chrono::milliseconds>(frame_end - frame_start);
        background_mode::pauseAfter(std::chrono::duration<double>(frame_end - frame_start).count());   // (--background)

        if (render_result == SUCCESS) {
            successful_frames++;
            png_converter.push(frame_path.string(), frame_is_png);
            std::cout << " ✓ (" << (frame_duration.count() / 1000.0) << "s)" << std::endl;
        } else {
            std::cerr << " ✗ FAILED (error code: " << render_result << ")" << std::endl;
        }
    }

    auto video_end_time = std::chrono::high_resolution_clock::now();
    auto video_duration = std::chrono::duration_cast<std::chrono::seconds>(video_end_time - video_start_time);

    std::cout << "\n========================================" << std::endl;
    std::cout << "CONVERTING FRAMES" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Successfully rendered: " << successful_frames << "/" << render_frame_count << " frames" << std::endl;
    std::cout << "Rendering time: " << video_duration.count() << " seconds" << std::endl;

    if (successful_frames == 0) {
        std::cerr << "ERROR: No frames rendered successfully!" << std::endl;
        return ERR_FILE_WRITE_FAILED;
    }
    if (successful_frames < render_frame_count) {
        std::cout << "WARNING: " << (render_frame_count - successful_frames) << " of " << render_frame_count
                   << " frames failed to render and will be skipped - the video will be shorter than requested."
                   << std::endl;
    }

    // Most frames were already converted to PNG in the background while
    // later frames rendered (see png_converter.push() above) - this just
    // waits for the queue to drain (typically near-instant, since
    // conversion is far faster than a render) and gets the final count.
    std::cout << "Finishing PNG conversion..." << std::endl;
    converted = png_converter.finish();
    if (cartoon_film) {
        // every frame that has a picture, however it came (this run, or the run being resumed); a hole is filled with the frame before it so that the video keeps its length
        converted = cartoon_render::countFrames(frames_dir, render_frame_count);
        if (converted > 0 && converted < render_frame_count) {
            const int filled = cartoon_render::fillMissingFrames(frames_dir, render_frame_count);
            std::cout << "WARNING: " << (render_frame_count - converted) << " frame(s) failed; each is repeated from the frame before it so that the video keeps its length"
                      << (filled == render_frame_count - converted ? "." : " (some could not be).") << std::endl;
            converted = cartoon_render::countFrames(frames_dir, render_frame_count);
        }
    }

    std::cout << "Rendered: " << successful_frames << " frames, converted: " << converted << " PNG files" << std::endl;

    if (converted == 0) {
        std::cerr << "ERROR: No frames converted to PNG successfully!" << std::endl;
        return ERR_FILE_WRITE_FAILED;
    }
    return SUCCESS;
}

// Assembles the PNG sequence into the MP4: AVFoundation on a Mac, ffmpeg otherwise (and as the fallback). With `audio_path` (a film's soundtrack, a .wav) the sound is added too, which
// only ffmpeg here can do; when that fails the picture is made without it and the .wav stays beside the video. SUCCESS or ERR_VIDEO_ASSEMBLY_FAILED.
inline int assembleVideoFile(const std::filesystem::path &frames_dir, int converted, int video_fps, const std::filesystem::path &video_path, const std::filesystem::path &audio_path = std::filesystem::path()) {
    // Assemble the PNG sequence into an MP4 with ffmpeg
    std::cout << "\n========================================" << std::endl;
    std::cout << "ASSEMBLING VIDEO WITH FFMPEG" << std::endl;
    std::cout << "========================================" << std::endl;

    // -g (one keyframe per second) and -movflags +faststart: without
    // them libx264's default ~250-frame GOP can leave a short render
    // with just its very first frame as a keyframe, and the moov atom
    // (seek index) lands at the end of the file - together those made
    // seeking backward in the GUI's embedded QMediaPlayer preview
    // unreliable (seeking forward mostly worked by chance since it
    // could just keep decoding ahead to the target).
    const std::string enc_pattern = (frames_dir / "enc_%04d.png").string();
    auto runFfmpeg = [&](bool with_audio) {
        std::vector<std::string> ffmpeg_argv = {"ffmpeg", "-y", "-r", std::to_string(video_fps), "-i", enc_pattern};
        std::string manual_cmd = "ffmpeg -y -r " + std::to_string(video_fps) + " -i \"" + enc_pattern + "\"";
        if (with_audio) {
            ffmpeg_argv.push_back("-i");
            ffmpeg_argv.push_back(audio_path.string());
            manual_cmd += " -i \"" + audio_path.string() + "\"";
        }
        // yuv420p needs an even width and height; a picture of an odd size (the GUI's size boxes allow one) loses its last row or column rather than the whole video failing
        const std::vector<std::string> codec = {"-vf", "crop=trunc(iw/2)*2:trunc(ih/2)*2", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-g", std::to_string(video_fps)};
        ffmpeg_argv.insert(ffmpeg_argv.end(), codec.begin(), codec.end());
        manual_cmd += " -vf \"crop=trunc(iw/2)*2:trunc(ih/2)*2\" -c:v libx264 -pix_fmt yuv420p -g " + std::to_string(video_fps);
        if (with_audio) {
            const std::vector<std::string> sound = {"-c:a", "aac", "-b:a", "192k", "-shortest"};
            ffmpeg_argv.insert(ffmpeg_argv.end(), sound.begin(), sound.end());
            manual_cmd += " -c:a aac -b:a 192k -shortest";
        }
        ffmpeg_argv.push_back("-movflags");
        ffmpeg_argv.push_back("+faststart");
        ffmpeg_argv.push_back(video_path.string());
        manual_cmd += " -movflags +faststart \"" + video_path.string() + "\"";
        std::cout << "Running: " << manual_cmd << std::endl;
        const int result = run_subprocess(ffmpeg_argv);
        if (result == -1) {
            std::cerr << "ERROR: Could not launch ffmpeg - is it installed and on PATH?" << std::endl;
            std::cerr << "Install ffmpeg (https://ffmpeg.org/download.html), add it to PATH, then assemble manually:" << std::endl;
            std::cerr << "  " << manual_cmd << std::endl;
        } else if (result != 0) {
            std::cerr << "ERROR: ffmpeg exited with code " << result << std::endl;
            std::cerr << "Rendered frames are still available in: " << frames_dir << std::endl;
            std::cerr << "You can retry assembly manually with:" << std::endl;
            std::cerr << "  " << manual_cmd << std::endl;
        }
        return result;
    };

    bool video_done = false;
    if (!audio_path.empty()) {
        if (runFfmpeg(true) == 0) video_done = true;
        else std::cerr << "WARNING: the sound could not be added; making the video without it (the sound is saved as " << audio_path << ")." << std::endl;
    }
    // macOS: assemble with AVFoundation (built in, no ffmpeg needed - the GUI's render subprocess does not see a Homebrew
    // ffmpeg anyway). The banner above is kept verbatim: the GUI keys its "Assembling video" status on it. ffmpeg stays as
    // the fallback (and the only path on Windows/Linux).
#ifdef RT_HAVE_AVFOUNDATION_ENCODER
    if (!video_done) {
        std::cout << "Encoding with AVFoundation (built into macOS, no ffmpeg needed)..." << std::endl;
        std::string av_error;
        if (encode_png_sequence_to_mp4_avfoundation(frames_dir.string(), converted, video_fps, video_path.string(), av_error)) {
            video_done = true;
        } else {
            std::cerr << "WARNING: the built-in macOS encoder failed (" << av_error << "); trying ffmpeg instead..." << std::endl;
        }
    }
#else
    (void)converted;
#endif
    if (!video_done && runFfmpeg(false) != 0) return ERR_VIDEO_ASSEMBLY_FAILED;
    return SUCCESS;
}

// --video: renders the frames along a camera path, converts them to PNG as they finish, and assembles an MP4.
inline int run_video_render(const LaunchArgs &args, const RenderSetup &s) {
    const int image_width = s.image_width;
    const int image_height = s.image_height;

    std::cout << "\n========================================" << std::endl;
    std::cout << "VIDEO RENDERING" << std::endl;
    std::cout << "========================================" << std::endl;

    camera_keyframes::Path keyframes;
    bool use_keyframes = false;
    if (int rc = loadVideoKeyframes(s, keyframes, use_keyframes); rc != SUCCESS) return rc;
    int render_frame_count = 0;
    if (int rc = resolveVideoFrameCount(s.video_frames, s.video_speed, s.video_fps, render_frame_count); rc != SUCCESS) return rc;
    pinThreadsForVideo(s.use_gpu);
    VideoPaths paths;
    std::string resume_signature;
    if (const cartoon::Film *film = cartoon::forCameraPath(s.camera_path.c_str()))
        resume_signature = cartoon_render::resumeSignature(*film, args, render_frame_count, s.video_fps, image_width, image_height, s.samples_per_pixel, s.max_ray_depth, s.exe_path);
    // a cartoon's frames are in a folder of the film's own (frames-<key>), so that rendering another film does not wipe the frames of one that was stopped, and two films never share a folder
    const cartoon::Film *frames_film = cartoon::forCameraPath(s.camera_path.c_str());
    const std::string frames_folder = frames_film ? std::string("frames-") + frames_film->key : std::string("frames");
    if (int rc = prepareVideoDirectories(s.out_path, render_frame_count, paths, resume_signature, args.cartoon_resume, frames_folder); rc != SUCCESS) return rc;
    const VideoCameraStart start = cartoon::isCartoonPath(s.camera_path.c_str()) ? VideoCameraStart{0, 0, 0, 0, 0, 0} : videoCameraStart(args, s);

    int converted = 0;
    if (int rc = renderVideoFrames(args, s, paths, render_frame_count, keyframes, use_keyframes, start, converted); rc != SUCCESS) return rc;
    // a film with a soundtrack gets it, when the video is the film's own length (the sound is made for that: a shorter or faster video would not fit it)
    std::filesystem::path soundtrack_path;
    if (const cartoon::Film *film = cartoon::forCameraPath(s.camera_path.c_str()); film && film->soundtrack) {
        if (converted != render_frame_count) {
            std::cout << "\nNote: " << (render_frame_count - converted) << " frame(s) failed, so the sound (made for every frame) would drift out of step; the video is made without it." << std::endl;
        } else if (cartoon_render::soundtrackFits(*film, render_frame_count, s.video_fps)) {
            soundtrack_path = paths.video_path;
            soundtrack_path.replace_extension(".wav");
            std::cout << "\nMaking the soundtrack of " << film->name << "..." << std::endl;
            if (!cartoon_render::writeSoundtrack(*film, soundtrack_path)) {
                std::cerr << "WARNING: the soundtrack could not be written to " << soundtrack_path << "; the video will be silent." << std::endl;
                soundtrack_path.clear();
            }
        } else {
            std::cout << "\nNote: " << film->name << " has a soundtrack, made for " << cartoon::frameCountOf(*film) << " frames at " << film->fps << " frames per second; this video has "
                      << render_frame_count << " at " << s.video_fps << ", so it is made without sound." << std::endl;
        }
    }
    if (int rc = assembleVideoFile(paths.frames_dir, converted, s.video_fps, paths.video_path, soundtrack_path); rc != SUCCESS) return rc;

    std::cout << "\n========================================" << std::endl;
    std::cout << "VIDEO COMPLETE!" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Output video: " << paths.video_path << std::endl;
    std::cout << "Resolution: " << image_width << "x" << image_height << std::endl;
    std::cout << "========================================" << std::endl;

    return SUCCESS;
}
