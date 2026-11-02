#pragma once
// film_toonwater.h -- water drawn, not photographed (water round 10): in a `toon` film the sea, lake or river is flat colour in bands, as a painter would lay it, and what moves on it is drawn.
//
//   bands      deep, middle and shallow water in flat colours, stepped by the depth (`toonwater deep R G B mid R G B shallow R G B`); with no land under it, one colour
//   lines      white wave lines a stroke long, drifting on the water, each growing and fading in turn, finer far off
//   sparkles   four-pointed stars twinkling along the way to the sun
//   rings      a touch of the water is a drawn ring or two widening and thinning, and a splash is a white crown with fat drops, inked like anything else
//
// `toonwater off` keeps real water in a toon film. Coordinates: pbrt's in a film file; meshes in story space (x mirrored).

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_craft.h"
#include "film_sky.h"
#include "film_stir.h"
#include "film_terrain.h"
#include "film_water.h"
#include "nature_lib.h"

namespace cartoon {
namespace toonwater {

struct Paint {
	bool off = false;
	Vec3 deep{0.04, 0.27, 0.5}, mid{0.1, 0.46, 0.66}, shallow{0.42, 0.8, 0.8};
	bool coloured = false;   // (the film gave its own)
	double lines = 1.0, sparkle = 1.0;
};

// The colour of the water by its depth, in steps: the bands' picture over the terrain's square (u, v along x, z).
inline std::string bandsPicture(const std::string &film, const terrain::Field &g, double level, const Paint &p) {
	const int W = 768;
	char words[240];   // (its name: every number it is made from, the land's shape and all nine of the colours)
	std::snprintf(words, sizeof words, "|%d|%.4f|%.4f|%.4f|%.4f|%.4f|%.4f|%.4f|%.4f|%.4f|%.4f", g.n, level, p.deep.x, p.deep.y, p.deep.z, p.mid.x, p.mid.y, p.mid.z, p.shallow.x, p.shallow.y, p.shallow.z);
	return film_sky::picture(film, "toonsea2_" + film_cache::nameOf(g.spec.key() + words), W, W, [&](std::vector<float> &rgb) {
		for (int y = 0; y < W; ++y)
			for (int x = 0; x < W; ++x) {
				const double px = g.x0 + (x + 0.5) / W * (g.n - 1) * g.cell, pz = g.z0 + (W - 1 - y + 0.5) / W * (g.n - 1) * g.cell;
				const double d = level - g.heightAt(px, pz) + 0.25 * (nature::noise2(px * 0.1, pz * 0.1, 851) - 0.5);
				const Vec3 c = d < 0.7 ? p.shallow : d < 2.2 ? p.mid : p.deep;
				const std::size_t k = (static_cast<std::size_t>(y) * W + x) * 3;
				rgb[k] = static_cast<float>(c.x), rgb[k + 1] = static_cast<float>(c.y), rgb[k + 2] = static_cast<float>(c.z);
			}
	});
}

namespace detail {
inline void flatRibbon(Mesh &m, const std::vector<Vec3> &pts, double width) {
	const int base = static_cast<int>(m.verts.size());
	for (std::size_t i = 0; i < pts.size(); ++i) {
		const Vec3 a = pts[i > 0 ? i - 1 : i], b = pts[i + 1 < pts.size() ? i + 1 : i], d = normalize(b - a + Vec3{1e-9, 0.0, 0.0}), side{-d.z, 0.0, d.x};
		const double u = pts.size() > 1 ? i / double(pts.size() - 1) : 0.5, w = width * std::sin(kPi * std::clamp(u, 0.02, 0.98));
		m.verts.push_back(pts[i] - side * w), m.verts.push_back(pts[i] + side * w);
		m.normals.push_back({0.0, 1.0, 0.0}), m.normals.push_back({0.0, 1.0, 0.0});
	}
	for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
		const int a = base + 2 * static_cast<int>(i);
		m.tris.push_back({a, a + 1, a + 2}), m.tris.push_back({a + 1, a + 3, a + 2});
	}
}
inline void circle(Mesh &m, double x, double y, double z, double r, double width, int segs = 48) {
	const int base = static_cast<int>(m.verts.size());
	for (int i = 0; i <= segs; ++i) {
		const double a = 6.283185 * i / segs, wob = 1.0 + 0.02 * std::sin(5.0 * a + r);
		m.verts.push_back({-(x + (r * wob - width) * std::cos(a)), y, z + (r * wob - width) * std::sin(a)}), m.verts.push_back({-(x + (r * wob + width) * std::cos(a)), y, z + (r * wob + width) * std::sin(a)});
		m.normals.push_back({0.0, 1.0, 0.0}), m.normals.push_back({0.0, 1.0, 0.0});
	}
	for (int i = 0; i < segs; ++i) {
		const int a = base + 2 * i;
		m.tris.push_back({a, a + 1, a + 2}), m.tris.push_back({a + 1, a + 3, a + 2});
	}
}
}  // namespace detail

// The drawn things on the water at t: its wave lines and sparkles round the camera, its rings and its splashes. `sunFacing`: where the sun is (a facing, degrees), `sunUp` how high.
// `waterY`: the water's height at pbrt's (x, z) (a sea's is flat; a river's falls); the wave lines and sparkles are for open water (`sea`) alone: they are laid out everywhere round
// the camera, and over a valley with only a river they floated in the air.
template <class WaterY>
inline std::string text(const Paint &paint, const stir::Field &f, double t, const Vec3 &eye, const Vec3 &ahead, double hfov, double level, const water::Sea *sea, double sunFacing, double sunUp, WaterY waterY) {
	if (paint.off) return std::string();
	const double wide = std::cos(std::min(kPi, (0.5 * hfov + 12.0) * kPi / 180.0));
	const auto seen = [&](double x, double z) {
		const Vec3 to{x - eye.x, 0.0, z - eye.z};
		const double d = length(to);
		return d > 0.5 && dot(to * (1.0 / d), ahead) > wide;
	};
	Mesh lines;
	const double height = std::max(0.5, std::abs(eye.y - level));
	for (const double cell : {3.0 + 0.8 * height, 14.0 + 3.0 * height}) {
		if (!sea) break;
		const bool nearOnes = cell < 10.0 + 2.0 * height;
		const int reach = nearOnes ? 8 : 12, ix = static_cast<int>(std::floor(eye.x / cell)), iz = static_cast<int>(std::floor(eye.z / cell));
		for (int a = ix - reach; a <= ix + reach; ++a)
			for (int b = iz - reach; b <= iz + reach; ++b) {
				const int k = static_cast<int>((static_cast<unsigned>(a) * 73856093u ^ static_cast<unsigned>(b) * 19349663u) & 0x7fffffffu);
				if (hash01(k, 1) > 0.55 * paint.lines) continue;
				const double period = 4.0 + 3.0 * hash01(k, 2), phase = t / period + hash01(k, 3), u = phase - std::floor(phase), x = (a + hash01(k, 4)) * cell, z = (b + hash01(k, 5)) * cell;
				if (!seen(x, z)) continue;
				const double d = std::hypot(x - eye.x, z - eye.z);
				if (!nearOnes && d < 7.0 * (3.0 + 0.8 * height)) continue;
				const double L = (0.22 + 0.2 * hash01(k, 6)) * cell * std::sin(kPi * u);
				if (L < 0.02 * cell) continue;
				const Vec3 right = normalize(Vec3{-ahead.z, 0.0, ahead.x});
				std::vector<Vec3> pts;
				for (int i = 0; i <= 7; ++i) {
					const double v = i / 7.0 - 0.5;
					const Vec3 p = Vec3{x, 0.0, z} + right * (v * L * 2.0) + ahead * (0.06 * L * std::sin(v * 8.0 + 6.0 * hash01(k, 7)));
					pts.push_back({-p.x, (d < 70.0 ? waterY(p.x, p.z) : level) + 0.03 + 0.0015 * d, p.z});
				}
				detail::flatRibbon(lines, pts, std::max(0.03, 0.007 * d));
			}
	}
	Mesh stars;
	if (sea && paint.sparkle > 0.0 && sunUp > -2.0) {
		const double a = sunFacing * kPi / 180.0;
		const Vec3 toSun{std::sin(a), 0.0, std::cos(a)}, across{toSun.z, 0.0, -toSun.x};
		const int count = static_cast<int>(70.0 * paint.sparkle);
		for (int n = 0; n < count; ++n) {
			const double u = 0.02 + 0.98 * hash01(n, 1) * hash01(n, 2), period = 0.7 + 1.1 * hash01(n, 3), phase = t / period + hash01(n, 4), w = phase - std::floor(phase);
			const double d = height * (2.0 + 60.0 * u), off = d * 0.12 * (hash01(n * 3 + static_cast<int>(std::floor(phase)), 5) - 0.5) * 2.0;
			const Vec3 at = Vec3{eye.x, 0.0, eye.z} + toSun * d + across * off;
			if (!seen(at.x, at.z)) continue;
			const double s = (0.012 + 0.006 * d) * std::sin(kPi * w) * (0.5 + hash01(n, 6)), y = (d < 70.0 ? waterY(at.x, at.z) : level) + 0.05 + 0.0015 * d;
			const Vec3 c{-at.x, y, at.z}, up{0.0, s * 1.0, 0.0}, r = normalize(Vec3{-across.x, 0.0, across.z}) * s;   // (across the sun's way in story space, as `c` is: unmirrored, a star lay edge-on to the eye at any but a square facing)
			const int base = static_cast<int>(stars.verts.size());
			const double q = 0.14;
			for (const Vec3 &v : {c + up, c + (up + r) * q, c + r, c + (r - up) * q, c - up, c - (up + r) * q, c - r, c + (up - r) * q, c}) stars.verts.push_back(v), stars.normals.push_back(normalize(Vec3{-eye.x + at.x, eye.y - y, eye.z - at.z}));
			for (int e = 0; e < 8; ++e) stars.tris.push_back({base + 8, base + e, base + (e + 1) % 8});
		}
	}
	Mesh rings, crown;
	std::string drops;
	char buf[160];
	for (const stir::Ring &q : f.rings) {
		const double age = t - q.t0;
		if (age < 0.0) continue;
		const double y = waterY(q.x, q.z) + 0.018, grown = 0.45 * stir::reachOf(q, age);
		if (q.size > 0.022 && age < 2.0 * q.life && seen(q.x, q.z)) {
			const double thin = std::max(0.2, 1.0 - age / (2.0 * q.life)) * std::max(0.008, 0.13 * q.size);
			detail::circle(rings, q.x, y, q.z, grown, thin);
			if (age > 0.4) detail::circle(rings, q.x, y, q.z, 0.55 * grown, 0.7 * thin, 36);
		}
		if (q.splash && age < 0.9) {   // a white crown round the place, fat drops off it
			const double up = 2.4 * q.size * std::sin(kPi * std::min(1.0, age / 0.65)), r = 1.7 * q.size * (1.0 + 2.0 * age);
			const int spikes = 11;
			for (int s = 0; s < spikes && up > 0.01; ++s) {
				const double a = 6.283 * s / spikes + 0.4 * hash01(s, static_cast<int>(q.t0 * 10.0)), a1 = a - 0.25, a2 = a + 0.25, tall = up * (0.7 + 0.6 * hash01(s + 3, static_cast<int>(q.x * 10.0)));
				const int base = static_cast<int>(crown.verts.size());
				const Vec3 p1{-(q.x + r * std::cos(a1)), y, q.z + r * std::sin(a1)}, p2{-(q.x + r * std::cos(a2)), y, q.z + r * std::sin(a2)}, tip{-(q.x + 1.3 * r * std::cos(a)), y + tall, q.z + 1.3 * r * std::sin(a)};
				const Vec3 n = normalize(Vec3{-std::cos(a), 0.3, std::sin(a)});
				for (const Vec3 &v : {p1, p2, tip}) crown.verts.push_back(v), crown.normals.push_back(n);
				crown.tris.push_back({base, base + 1, base + 2});
				const double u = age / 0.9, bx = q.x + (r + 2.2 * q.size) * 1.3 * std::cos(a), bz = q.z + (r + 2.2 * q.size) * 1.3 * std::sin(a), by = y + tall + 3.0 * q.size * u - 4.9 * age * age;
				if (by > y) {
					std::snprintf(buf, sizeof buf, "  AttributeBegin\n    Translate %.4f %.4f %.4f\n    Shape \"sphere\" \"float radius\" [ %.4f ]\n  AttributeEnd\n", bx, by, bz, 0.2 * q.size);
					drops += buf;
				}
			}
		}
	}
	std::string out = "# drawn water (film_toonwater.h)\n";
	const std::string white = diffuseMaterial({1.0, 1.0, 1.0});
	Mesh flat = lines;
	craft::detail::add(flat, rings);
	if (!flat.tris.empty()) out += "# bubbles (film_undersea.h)\nAttributeBegin\n  " + white + "\n" + emitMesh(water::detail::agreeing(flat), identity()) + "AttributeEnd\n";   // (no ink round these)
	if (!stars.tris.empty()) out += "# bubbles (film_undersea.h)\nAttributeBegin\n  " + white + "\n" + emitMesh(stars, identity()) + "AttributeEnd\n";
	if (!crown.tris.empty()) out += attr(white, emitMesh(crown, identity()));
	if (!drops.empty()) out += "AttributeBegin\n  " + white + "\n" + drops + "AttributeEnd\n";
	return out;
}

}  // namespace toonwater
}  // namespace cartoon
