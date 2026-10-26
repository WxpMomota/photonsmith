#pragma once
// film_flowers.h -- a meadow of wild flowers for a film file (film_file.h `flowers`): poppies, daisies, lupins or sunflowers, coming into bloom over the film and closing
// again, each flower in its own time - a time-lapse of a meadow opening in the morning and closing at dusk.
//
//   poppies     a nodding, hairy bud that lifts its head as it swells, then four red petals opening from a closed cup to a wide bowl round a dark centre
//   daisies     white rays folded up over a yellow disc, opening out flat
//   lupins      a spike of purple florets that opens from the bottom up, over palmate leaves at its foot
//   sunflowers  a tall stem with broad leaves and a heavy head whose yellow rays unfold from over the disc; the head turns to follow the sun across the sky (from the film's
//               `sky` line) and back to the east at night
//
// Each kind is made in a few variants at eight stages of its opening (bud .. open), each a pbrt object written once a frame when a frame uses it, placed once a flower
// (thousands of flowers are thousands of placements), leaning in the wind.
//
// Coordinates are pbrt's, as a film file's; the shapes are made in story space and placed with nature::placeObject.

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_terrain.h"
#include "nature_lib.h"

namespace cartoon {
namespace flowers {

inline const std::vector<std::string> &kinds() {
	static const std::vector<std::string> k = {"poppies", "daisies", "lupins", "sunflowers"};
	return k;
}

// A meadow: its kind, how many, over which rectangle (pbrt's x0 z0 x1 z1), when it comes into bloom and when it closes (film seconds; below 0: open all the time, or
// never closing), how big, a seed.
struct Meadow {
	std::string kind = "poppies";
	int count = 500;
	double x0 = -5.0, z0 = -5.0, x1 = 5.0, z1 = 5.0;
	double bloom0 = -1.0, bloom1 = -1.0, close0 = -1.0, close1 = -1.0, scale = 1.0;
	int seed = 1;
};

constexpr int kStages = 8, kVariants = 3;

namespace detail {

struct Parts {
	Mesh green, petals, centre;
};
inline Vec3 transformDir(const Mat4 &m, Vec3 d) { return {m[0] * d.x + m[1] * d.y + m[2] * d.z, m[4] * d.x + m[5] * d.y + m[6] * d.z, m[8] * d.x + m[9] * d.y + m[10] * d.z}; }
inline void blob(Mesh &m, Vec3 at, Vec3 size, const Mat4 &place = identity()) {
	const Mesh s = uvSphere(1.0, 8, 5);
	const int b = static_cast<int>(m.verts.size());
	for (std::size_t i = 0; i < s.verts.size(); ++i) {
		m.verts.push_back(transformPoint(place, at + Vec3{s.verts[i].x * size.x, s.verts[i].y * size.y, s.verts[i].z * size.z}));
		m.normals.push_back(normalize(Vec3{s.normals[i].x / size.x, s.normals[i].y / size.y, s.normals[i].z / size.z}));
	}
	for (const auto &t : s.tris) m.tris.push_back({t[0] + b, t[1] + b, t[2] + b});
}
// a stem: a thin tube up a gentle curve, `h` high, bending `bend` metres to the side at its top
inline Vec3 stemAt(double h, double bend, double u) { return {bend * u * u, h * u, 0.0}; }
inline void stem(Mesh &m, double h, double bend, double r) {
	for (int k = 0; k < 6; ++k) {
		const Vec3 a = stemAt(h, bend, k / 6.0), b = stemAt(h, bend, (k + 1) / 6.0);
		nature::addStrip(m, [&](double s) { return a + (b - a) * s; }, [&](double) { return 2.0 * r; }, {1, 0, 0}, 1);
		nature::addStrip(m, [&](double s) { return a + (b - a) * s; }, [&](double) { return 2.0 * r; }, {0, 0, 1}, 1);
	}
}

// A flower of a kind, variant v, at opening s (0 a bud .. 1 open), standing at the origin (story space; a sunflower's head faces +z).
inline Parts flowerAt(const std::string &kind, int v, double s) {
	Parts p;
	const double var = hash01(v, 7), open = std::clamp(s, 0.0, 1.0);
	if (kind == "poppies") {
		const double h = 0.42 + 0.12 * var, bend = 0.04 * (var - 0.5);
		stem(p.green, h, bend, 0.0025);
		const Vec3 top = stemAt(h, bend, 1.0);
		const double lift = std::clamp(open / 0.35, 0.0, 1.0);   // (the bud hangs, then lifts its head as it swells)
		const Mat4 head = mul(translate(top), rotate({0, 0, 1}, 150.0 * (1.0 - lift)));
		if (open < 0.3) {
			blob(p.green, {0, 0.012, 0}, {0.009, 0.014, 0.009}, head);
		} else {
			const double a = 8.0 + 55.0 * smoothstep(0.3, 1.0, open);   // (from a closed cup to a wide bowl)
			for (int k = 0; k < 4; ++k) {
				const double yaw = 90.0 * k + 20.0 * var;
				const Vec3 out = Vec3{std::cos(yaw * kPi / 180.0), 0.0, std::sin(yaw * kPi / 180.0)};
				const Vec3 dir = normalize(out * std::sin(a * kPi / 180.0) + Vec3{0.0, std::cos(a * kPi / 180.0), 0.0});
				nature::addLeaf(p.petals, transformPoint(head, out * 0.004), transformDir(head, dir), transformDir(head, normalize(Vec3{0, 1, 0} - out * 0.6)), 0.04, 0.05);
			}
			blob(p.centre, {0, 0.008, 0}, {0.006, 0.007, 0.006}, head);
		}
		for (int k = 0; k < 2; ++k)   // (two leaves low on the stem)
			nature::addLeaf(p.green, stemAt(h, bend, 0.12 + 0.1 * k), {std::cos(2.6 * k + var), 0.5, std::sin(2.6 * k + var)}, {0, 1, 0}, 0.1, 0.035);
	} else if (kind == "daisies") {
		const double h = 0.25 + 0.1 * var, bend = 0.03 * (var - 0.5);
		stem(p.green, h, bend, 0.0018);
		const Vec3 top = stemAt(h, bend, 1.0);
		const double a = 10.0 + 78.0 * smoothstep(0.1, 1.0, open);   // (rays folded up over the disc, then out flat)
		for (int k = 0; k < 18; ++k) {
			const double yaw = 20.0 * k + 7.0 * hash01(v * 31 + k, 3);
			const Vec3 out{std::cos(yaw * kPi / 180.0), 0.0, std::sin(yaw * kPi / 180.0)};
			const Vec3 dir = normalize(out * std::sin(a * kPi / 180.0) + Vec3{0.0, std::cos(a * kPi / 180.0), 0.0});
			nature::addLeaf(p.petals, top + out * 0.006 + Vec3{0, 0.002, 0}, dir, normalize(Vec3{0, 1, 0} + dir * -0.3), 0.022, 0.006);
		}
		blob(p.centre, top + Vec3{0, 0.003, 0}, {0.007, 0.0035 + 0.001 * open, 0.007});
		nature::addLeaf(p.green, {0, 0.01, 0}, {1, 0.3, 0.2 * var}, {0, 1, 0}, 0.06, 0.02);
	} else if (kind == "lupins") {
		const double h = 0.65 + 0.2 * var, bend = 0.05 * (var - 0.5);
		stem(p.green, h, bend, 0.004);
		const int florets = 28;
		for (int k = 0; k < florets; ++k) {   // the spike: florets round the top third, opening from the bottom up
			const double u = 0.62 + 0.38 * k / florets, around = 2.4 * k;
			const Vec3 at = stemAt(h, bend, u) + Vec3{0.012 * (1.0 - 0.5 * k / florets) * std::cos(around), 0.0, 0.012 * (1.0 - 0.5 * k / florets) * std::sin(around)};
			const bool out = static_cast<double>(k) / florets < 1.15 * open - 0.05;
			blob(out ? p.petals : p.green, at, out ? Vec3{0.008, 0.007, 0.008} : Vec3{0.004, 0.006, 0.004});
		}
		for (int k = 0; k < 5; ++k)   // palmate leaves at its foot
			for (int f = 0; f < 7; ++f) {
				const double a = 2.0 * kPi * k / 5.0 + var, fan = (f - 3) * 0.22;
				nature::addLeaf(p.green, {0.0, 0.08 + 0.02 * k, 0.0}, {std::cos(a + fan), 0.25, std::sin(a + fan)}, {0, 1, 0}, 0.08, 0.016);
			}
	} else {   // sunflowers
		const double h = 1.5 + 0.35 * var;
		stem(p.green, h, 0.0, 0.012);
		for (int k = 0; k < 6; ++k)   // broad leaves up the stem
			nature::addLeaf(p.green, {0.0, h * (0.2 + 0.12 * k), 0.0}, {std::cos(2.4 * k + var), 0.15, std::sin(2.4 * k + var)}, {0, 1, 0}, 0.28 - 0.025 * k, 0.22 - 0.02 * k);
		// the head: facing +z, nodding a little, on a neck bent over from the stem's top
		const Mat4 head = mul(translate(0.0, h + 0.06, 0.05), rotate({1, 0, 0}, 20.0));
		blob(p.centre, {0, 0, 0}, {0.13, 0.13, 0.03}, head);
		blob(p.green, {0, 0, -0.03}, {0.14, 0.14, 0.035}, head);   // (the green back of the head)
		const double a = 15.0 + 75.0 * smoothstep(0.15, 1.0, open);   // (rays folded forward over the disc, then out)
		for (int k = 0; k < 26; ++k) {
			const double around = 2.0 * kPi * k / 26.0;
			const Vec3 out{std::cos(around), std::sin(around), 0.0};
			const Vec3 dir = normalize(out * std::sin(a * kPi / 180.0) + Vec3{0.0, 0.0, std::cos(a * kPi / 180.0)});
			nature::addLeaf(p.petals, transformPoint(head, out * 0.12), transformDir(head, dir), transformDir(head, {0, 0, 1}), 0.085, 0.032);
		}
	}
	return p;
}
inline Vec3 petalColour(const std::string &kind) {
	return kind == "poppies" ? Vec3{0.75, 0.05, 0.03} : kind == "daisies" ? Vec3{0.86, 0.86, 0.84} : kind == "lupins" ? Vec3{0.32, 0.16, 0.62} : Vec3{0.85, 0.6, 0.04};
}
inline Vec3 centreColour(const std::string &kind) {
	return kind == "poppies" ? Vec3{0.03, 0.025, 0.03} : kind == "daisies" ? Vec3{0.8, 0.55, 0.04} : Vec3{0.12, 0.07, 0.03};
}

}  // namespace detail

// The object of a kind's variant at stage k (0 a bud .. kStages open).
inline std::string objectName(const std::string &kind, int v, int k) { return "flower_" + kind + "_" + std::to_string(v) + "_" + std::to_string(k); }
inline std::string objectText(const std::string &kind, int v, int k) {
	const detail::Parts p = detail::flowerAt(kind, v, static_cast<double>(k) / kStages);
	const Vec3 petal = detail::petalColour(kind);
	std::string out = nature::objectBegin(objectName(kind, v, k)) + attr(diffuseMaterial({0.08, 0.2, 0.04}), emitMesh(p.green, identity()));
	if (!p.petals.tris.empty()) out += attr(translucentMaterial(petal, petal * 0.5), emitMesh(p.petals, identity()));
	if (!p.centre.tris.empty()) out += attr(diffuseMaterial(detail::centreColour(kind)), emitMesh(p.centre, identity()));
	return out + nature::objectEnd();
}

// How open a flower is at film time t (0 a bud .. 1 open): blooming over its own part of the meadow's bloom, closing over its own part of the close. `own` picks its part.
inline double openAt(const Meadow &m, int own, double t) {
	double s = 1.0;
	if (m.bloom1 > m.bloom0) {
		const double start = m.bloom0 + 0.6 * (m.bloom1 - m.bloom0) * hash01(own, 41);
		s = smoothstep(start, start + 0.4 * (m.bloom1 - m.bloom0), t);
	}
	if (m.close1 > m.close0) {
		const double start = m.close0 + 0.6 * (m.close1 - m.close0) * hash01(own, 42);
		s *= 1.0 - smoothstep(start, start + 0.4 * (m.close1 - m.close0), t);
	}
	return s;
}

// A meadow at film time t, as scene text: the objects it uses (once a frame: `written`), then a placement for each flower on the land, out of the water, leaning in the
// wind (`wind`: the wind's time). `sunFacing`: where the sun is (degrees as a facing, 0 +z, 90 +x; pbrt's), which sunflowers turn to; `sunUp` false at night.
inline std::string text(const Meadow &m, double t, double wind, const terrain::Field *ground, double waterLevel, double sunFacing, bool sunUp, double eastFacing,
						std::set<std::string> &written) {
	std::string objects, places;
	for (int i = 0; i < m.count; ++i) {
		const int s = m.seed * 7907 + i;
		const double x = m.x0 + (m.x1 - m.x0) * hash01(s, 1), z = m.z0 + (m.z1 - m.z0) * hash01(s, 2);
		const double y = ground && ground->inside(x, z) ? ground->heightAt(x, z) : 0.0;
		if (y < waterLevel + 0.1) continue;
		const int v = static_cast<int>(hash01(s, 3) * kVariants) % kVariants;
		const int k = static_cast<int>(std::lround(openAt(m, s, t) * kStages));
		const std::string name = objectName(m.kind, v, k);
		if (written.insert(name).second) objects += objectText(m.kind, v, k);
		const Vec3 foot{-x, y, z};
		double yaw = 360.0 * hash01(s, 4);
		if (m.kind == "sunflowers") {   // (the head to the sun, a little each its own way; in the dark, back to the east)
			const double f = (sunUp ? sunFacing : eastFacing) * kPi / 180.0;
			const Vec3 to{-std::sin(f), 0.0, std::cos(f)};   // (story space)
			yaw = std::atan2(to.x, to.z) * 180.0 / kPi + 12.0 * (hash01(s, 4) - 0.5);
		}
		const Vec3 w = nature::windLean(foot.x, foot.z, wind, 0.05);
		places += nature::placeObject(name, mul(translate(foot), nature::shearXZ(w.x, w.z), rotate({0, 1, 0}, yaw), scale(m.scale * (0.85 + 0.3 * hash01(s, 5)))));
	}
	return places.empty() ? std::string() : "# a meadow of " + m.kind + " (film_flowers.h)\n" + objects + places;
}

}  // namespace flowers
}  // namespace cartoon
