#pragma once
// scene_camera_views.h -- saved cameras for the Scene Builder: name where the camera is, and come back to it later (a front view, a close-up, a bird's-eye
// shot), like Blender's saved camera markers. A view is a whole camera (position, what it looks at, field of view, lens radius, focus distance), kept in the
// document's JSON copy and nowhere else: the .pbrt text has just the scene's one camera.
//
// Every function changes the Document it is given and does nothing else (no selection, no undo): the GUI wraps one call in one undo step. std-only, like
// scene_document.h.

#include "camera_keyframes.h"
#include "scene_document.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace scene_doc {

constexpr std::size_t kMaxCameraViews = 100;

namespace camera_views_detail {

inline std::string trimmed(const std::string& s) {
	const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
	std::size_t a = 0, b = s.size();
	while (a < b && isSpace(static_cast<unsigned char>(s[a]))) ++a;
	while (b > a && isSpace(static_cast<unsigned char>(s[b - 1]))) --b;
	return s.substr(a, b - a);
}

inline bool nameTaken(const Document& d, const std::string& name, int except) {
	for (std::size_t i = 0; i < d.cameraViews.size(); ++i)
		if (static_cast<int>(i) != except && d.cameraViews[i].name == name) return true;
	return false;
}

}  // namespace camera_views_detail

// `wanted` (or "View" if it is blank) when no other view has that name, else "View 2", "View 3"... `except` is a view being renamed (its own name does not count).
inline std::string uniqueViewName(const Document& d, const std::string& wanted, int except = -1) {
	std::string base = camera_views_detail::trimmed(wanted);
	if (base.empty()) base = "View";
	if (!camera_views_detail::nameTaken(d, base, except)) return base;
	// "Front 2" asks for the next free number after "Front", not "Front 2 2".
	std::string stem = base;
	std::size_t end = stem.size();
	while (end > 0 && std::isdigit(static_cast<unsigned char>(stem[end - 1]))) --end;
	if (end < stem.size() && end > 1 && stem[end - 1] == ' ') stem.erase(end - 1);
	for (int n = 2;; ++n) {
		const std::string candidate = stem + " " + std::to_string(n);
		if (!camera_views_detail::nameTaken(d, candidate, except)) return candidate;
	}
}

// Saves `camera` as a new view named from `wanted`; returns its index, or -1 when there are already kMaxCameraViews.
inline int saveCameraView(Document& d, const std::string& wanted, const Camera& camera) {
	if (d.cameraViews.size() >= kMaxCameraViews) return -1;
	d.cameraViews.push_back(CameraView{uniqueViewName(d, wanted), camera});
	return static_cast<int>(d.cameraViews.size()) - 1;
}

// The scene's camera becomes the saved one. False for a bad index.
inline bool applyCameraView(Document& d, int index) {
	if (index < 0 || index >= static_cast<int>(d.cameraViews.size())) return false;
	d.camera = d.cameraViews[static_cast<std::size_t>(index)].camera;
	return true;
}

// The saved view takes `camera` (its name stays). False for a bad index.
inline bool updateCameraView(Document& d, int index, const Camera& camera) {
	if (index < 0 || index >= static_cast<int>(d.cameraViews.size())) return false;
	d.cameraViews[static_cast<std::size_t>(index)].camera = camera;
	return true;
}

// How long a flythrough stays at the view, in seconds (0 to 3600; anything else is refused). False for a bad index.
inline bool setCameraViewHold(Document& d, int index, double seconds) {
	if (index < 0 || index >= static_cast<int>(d.cameraViews.size()) || !(seconds >= 0.0 && seconds <= 3600.0)) return false;
	d.cameraViews[static_cast<std::size_t>(index)].hold = seconds;
	return true;
}

inline bool removeCameraView(Document& d, int index) {
	if (index < 0 || index >= static_cast<int>(d.cameraViews.size())) return false;
	d.cameraViews.erase(d.cameraViews.begin() + index);
	return true;
}

// Renames a view; the name is trimmed, and numbered if another view has it. A blank name is refused (false), and so is a bad index.
inline bool renameCameraView(Document& d, int index, const std::string& wanted) {
	if (index < 0 || index >= static_cast<int>(d.cameraViews.size()) || camera_views_detail::trimmed(wanted).empty()) return false;
	d.cameraViews[static_cast<std::size_t>(index)].name = uniqueViewName(d, wanted, index);
	return true;
}

