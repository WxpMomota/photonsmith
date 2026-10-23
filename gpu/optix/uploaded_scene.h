#pragma once
// uploaded_scene.h -- what "the scene already on the GPU" means, as one value with one rule.
//
// The OptiX renderer keeps ONE scene on the GPU between calls (a video renders thousands of frames of the same scene; Live Preview many of the same one), and a call reuses it when it is the
// scene it wants. Which scene is not enough: the default renderer needs a shader binding table that the wavefront renderer and GPU SPPM do not, and building it makes the default renderer's
// modules compile (20 minutes on a cold OptiX cache), so a scene is uploaded for the renderers that will draw it and serves only those. This used to be a bare scene id with the rest in a flag
// of the renderer set by two distant call sites; the rule now lives here.

#include <string>

namespace optix_renderer {

enum class RendererKind { Default, Wavefront, Sppm };

struct UploadedScene {
	// What identifies the uploaded scene (the scene id and, for a pbrt file, the file and when it was written: optix_interface.cpp's uploadKeyFor). "" = nothing uploaded.
	std::string key;
	// Whether the default renderer's shader binding table was built with it.
	bool defaultRendererSbt = false;

	// Whether the scene on the GPU can be drawn by `kind` as it is: it is the scene wanted and, for the default renderer, has the default renderer's SBT.
	bool serves(const std::string &wantedKey, RendererKind kind) const {
		return !key.empty() && key == wantedKey && (kind != RendererKind::Default || defaultRendererSbt);
	}
	// Remembers an upload of `uploadedKey`, made with or without the default renderer's SBT.
	void record(const std::string &uploadedKey, bool builtDefaultRendererSbt) {
		key = uploadedKey;
		defaultRendererSbt = builtDefaultRendererSbt;
	}
	void clear() {
		key.clear();
		defaultRendererSbt = false;
	}
};

}  // namespace optix_renderer
