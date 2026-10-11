#pragma once
// scene_viewport.h -- the decisions behind the Scene Builder's rendered viewport (the 3D view showing a progressive path-traced picture of the scene you are editing): what a change
// of the view or of the document means for the renderer, and how big to draw. Qt-free and std-only, so the unit tests and the GUI use the same code.
//
// The viewport is a Live Preview session (a GPU renderer that refines one picture frame after frame) running on a copy of the scene written from the document. Three things can
// happen, and they cost very different amounts:
//   * the view's eye or target moved (orbit, pan, zoom): tell the renderer the new camera; it keeps what it can of the picture (cheap);
//   * the picture's size or field of view changed (the window was resized, a different lens): the renderer starts the picture again from the same scene (a restart);
//   * the document changed so that the scene is different: write the copy again and start again (a restart, after the edits have paused).
// An edit that does not change the scene (a saved view, hiding something in the views, a name, the picture size for rendering) must not restart anything: sceneSignature() is what
// is compared to know.

#include "scene_document.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace scene_viewport {

struct Pose {
	double eye[3] = {0, 0, 0};
	double target[3] = {0, 0, -1};
	double fovDeg = 40.0;    // vertical
	int width = 0, height = 0;   // the picture being rendered, in pixels
};

enum class Change { None, Camera, Restart };

// What the renderer must do to go from the pose it was last given to `now`.
inline Change classify(const Pose& sent, const Pose& now) {
	if (sent.width != now.width || sent.height != now.height || std::fabs(sent.fovDeg - now.fovDeg) > 1e-6) return Change::Restart;
	for (int a = 0; a < 3; ++a)
		if (std::fabs(sent.eye[a] - now.eye[a]) > 1e-9 || std::fabs(sent.target[a] - now.target[a]) > 1e-9) return Change::Camera;
	return Change::None;
}

// The size of the picture to render for a widget `width` x `height` pixels: the widget's shape (so what is drawn lines up with the views' own drawing), at most `maxWidth` wide
// (a path tracer's cost is its pixel count; the picture is drawn scaled up), and at least 16 pixels each way.
inline void renderSize(int width, int height, int maxWidth, int& outWidth, int& outHeight) {
	width = std::max(1, width);
	height = std::max(1, height);
	const double scale = width > maxWidth ? static_cast<double>(maxWidth) / width : 1.0;
	outWidth = std::max(16, static_cast<int>(std::lround(width * scale)));
	outHeight = std::max(16, static_cast<int>(std::lround(height * scale)));
}

// What the scene is, for deciding whether a change of the document changes the picture: the scene text with the camera, the picture settings, the title, the saved views and every
// comment (names, the builder's own JSON copy, which carries what is only for the views) left out. Two documents with the same signature render the same picture.
inline std::string sceneSignature(const scene_doc::Document& d) {
	scene_doc::Document copy = d;
	copy.camera = scene_doc::Camera{};
	copy.render = scene_doc::RenderSettings{};
	copy.title.clear();
	copy.cameraViews.clear();
	const std::string text = scene_doc::toPbrt(copy);
	std::string out;
	std::size_t at = text.find("WorldBegin");
	if (at == std::string::npos) at = 0;
	for (std::size_t start = at; start < text.size();) {
		std::size_t end = text.find('\n', start);
		if (end == std::string::npos) end = text.size();
		if (text[start] != '#') out.append(text, start, end - start + 1);
		start = end + 1;
	}
	return out;
}

}  // namespace scene_viewport