// `base` moved to look from `eye` at `target`: the lens, field of view and up stay. The focus distance follows what is looked at while the camera has no depth of
// field (lens radius 0) and is left alone otherwise (the user set it on purpose).
inline Camera cameraLookingFrom(const Camera& base, const Float3& eye, const Float3& target) {
	Camera c = base;
	c.position = eye;
	c.target = target;
	if (c.lensRadius <= 0.0) {
		const double dx = target.x - eye.x, dy = target.y - eye.y, dz = target.z - eye.z;
		const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
		if (dist > 1e-6) c.focusDistance = dist;
	}
	return c;
}

// The flythrough's camera path: the saved views in the order they were saved (and the first again at the end, if asked), each with its own field of view, lens and hold. The
// Scene Builder's flythrough video and its preview in the 3D view both come from here, so what is drawn is what is rendered. Empty with fewer than two views.
inline camera_keyframes::Path flythroughPath(const Document& d, bool ease = true, bool returnToStart = false) {
	camera_keyframes::Path path;
	path.ease = ease;
	if (d.cameraViews.size() < 2) return path;
	for (const CameraView& v : d.cameraViews) {
		camera_keyframes::Key k;
		k.pos[0] = v.camera.position.x; k.pos[1] = v.camera.position.y; k.pos[2] = v.camera.position.z;
		k.target[0] = v.camera.target.x; k.target[1] = v.camera.target.y; k.target[2] = v.camera.target.z;
		k.fov = v.camera.fov;
		k.lensRadius = v.camera.lensRadius;
		k.focus = v.camera.focusDistance;
		k.hold = v.hold;
		path.keys.push_back(k);
	}
	if (returnToStart) path.keys.push_back(path.keys.front());
	return path;
}

// Points along the curve the camera follows (position and target), `stepsPerStretch` between one key and the next, starting at the first key and ending exactly on the last. The
// same Catmull-Rom curve the renderer uses, though not at its pace: this is the shape of the path, to draw. Empty with fewer than two keys.
inline std::vector<camera_keyframes::Key> flythroughCurve(const camera_keyframes::Path& path, int stepsPerStretch = 16) {
	std::vector<camera_keyframes::Key> out;
	const std::size_t n = path.keys.size();
	if (n < 2) return out;
	const int steps = std::max(1, stepsPerStretch);
	for (std::size_t seg = 0; seg + 1 < n; ++seg)
		for (int s = 0; s < steps; ++s) out.push_back(camera_keyframes::splineAt(path.keys, seg, static_cast<double>(s) / steps));
	out.push_back(path.keys.back());
	return out;
}

// Moves a saved view's eye (where the camera stands) or its target (what it looks at) to `where`, the way dragging its marker in the 3D view does: the lens, field of view and up
// stay, and the focus distance follows (cameraLookingFrom). False for a bad index or a point that is not finite; the view is unchanged then.
inline bool moveCameraViewEye(Document& d, int index, const Float3& where) {
	if (index < 0 || index >= static_cast<int>(d.cameraViews.size()) || !std::isfinite(where.x) || !std::isfinite(where.y) || !std::isfinite(where.z)) return false;
	Camera& c = d.cameraViews[static_cast<std::size_t>(index)].camera;
	c = cameraLookingFrom(c, where, c.target);
	return true;
}

inline bool moveCameraViewTarget(Document& d, int index, const Float3& where) {
	if (index < 0 || index >= static_cast<int>(d.cameraViews.size()) || !std::isfinite(where.x) || !std::isfinite(where.y) || !std::isfinite(where.z)) return false;
	Camera& c = d.cameraViews[static_cast<std::size_t>(index)].camera;
	c = cameraLookingFrom(c, c.position, where);
	return true;
}

// The camera `progress` (0 first view, 1 last) of the way through the flythrough, for the Scene Builder's scrub slider: the renderer's own camera_keyframes::at() (so the pace, the
// easing and the pauses are the video's), with the scene camera's other settings (up) and each view's field of view and lens interpolated. Pauses are seconds of the finished video,
// so they are measured against `nominalSeconds`. The scene's camera if there are fewer than two views.
inline Camera cameraAtFlythrough(const Document& d, double progress, bool ease = true, double nominalSeconds = 10.0) {
	if (d.cameraViews.size() < 2) return d.camera;
	const camera_keyframes::Key k = camera_keyframes::at(flythroughPath(d, ease), progress, nominalSeconds);
	Camera c = cameraLookingFrom(d.camera, Float3{k.pos[0], k.pos[1], k.pos[2]}, Float3{k.target[0], k.target[1], k.target[2]});
	if (k.fov > 0) c.fov = k.fov;
	if (k.hasLens()) {
		c.lensRadius = k.lensRadius;
		c.focusDistance = k.focus;
	}
	return c;
}

}  // namespace scene_doc
