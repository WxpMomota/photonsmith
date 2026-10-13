#pragma once
// scene_viewport.h -- the decisions behind the Scene Builder's rendered viewport (the 3D view showing a progressive path-traced picture of the scene you are editing): what a change
// of the view or of the document means for the renderer, and how big to draw. Qt-free and std-only, so the unit tests and the GUI use the same code.
//
// The viewport is a Live Preview session (a GPU renderer that refines one picture frame after frame) running on a copy of the scene written from the document. Three things can
// happen, and they cost very different amounts:
//   * the view's eye or target moved (orbit, pan, zoom): tell the renderer the new camera; it keeps what it can of the picture (cheap);
//   * the picture's size or field of view changed (the window was resized, a different lens): the renderer starts the picture again from the same scene (a restart);
//   * the document changed so that the scene is different: write the copy again and start again (a restart, after the edits have paused);
//   * the document changed only by moving objects (a drag): tell the renderer each object's offset from where the copy has it (translationOnly()); it keeps the copy and redraws.
// An edit that does not change the scene (a saved view, hiding something in the views, a name, the picture size for rendering) must not restart anything: sceneSignature() is what
// is compared to know.

#include "scene_document.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace scene_viewport {

struct Pose {
	double eye[3] = {0, 0, 0};
	double target[3] = {0, 0, -1};
	double fovDeg = 40.0;    // vertical
	int width = 0, height = 0;   // the picture being rendered, in pixels
	double lensRadius = 0.0;     // depth of field: 0 is a pinhole (everything sharp); otherwise the lens radius and the distance that is in focus
	double focusDistance = 0.0;
};

enum class Change { None, Camera, Restart };

// What the renderer must do to go from the pose it was last given to `now`.
inline Change classify(const Pose& sent, const Pose& now) {
	if (sent.width != now.width || sent.height != now.height || std::fabs(sent.fovDeg - now.fovDeg) > 1e-6) return Change::Restart;
	if (std::fabs(sent.lensRadius - now.lensRadius) > 1e-9 || (now.lensRadius > 0.0 && std::fabs(sent.focusDistance - now.focusDistance) > 1e-9)) return Change::Restart;   // (the lens is fixed when it starts)
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

// The document with everything that does not change the picture cleared: the camera and the picture settings (the viewport has its own), the title, the saved views, and the names, groups and
// hidden and locked flags (only the views use them).
inline scene_doc::Document pictureOf(const scene_doc::Document& d) {
	scene_doc::Document copy = d;
	copy.camera = scene_doc::Camera{};
	copy.render = scene_doc::RenderSettings{};
	copy.title.clear();
	copy.cameraViews.clear();
	for (scene_doc::Object& o : copy.objects) {
		o.name.clear();
		o.group.clear();
		o.hidden = false;
		o.locked = false;
	}
	for (scene_doc::Light& l : copy.lights) {
		l.name.clear();
		l.hidden = false;
		l.locked = false;
	}
	return copy;
}

// What the scene is, for deciding whether a change of the document changes the picture: pictureOf() as JSON. Two documents with the same signature render the same picture. (It is the
// JSON and not the pbrt text because that holds the parameters of every shape and not the thousands of numbers of a terrain or a rounded box: it is worked out for every step of a drag.)
inline std::string sceneSignature(const scene_doc::Document& d) { return scene_doc::toJson(pictureOf(d)); }

// Whether `now` differs from `base` (the document the renderer's copy was written from, `baseSignature` its sceneSignature(), which the caller keeps so that it is not worked out again for
// every step of a drag) only in where objects are, and by how much: the offset of each object (absolute from where `base` has it, so all zeros when nothing moved).
// The renderer numbers objects as the copy's AttributeBegin blocks in file order, which are the document's objects in order, so an offset's index is the object's, except that the
// placements of instances (scene_instancing.h) come after every plain object: the loader lists the shapes first and the instances after them (FlatScene::shapeRanges). The offsets
// are given in that order: the plain objects in document order, then the instanced ones in document order.
// Nothing is returned (an empty vector) when the change is more than a move, or for a scene where the numbering cannot be trusted (a mesh, whose file may not load, leaves a
// different number of objects than the document has), or when a volume is moved (the medium of a cloud stays where it was declared). Those restart instead.
inline std::vector<std::array<double, 3>> translationOnly(const scene_doc::Document& base, const std::string& baseSignature, const scene_doc::Document& now) {
	if (base.objects.size() != now.objects.size() || base.objects.empty()) return {};
	for (const scene_doc::Object& o : now.objects)
		if (o.shape == scene_doc::ShapeKind::Mesh) return {};
	scene_doc::Document unmoved = now;
	std::vector<std::array<double, 3>> offsets(now.objects.size());
	for (std::size_t i = 0; i < now.objects.size(); ++i) {
		const scene_doc::Float3& a = base.objects[i].position;
		const scene_doc::Float3& b = now.objects[i].position;
		offsets[i] = {b.x - a.x, b.y - a.y, b.z - a.z};
		unmoved.objects[i].position = a;
	}
	if (sceneSignature(unmoved) != baseSignature) return {};
	for (std::size_t i = 0; i < offsets.size(); ++i)   // (a volume is an object like another to the renderer, but its medium does not move with it: moving one is a restart, standing still is nothing)
		if (now.objects[i].volume.kind != scene_doc::VolumeKind::None && (offsets[i][0] != 0.0 || offsets[i][1] != 0.0 || offsets[i][2] != 0.0)) return {};
	const scene_doc::InstancePlan plan = scene_doc::planInstances(now);
	if (plan.templates() == 0) return offsets;
	std::vector<std::array<double, 3>> inRendererOrder;
	inRendererOrder.reserve(offsets.size());
	for (std::size_t i = 0; i < offsets.size(); ++i)
		if (plan.templateOf[i] < 0) inRendererOrder.push_back(offsets[i]);
	for (std::size_t i = 0; i < offsets.size(); ++i)
		if (plan.templateOf[i] >= 0) inRendererOrder.push_back(offsets[i]);
	return inRendererOrder;
}
inline std::vector<std::array<double, 3>> translationOnly(const scene_doc::Document& base, const scene_doc::Document& now) { return translationOnly(base, sceneSignature(base), now); }

}  // namespace scene_viewport
