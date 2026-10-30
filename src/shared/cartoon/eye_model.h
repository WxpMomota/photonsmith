#pragma once
// eye_model.h -- real eyes for the films' characters (`eyes real`), built as an eye is and as feature animation builds one: a white eyeball with a hole where the iris shows,
// an iris of radial fibres in three zones (a light collarette round the pupil, the iris's own colour, a dark limbal ring at its edge) set back inside the ball, a pupil
// in front of it as wide as the light and the feeling make it, and round them all a clear wet shell, the ball's surface with the cornea bulging out of it in front - glass
// (n 1.376) that bends the iris as the eye turns and reflects the scene's own lights and sky as catchlights, so no painted spark is needed. The shell casts no shadow
// (`"bool shadow" false`): a path tracer finds light that went through it only by chance, and the white and the iris inside it would be dark. Its gaps are kept wider than
// the renderers' ray offset (1 mm) for a person's eye; an eye under about 1.5 cm across (a small turtle's) is too small for them, and its iris is lost.
//
// Everything is in a unit eye's frame (radius 1, the gaze along +y, as alignY makes it) and placed by the caller's matrix: the eye's centre, its gaze and its radius.

#include <algorithm>
#include <cmath>
#include <string>

#include "cartoon_lib.h"

namespace cartoon {
namespace eye {

constexpr double kIris = 0.62;    // where the iris meets the white (the limbus), as a share of the ball's radius: a cartoon's large iris (a real one is about 0.5)
constexpr double kApex = 1.06;    // how far the cornea's top stands out of the ball
constexpr double kWhite = 0.94;   // the white ball's radius inside the clear shell (a gap of 0.06: 1.7 mm on a person's eye, more than a ray's offset)
constexpr double kIrisY = 0.72;   // the iris's plane, well behind the cornea (the anterior chamber: 0.34 of the radius to the apex, as in an eye)
constexpr double kEta = 1.376;    // the cornea's index of refraction
inline double limbusY() { return std::sqrt(1.0 - kIris * kIris); }
// How far over the ball the lids must lie to cover the cornea's bulge (the painted eyes' lids are 1.03).
constexpr double kLidOver = 1.09;

namespace detail {
inline void append(Mesh &into, const Mesh &m) {
	const int base = static_cast<int>(into.verts.size());
	into.verts.insert(into.verts.end(), m.verts.begin(), m.verts.end());
	into.normals.insert(into.normals.end(), m.normals.begin(), m.normals.end());
	into.uvs.insert(into.uvs.end(), m.uvs.begin(), m.uvs.end());
	for (const auto &t : m.tris) into.tris.push_back({t[0] + base, t[1] + base, t[2] + base});
}
// A ring of the iris from radius r0 to r1 at height y, its fibres standing up from it: radial ridges that wander a little, with a few crypts (pits) between them.
inline double fibre(double r, double phi) {
	const double ridges = 0.5 + 0.5 * std::sin(phi * 46.0 + 2.4 * std::sin(phi * 5.0 + r * 9.0) + 1.7 * std::sin(r * 23.0));
	const double crypts = std::max(0.0, std::sin(phi * 9.0 + 1.3) * std::sin(r * 31.0)) * 0.6;
	return 0.011 * smoothstep(0.20, 0.30, r) * (0.35 + 0.65 * ridges - crypts);
}
inline Mesh irisRing(double r0, double r1, double y, int nr, int nphi, double relief = 1.0) {
	Mesh m;
	const auto at = [&](double r, double phi) { return Vec3{r * std::cos(phi), y + relief * fibre(r, phi), r * std::sin(phi)}; };
	for (int j = 0; j <= nphi; ++j) {
		const double phi = 2.0 * kPi * j / nphi;
		for (int i = 0; i <= nr; ++i) {
			const double r = r0 + (r1 - r0) * i / nr, e = 1e-4;
			const Vec3 p = at(r, phi);
			const Vec3 dr = at(r + e, phi) - at(std::max(0.0, r - e), phi), dphi = at(r, phi + e) - at(r, phi - e);
			Vec3 n = normalize(cross(dphi, dr));
			if (n.y < 0.0) n = n * -1.0;
			m.verts.push_back(p);
			m.normals.push_back(length(n) > 0.5 ? n : Vec3{0.0, 1.0, 0.0});
			m.uvs.push_back({static_cast<double>(j) / nphi, static_cast<double>(i) / nr});
		}
	}
	for (int j = 0; j < nphi; ++j)
		for (int i = 0; i < nr; ++i) {
			const int a = j * (nr + 1) + i, b = (j + 1) * (nr + 1) + i;
			m.tris.push_back({a, a + 1, b});   // (facing +y)
			m.tris.push_back({b, a + 1, b + 1});
		}
	return m;
}
}  // namespace detail

// The parts of a unit eye, made once.
struct Parts {
	Mesh white, cornea, collarette, iris, limbal, pupil;
};
inline const Parts &parts() {
	static const Parts p = [] {
		Parts q;
		const double y0 = limbusY(), yi = kIrisY, edge = std::sqrt(kWhite * kWhite - yi * yi);   // (the iris meets the white inside the clear shell)
		{   // the white: a ball a little inside the clear shell, from its back up to where the iris meets it
			std::vector<std::pair<double, double>> profile;
			const double top = std::asin(yi / kWhite);
			for (int i = 0; i <= 20; ++i) {
				const double th = -kPi / 2.0 + (top + kPi / 2.0) * i / 20.0;
				profile.push_back({kWhite * std::cos(th), kWhite * std::sin(th)});
			}
			profile.front().first = 0.0;
			q.white = lathe(profile, 40);
			for (std::size_t v = 0; v < q.white.verts.size(); ++v) q.white.normals[v] = normalize(q.white.verts[v]);   // (exact)
		}
		{   // the clear shell: the ball's wet surface up to the limbus, then the cornea - a cap of a sphere through the limbus and the apex
			const double yc = (kApex * kApex - kIris * kIris - y0 * y0) / (2.0 * (kApex - y0)), rc = kApex - yc;
			std::vector<std::pair<double, double>> profile;
			const double top = std::asin(y0);
			for (int i = 0; i <= 18; ++i) {
				const double th = -kPi / 2.0 + (top + kPi / 2.0) * i / 18.0;
				profile.push_back({std::cos(th), std::sin(th)});
			}
			profile.front().first = 0.0;
			const double a0 = std::atan2(y0 - yc, kIris);
			for (int i = 1; i <= 14; ++i) {
				const double a = a0 + (kPi / 2.0 - a0) * i / 14.0;
				profile.push_back({rc * std::cos(a), yc + rc * std::sin(a)});
			}
			profile.back().first = 0.0;
			q.cornea = lathe(profile, 48);
			for (std::size_t v = 0; v < q.cornea.verts.size(); ++v) {   // (exact normals: the ball's below the limbus, the cornea's above)
				const Vec3 &x = q.cornea.verts[v];
				q.cornea.normals[v] = x.y > y0 + 1e-9 ? normalize(x - Vec3{0.0, yc, 0.0}) : normalize(x);
			}
		}
		q.collarette = detail::irisRing(0.0, 0.42 * edge, yi, 10, 120);
		q.iris = detail::irisRing(0.42 * edge, 0.86 * edge, yi, 10, 160);
		q.limbal = detail::irisRing(0.86 * edge, edge, yi, 4, 160, 0.25);   // (nearly flat: lit fibre tips on the dark ring look like a comb)
		q.pupil = lathe({{1.0, yi + 0.016}, {0.5, yi + 0.016}, {0.0, yi + 0.016}}, 40);   // (a unit disc in front of the fibres facing out of the eye, scaled to the pupil)
		return q;
	}();
	return p;
}

// How wide the pupil is, as a share of the iris (0.22 in bright light .. 0.58 in the dark): `dark` 0 a sunny day .. 1 night, `feeling` -1 (anger narrows it) .. 1 (fear,
// surprise and delight open it).
inline double pupilOf(double dark, double feeling) { return std::clamp(0.30 + 0.24 * std::clamp(dark, 0.0, 1.0) + 0.08 * std::clamp(feeling, -1.0, 1.0), 0.22, 0.58); }

// A pair (or more) of eyes being gathered into one text, a mesh per material.
struct Eyes {
	std::string white, cornea, collarette, iris, limbal;
	// An eye: `place` is the unit eye's frame in the world (its centre, its gaze along +y, scaled by its radius); `pupil` its pupil (pupilOf).
	void add(const Mat4 &place, double pupil) {
		const Parts &P = parts();
		white += emitMesh(P.white, place);
		cornea += emitMesh(P.cornea, place);
		collarette += emitMesh(P.collarette, place);
		iris += emitMesh(P.iris, place);
		limbal += emitMesh(P.limbal, place);
		const double r = std::clamp(pupil, 0.1, 0.7) * std::sqrt(kWhite * kWhite - kIrisY * kIrisY);
		pupil_ += emitMesh(P.pupil, mul(place, scale(r, 1.0, r)));
	}
	// The scene text: `colour` the iris's, `whiteColour` the ball's.
	std::string text(Vec3 colour, Vec3 whiteColour = {0.93, 0.91, 0.88}) const {
		if (white.empty()) return "";
		std::string out;
		out += attr(coated(whiteColour, 0.18), noShadow(white));   // (nor does the white: the iris sits deep in it, and a light from the side would leave half of it dark)
		const Vec3 c = Vec3{std::min(0.9, colour.x * 1.4), std::min(0.9, colour.y * 1.4), std::min(0.9, colour.z * 1.4)};   // (lit only through the cornea: a little brighter)
		out += attr(diffuseMaterial(lerp(c, Vec3{0.80, 0.62, 0.30}, 0.35) * 1.25), collarette);   // (a warm ring round the pupil)
		out += attr(diffuseMaterial(c), iris);
		out += attr(diffuseMaterial(c * 0.32), limbal);
		out += attr(diffuseMaterial({0.008, 0.007, 0.009}), pupil_);
		const std::string clear = noShadow(cornea);   // (see the top)
		char glass[120];
		std::snprintf(glass, sizeof glass, "Material \"dielectric\" \"float eta\" [ %.4f ] \"float roughness\" [ 0 ]", kEta);
		out += attr(glass, clear);
		return out;
	}

private:
	std::string pupil_;
	static std::string noShadow(std::string shapes) {
		for (std::size_t at = 0; (at = shapes.find("Shape \"trianglemesh\"", at)) != std::string::npos; at += 20) shapes.insert(at + 20, " \"bool shadow\" false");
		return shapes;
	}
};

}  // namespace eye
}  // namespace cartoon
