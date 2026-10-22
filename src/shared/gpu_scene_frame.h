#pragma once
// gpu_scene_frame.h -- the frame a GPU backend places a loaded pbrt scene in: how much to scale it, where its centre is, and where that centre is put.
//
// The Metal shaders were tuned for a room about 2 units across (their ray-offset epsilons are fixed numbers), but a pbrt scene is authored at any scale (a classic Cornell
// box spans about 555 units). So the Metal loader rescales the whole scene once, so that its largest dimension is 2, recentres it on its bounding box, and moves it well clear
// of the hardcoded room (+60 in X). Everything the loader places (triangles, spheres, lights, media, the camera) goes through the same transform, and Live Preview converts the
// GUI's camera and first-hit positions with it - which is why the rule returns all three numbers, not only the scale. Moved here from MetalPocApp::loadPbrtScene()
// (gpu/metal/host/metal_poc_pbrt_loader.mm) with the arithmetic unchanged (single precision, same order), so a seeded Metal render is byte-identical.
//
// The bounding box covers the triangles and the spheres. A huge ground-plane-as-a-sphere (the `Translate 0 -1000 0` + radius 1000 idiom) would set the extent to about 2000
// and shrink every real object to the size of the shaders' fixed epsilons, so a sphere whose diameter is at least 60% of the full extent is left out of the box when that
// shrinks the extent more than 4x. The sphere is still loaded; it just does not set the scale.
//
// The same goes for a few far-off triangles: a backdrop wall of a dozen triangles 600 units across (the cartoon films have one) behind a stage 34 across and a lamp half a unit tall would
// make the lamp smaller than the shaders' shadow-ray offsets (a point light inside a bulb lit the whole stage). When the extent of the triangles without the outermost 0.5% on each side
// (by centroid, per axis; at least 1000 triangles) is less than a quarter of the full one, that trimmed box sets the scale. Those triangles are still loaded.
//
// OptiX does not rescale (its epsilons are relative), so it does not use this; the rule lives here, in shared code, so that is a choice a backend makes, not a copy of the code.
// Standard library only.

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

#include "pbrt_flatten.h"

namespace gpu_scene_frame {

struct Frame {
	float scale = 1.0f;       // multiply positions (after recentring) by this
	float centre[3] = {0, 0, 0};   // the bounding box's centre, in the scene's own units
	float offset[3] = {60.0f, 0.0f, 0.0f};   // where the centre ends up (the hardcoded room occupies about [-1, 1])
	float extent = 0.0f;       // the largest dimension of the bounding box that set the scale
	float fullExtent = 0.0f;   // the same, with every sphere counted
	bool ignoredGiantSphere = false;   // true when a huge ground-like sphere was left out of the box
	bool ignoredOutliers = false;      // true when the outermost triangles were left out of the box (see above)

