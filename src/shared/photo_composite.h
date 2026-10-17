#pragma once
// photo_composite.h -- "Render into the photo": a scene rendered through a camera matched to a photo, put into that photo with its shadows.
//
// A shadow catcher without a line of renderer code, by the difference method. The scene is rendered three times from the same camera:
//   * FULL: the scene as it is;
//   * GROUND: the same with the inserted objects taken away, leaving the ground they stand on (and the lights): what the photo's ground would look like with nothing on it;
//   * MASK: the inserted objects as plain white against black (the ground black too, so it hides what is under it): how much of each pixel they cover.
// The photo's own ground is not drawn. What the inserted objects do to it is the ratio FULL / GROUND (a shadow darkens it, the glow of a bright object or its reflection lightens it), which
// multiplies the photo, in linear light. The objects are pasted in where the mask says, un-matted first (an edge pixel of the render is part object, part ground: the ground's share is taken
// off before the object is blended over the photo again, or the edges would carry a halo of the render's ground).
//
// Two details that decide how it looks. The render has noise, and where nothing changes the ratio is 1 only on average: a soft dead-band round 1 and a small smoothing keep that noise out of
// the photo (a real shadow is far stronger than either). And the object is shown the way the renderer shows any picture, ACES filmic then the sRGB curve (measured against the renderer's own PNG),
// with an exposure control, since the photo's exposure and the scene's light are not the same thing.
//
// Header-only, std-only (plus scene_document.h for the three scene variants); shared by the Scene Builder (qt_gui/scene_builder_composite.cpp) and its tests.

#include "scene_document.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace photo_composite {

struct Options {
	double exposureStops = 0.0;     // the inserted objects' exposure: +1 doubles their light before it is tone mapped
	double shadowStrength = 1.0;    // 1: the scene's shadows as rendered; 0: no shadows at all; above 1: darker ones
	int smoothRadius = 2;           // the ratio is averaged over (2 r + 1)^2 pixels (not across the objects): render noise
	double deadBand = 0.04;         // a ratio within this (as a natural log) of 1 is taken to be 1
};

namespace detail {

inline double decode(double s) { return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4); }
inline double encode(double l) {
	l = std::min(1.0, std::max(0.0, l));
	return l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055;
}
// The renderer's tone curve (ACES filmic, Narkowicz's fit): scene radiance -> display-linear.
inline double aces(double x) {
	x = std::max(0.0, x);
	return std::min(1.0, std::max(0.0, (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14)));
}
inline double luminance(const float* c) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; }

}  // namespace detail

// The photo (`photo`: w x h x 3, 8-bit sRGB) with the render put in. `with` and `ground` are w x h x 3 linear radiance (the FULL and GROUND renders), `mask` w x h coverage 0..1. Returns w x h x 3
// 8-bit sRGB, empty for a null input or a size below 1.
inline std::vector<unsigned char> composite(const unsigned char* photo, const float* with, const float* ground, const float* mask, int w, int h, const Options& options = Options()) {
	if (!photo || !with || !ground || !mask || w < 1 || h < 1) return {};
	const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
	// the log of what the objects do to the ground's light, where the objects are not (under them the ratio means nothing)
	std::vector<float> logRatio(n, 0.0f);
	std::vector<char> valid(n, 0);
	for (size_t i = 0; i < n; ++i) {
		if (mask[i] > 0.02f) continue;
		const double lw = detail::luminance(&with[i * 3]), lg = detail::luminance(&ground[i * 3]);
		if (!(lw >= 0.0) || !(lg >= 0.0) || !std::isfinite(lw) || !std::isfinite(lg)) continue;
		logRatio[i] = static_cast<float>(std::log((lw + 1e-4) / (lg + 1e-4)));
		valid[i] = 1;
	}
	const int r = std::max(0, options.smoothRadius);
	std::vector<float> smooth(n, 0.0f);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			double sum = 0.0;
			int count = 0;
			for (int dy = -r; dy <= r; ++dy)
				for (int dx = -r; dx <= r; ++dx) {
					const int xx = x + dx, yy = y + dy;
					if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
					const size_t j = static_cast<size_t>(yy) * w + xx;
					if (valid[j]) { sum += logRatio[j]; ++count; }
				}
			smooth[static_cast<size_t>(y) * w + x] = count > 0 ? static_cast<float>(sum / count) : 0.0f;
		}
	const double exposure = std::exp2(options.exposureStops);
	const double lo = std::log(0.02), hi = std::log(8.0);
	std::vector<unsigned char> out(n * 3);
	for (size_t i = 0; i < n; ++i) {
		double lr = std::min(hi, std::max(lo, static_cast<double>(smooth[i])));
		lr = lr > 0.0 ? std::max(0.0, lr - options.deadBand) : std::min(0.0, lr + options.deadBand);   // (a soft dead-band: nothing within it, the rest less by it)
		const double ratio = std::exp(options.shadowStrength * lr);
		const double m = std::min(1.0, std::max(0.0, static_cast<double>(mask[i])));
		for (int c = 0; c < 3; ++c) {
			const double background = detail::encode(detail::decode(photo[i * 3 + static_cast<size_t>(c)] / 255.0) * ratio);
			double value = background;
			if (m > 0.001) {
				// un-matted: the render's pixel is m of the object and (1 - m) of the ground
				const double objectLinear = std::max(0.0, (with[i * 3 + static_cast<size_t>(c)] - (1.0 - m) * ground[i * 3 + static_cast<size_t>(c)]) / std::max(m, 0.05)) * exposure;
				const double object = detail::encode(detail::aces(std::isfinite(objectLinear) ? objectLinear : 0.0));
				value = m * object + (1.0 - m) * background;
			}
			out[i * 3 + static_cast<size_t>(c)] = static_cast<unsigned char>(std::min(1.0, std::max(0.0, value)) * 255.0 + 0.5);
		}
	}
	return out;
}

