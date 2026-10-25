#pragma once
// flag_scopes.h -- which command-line flags a cartoon video repeats in each frame's run, decided by ONE table.
//
// A cartoon video renders its frames as runs of the same program (a worker, or a process a frame), and each run must repeat the flags that say HOW to render (the backend, the denoiser, the
// seed, the exposure ...) and must NOT repeat the ones that belong to the video (`--video` would make every frame a video of its own; `--output` and `--height` are set for the frame).
// This used to be a blacklist of the video's flags: a new video-only flag that was not added to it was repeated by every frame. Now every flag has a scope here, the frame's flags are the
// ones whose scope is Render, and a flag that is not in the table is NOT repeated (with a warning), so forgetting one is noticed instead of passed on. A test (cartoon_tests.cpp) checks
// that every flag the usage text documents is in the table.

#include <cstring>
#include <iostream>
#include <set>
#include <string>
#include <vector>

enum class FlagScope {
	Render,    // how to render: repeated in every frame's run
	Video,     // the video's own (its frames, its path, its output, its workers): never repeated
	Mode,      // does something else than render (--help, --diagnose, --probe ...): never repeated
	Internal,  // set by the program itself for the runs it starts: never taken from the user's command line
};

struct FlagScopeEntry {
	const char *name;
	FlagScope scope;
};

inline constexpr FlagScopeEntry kFlagScopes[] = {
	// the video
	{"--video", FlagScope::Video}, {"--video-preset", FlagScope::Video}, {"--frames", FlagScope::Video}, {"-f", FlagScope::Video}, {"--fps", FlagScope::Video}, {"--speed", FlagScope::Video},
	{"--camera-path", FlagScope::Video}, {"-p", FlagScope::Video}, {"--camera-keyframes", FlagScope::Video}, {"--output", FlagScope::Video}, {"-o", FlagScope::Video},
	{"--height", FlagScope::Video},          // (each frame's run is given its own)
	{"--time-limit", FlagScope::Video},      // (the video's whole budget: each frame is given its share, cartoon_render.h)
	{"--cartoon-jobs", FlagScope::Video}, {"--cartoon-timeout", FlagScope::Video}, {"--cartoon-workers", FlagScope::Video}, {"--worker-max-mb", FlagScope::Video}, {"--resume", FlagScope::Video}, {"--draft", FlagScope::Video},
	{"--cartoon-worker", FlagScope::Video},  // (a worker is started by the video, with a job file of its own)
	// other things than rendering
	{"--help", FlagScope::Mode}, {"-h", FlagScope::Mode}, {"--diagnose", FlagScope::Mode}, {"--probe", FlagScope::Mode}, {"--list-video-presets", FlagScope::Mode},
	{"--film-table", FlagScope::Mode}, {"--film-check", FlagScope::Mode}, {"--film-shots", FlagScope::Mode}, {"--film-subjects", FlagScope::Mode}, {"--film-events", FlagScope::Mode}, {"--film-feet", FlagScope::Mode}, {"--film-cuts", FlagScope::Mode}, {"--film-cues", FlagScope::Mode}, {"--prepare-gpu", FlagScope::Mode}, {"--assemble-video", FlagScope::Mode}, {"--export-gltf", FlagScope::Mode},
	{"--scene-report", FlagScope::Mode}, {"--print-scene-size", FlagScope::Mode},
	// set by the program for its own runs
	{"--ppm-only", FlagScope::Internal}, {"--preview", FlagScope::Render},
	// how to render
	{"--gpu", FlagScope::Render}, {"-gpu", FlagScope::Render}, {"--gpu-device", FlagScope::Render}, {"--cpu", FlagScope::Render}, {"-cpu", FlagScope::Render}, {"--wavefront", FlagScope::Render}, {"--optix-validate", FlagScope::Render},
	{"--isolate-pbrt-lighting", FlagScope::Render}, {"--denoise", FlagScope::Render}, {"--denoise-guides", FlagScope::Render}, {"--denoise-blend", FlagScope::Render},
	{"--stats", FlagScope::Render}, {"--exposure", FlagScope::Render},
	{"--bloom", FlagScope::Render}, {"--vignette", FlagScope::Render}, {"--warmth", FlagScope::Render}, {"--saturation", FlagScope::Render}, {"--sampler", FlagScope::Render}, {"--aovs", FlagScope::Render}, {"--adaptive", FlagScope::Render},
	{"--adaptive-threshold", FlagScope::Render}, {"--lightsampler", FlagScope::Render}, {"--accelerator", FlagScope::Render}, {"--splitmethod", FlagScope::Render},
	{"--regularize", FlagScope::Render}, {"--maxcomponentvalue", FlagScope::Render}, {"--crop", FlagScope::Render}, {"--seed", FlagScope::Render}, {"--aperture", FlagScope::Render},
	{"--focus-distance", FlagScope::Render}, {"--spectral", FlagScope::Render}, {"--tonemap", FlagScope::Render},
	{"--background", FlagScope::Render},     // (the frames' runs lower their own priority too: run_policy.h)
	// the other integrators and their options
	{"--sppm", FlagScope::Render}, {"--sppm-iterations", FlagScope::Render}, {"--sppm-photons", FlagScope::Render}, {"--bdpt", FlagScope::Render}, {"--bdpt-max-depth", FlagScope::Render},
	{"--mlt", FlagScope::Render}, {"--mlt-bootstrap", FlagScope::Render}, {"--mlt-mutations", FlagScope::Render}, {"--mlt-max-depth", FlagScope::Render}, {"--randomwalk", FlagScope::Render},
	{"--ao", FlagScope::Render}, {"--ao-max-dist", FlagScope::Render}, {"--ao-uniform", FlagScope::Render}, {"--ao-illum-scale", FlagScope::Render}, {"--ao-illum-rgb", FlagScope::Render},
	{"--simplepath", FlagScope::Render}, {"--simplepath-no-lights", FlagScope::Render}, {"--simplepath-no-bsdf", FlagScope::Render}, {"--simplevolpath", FlagScope::Render},
	{"--lightpath", FlagScope::Render},
};

// The entry for a flag, or nullptr when the table does not know it.
inline const FlagScopeEntry *findFlagScope(const std::string &name) {
	for (const FlagScopeEntry &e : kFlagScopes)
		if (name == e.name) return &e;
	return nullptr;
}

// Whether a token starts a flag (and so ends the previous flag's values): "--anything", or a short flag the table knows (-f, -o, -p, -h, -cpu, -gpu). A value such as -1.5 is not one.
inline bool startsFlag(const std::string &token) {
	if (token.rfind("--", 0) == 0) return true;
	return token.size() > 1 && token[0] == '-' && findFlagScope(token) != nullptr;
}

// From the tokens the parser consumed (each flag followed by its values, as typed): those of the flags whose scope is Render, in order. A flag that is not in the table is left out and
// said so once (it would be new, and not yet classified).
inline std::vector<std::string> renderFlagTokens(const std::vector<std::string> &consumedTokens) {
	std::vector<std::string> out;
	bool repeat = false;   // whether the flag (and so its values) that the current token belongs to is repeated
	for (const std::string &token : consumedTokens) {
		if (startsFlag(token)) {
			const FlagScopeEntry *entry = findFlagScope(token);
			if (!entry) {
				static std::set<std::string> told;
				if (told.insert(token).second)
					std::cerr << "WARNING: " << token << " is not in launcher/flag_scopes.h, so the frames of a video are not given it." << std::endl;
			}
			repeat = entry && entry->scope == FlagScope::Render;
		}
		if (repeat) out.push_back(token);
	}
	return out;
}