	// A point of the scene in the backend's world: (p - centre) * scale + offset.
	void toWorld(float x, float y, float z, float out[3]) const {
		out[0] = (x - centre[0]) * scale + offset[0];
		out[1] = (y - centre[1]) * scale + offset[1];
		out[2] = (z - centre[2]) * scale + offset[2];
	}
	// The inverse, for a position the backend hands back (Live Preview's first-hit positions).
	void fromWorld(float x, float y, float z, float out[3]) const {
		out[0] = (x - offset[0]) / scale + centre[0];
		out[1] = (y - offset[1]) / scale + centre[1];
		out[2] = (z - offset[2]) / scale + centre[2];
	}
};

namespace frame_detail {

struct Box {
	float lo[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
	float hi[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
	void grow(float x, float y, float z) {
		const float p[3] = {x, y, z};
		for (int a = 0; a < 3; ++a) {
			lo[a] = std::min(lo[a], p[a]);
			hi[a] = std::max(hi[a], p[a]);
		}
	}
	float maxExtent() const { return std::fmax(hi[0] - lo[0], std::fmax(hi[1] - lo[1], hi[2] - lo[2])); }
};

}  // namespace frame_detail

// The frame for `scene` (call it after any tessellation, so those triangles count). A degenerate or empty scene gets scale 1.
inline Frame computeSceneFrame(const pbrt_flatten::FlatScene& scene) {
	frame_detail::Box box;
	for (const pbrt_flatten::Triangle& t : scene.triangles)
		for (int c = 0; c < 3; ++c) box.grow((float)t.v[c * 3 + 0], (float)t.v[c * 3 + 1], (float)t.v[c * 3 + 2]);
	for (const pbrt_flatten::Sphere& s : scene.spheres) {
		const float r = (float)s.radius;
		const float cx = (float)s.center[0], cy = (float)s.center[1], cz = (float)s.center[2];
		box.grow(cx - r, cy - r, cz - r);
		box.grow(cx + r, cy + r, cz + r);
	}
	Frame frame;
	frame.fullExtent = box.maxExtent();
	{
		const float fullMax = frame.fullExtent;
		frame_detail::Box core;
		bool anyCore = false, droppedGiant = false;
		for (const pbrt_flatten::Triangle& t : scene.triangles)
			for (int c = 0; c < 3; ++c) {
				core.grow((float)t.v[c * 3 + 0], (float)t.v[c * 3 + 1], (float)t.v[c * 3 + 2]);
				anyCore = true;
			}
		for (const pbrt_flatten::Sphere& s : scene.spheres) {
			const float r = (float)s.radius;
			if (2.0f * r >= 0.6f * fullMax) { droppedGiant = true; continue; }
			const float cx = (float)s.center[0], cy = (float)s.center[1], cz = (float)s.center[2];
			core.grow(cx - r, cy - r, cz - r);
			core.grow(cx + r, cy + r, cz + r);
			anyCore = true;
		}
		if (droppedGiant && anyCore) {
			const float coreMaxExtent = core.maxExtent();
			if (coreMaxExtent > 1e-6f && coreMaxExtent < 0.25f * fullMax) {
				box = core;
				frame.ignoredGiantSphere = true;
			}
		}
	}
	if (!frame.ignoredGiantSphere && scene.triangles.size() >= 1000) {
		// the trimmed box: per axis, the 0.5% and 99.5% quantiles of the triangle centroids, widened by what a triangle at the edge reaches (its own bounding box), plus every sphere
		const size_t n = scene.triangles.size();
		float lo[3], hi[3];
		std::vector<float> c(n);
		for (int a = 0; a < 3; ++a) {
			for (size_t i = 0; i < n; ++i) {
				const pbrt_flatten::Triangle& t = scene.triangles[i];
				c[i] = (float)((t.v[a] + t.v[3 + a] + t.v[6 + a]) / 3.0);
			}
			const size_t k = n / 200;
			std::nth_element(c.begin(), c.begin() + k, c.end());
			lo[a] = c[k];
			std::nth_element(c.begin(), c.begin() + (n - 1 - k), c.end());
			hi[a] = c[n - 1 - k];
		}
		frame_detail::Box trimmed;
		for (const pbrt_flatten::Triangle& t : scene.triangles) {
			bool inside = true;
			for (int a = 0; a < 3; ++a) {
				const float m = (float)((t.v[a] + t.v[3 + a] + t.v[6 + a]) / 3.0);
				if (m < lo[a] || m > hi[a]) inside = false;
			}
			if (!inside) continue;
			for (int k = 0; k < 3; ++k) trimmed.grow((float)t.v[k * 3 + 0], (float)t.v[k * 3 + 1], (float)t.v[k * 3 + 2]);
		}
		for (const pbrt_flatten::Sphere& s : scene.spheres) {
			const float r = (float)s.radius;
			const float cx = (float)s.center[0], cy = (float)s.center[1], cz = (float)s.center[2];
			trimmed.grow(cx - r, cy - r, cz - r);
			trimmed.grow(cx + r, cy + r, cz + r);
		}
		const float trimmedExtent = trimmed.maxExtent();
		if (trimmedExtent > 1e-6f && trimmedExtent < 0.25f * frame.fullExtent) {
			box = trimmed;
			frame.ignoredOutliers = true;
		}
	}
	frame.extent = box.maxExtent();
	// The largest dimension maps to 2.0 units; a degenerate or empty scene is left at scale 1 rather than dividing by about 0.
	frame.scale = (frame.extent > 1e-6f) ? (2.0f / frame.extent) : 1.0f;
	for (int a = 0; a < 3; ++a) frame.centre[a] = 0.5f * (box.lo[a] + box.hi[a]);
	return frame;
}

}  // namespace gpu_scene_frame