// ---- the three scenes --------------------------------------------------------------------------------------------------------------------------------------------------------------------

// What each object of a scene is for the composite: the GROUND the photo already has (drawn by nobody: only the shadows it catches count), a LIGHT (an emissive object: it lights the scene and stays
// in the FULL and GROUND renders, and is not pasted), or INSERTED (everything else: pasted into the photo).
enum class Role { Ground, Light, Inserted };

// The scenes the three renders use. `roles` has one entry per object of `scene`.
//   ground: the scene without its inserted objects;
//   mask:   the inserted objects white and self-lit, the ground black (it hides what is under it), no lights, no sky: black everywhere else.
struct Passes {
	scene_doc::Document ground;
	scene_doc::Document mask;
};

inline Passes makePasses(const scene_doc::Document& scene, const std::vector<Role>& roles) {
	Passes p;
	p.ground = scene;
	p.mask = scene;
	p.ground.objects.clear();
	p.mask.objects.clear();
	p.mask.lights.clear();
	for (size_t i = 0; i < scene.objects.size(); ++i) {
		const Role role = i < roles.size() ? roles[i] : Role::Inserted;
		const scene_doc::Object& o = scene.objects[i];
		if (role != Role::Inserted) p.ground.objects.push_back(o);   // (the ground and the lights are what the shadows fall on and come from)
		if (role == Role::Light) continue;                          // (a lamp is not in the mask)
		scene_doc::Object m = o;
		m.material = scene_doc::Material();
		m.material.kind = scene_doc::MaterialKind::Diffuse;
		m.material.color = {0.0, 0.0, 0.0};
		m.emissive = role == Role::Inserted;
		m.emission = {1.0, 1.0, 1.0};
		m.emissionStrength = 1.0;
		m.emissionImage.clear();
		p.mask.objects.push_back(m);
	}
	return p;
}

// A reasonable first guess at what each object is: an emissive one is a light; one called Floor or Ground, or a big flat quad lying at the bottom of the scene, is the ground; the rest is inserted.
inline std::vector<Role> guessRoles(const scene_doc::Document& scene) {
	std::vector<Role> roles;
	for (const scene_doc::Object& o : scene.objects) {
		std::string name = o.name;
		std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		const bool named = name.rfind("floor", 0) == 0 || name.rfind("ground", 0) == 0;
		const bool bigFlatQuad = o.shape == scene_doc::ShapeKind::Quad && o.position.y < 0.05 && std::max(o.size.x, o.size.z) >= 5.0 && std::fabs(o.rotation.x) < 1e-6 && std::fabs(o.rotation.z) < 1e-6;
		roles.push_back(o.emissive ? Role::Light : (named || bigFlatQuad) ? Role::Ground : Role::Inserted);
	}
	return roles;
}

}  // namespace photo_composite
