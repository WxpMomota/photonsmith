#pragma once
// film_bankside.h -- what grows where water meets land (water round 6): `bankside [reeds D] [lilies D] [duckweed D] [willows N] [mud W] [patch X Z R]... [seed K]`.
//
//   the shore    the line where the land goes under the film's water, and both edges of its rivers: everything here grows along that line, into the water or just above it. Without a
//                shore (a lake with no terrain) it grows in `patch X Z R`es instead.
//   reeds        clumps along the shore (D to the metre), blades and stems, one in three with a bulrush's brown head. They lean and shiver in the wind, and part for a boat or someone wading
//                through, closing behind them.
//   lilies       pads lying on the water in the shallows (D to the metre of shore), notched, rocking on rings that pass under them and pushed aside by what comes through, one in seven
//                with a flower.
//   duckweed     a green scum of tiny leaves in still corners, thinning out into the open water.
//   willows      a trunk leaning over the water, its long strands hanging to the surface and swaying together.
//   mud          the wet dark ground a little above the waterline, W metres of it.
//   the water    `water ... ripples R` (0 mirror calm, 1 usual) and `catspaws C`: dark patches of wind running over still water. `watermist A [during T0 T1]`: mist lifting off it.
//
// Coordinates: pbrt's in a film file; meshes in story space (x mirrored), a ply file's pbrt's. The plants are laid out when the film is loaded; they move a frame at a time.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_air.h"
#include "film_cache.h"
#include "film_craft.h"
#include "film_stir.h"
#include "film_stream.h"
#include "film_terrain.h"
#include "film_water.h"
#include "nature_lib.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace bankside {

struct Patch {
	double x = 0.0, z = 0.0, r = 5.0;
};
struct Clump {
	double x, z, y, height, lean;
	int seed;
	bool head;
};
struct Pad {
	double x, z, r, turn;
	int seed;
	bool flower;
};
struct Willow {
	Vec3 foot;
	double lean, height;
	int seed;
	double water;   // (the height of the water its strands hang to: its shore's, the same every frame)
};
struct Weed {
	double x, z, y, size;   // (y: the water's height where it floats)
};
struct Bank {
	bool on = false;
	double reeds = 0.0, lilies = 0.0, duckweed = 0.0, mud = 0.0;
	int willows = 0, seed = 1;
	std::vector<Patch> patches;
	// laid out when the film is loaded
	std::vector<Clump> clumps;
	std::vector<Pad> pads;
	std::vector<Willow> trees;
	std::vector<Weed> weed;
	std::string key;   // (the name of its files: from every weed's and willow's place, so an edited film's are made again)
};
struct Mist {
	bool on = false;
	double amount = 1.0, t0 = 0.0, t1 = 1e30;
};
// Something going through the plants: where it is, how far it pushes them.
struct Mover {
	double x, z, r;
};

// A point of the shore: where (pbrt's x z, the height the water stands there), which way the water lies, the metres of shore it stands for, and how far out from it there is
// water of its own (a river's middle: past it is the other bank's).
struct Shore {
	double x, z, y, inX, inZ, every, reach;
};
inline std::vector<Shore> shoreOf(const std::vector<water::Sea> &seas, const std::vector<water::River> &rivers, const terrain::Field *ground) {
	std::vector<Shore> out;
	if (ground && !seas.empty()) {
		const double level = seas[0].level, cell = ground->cell;
		for (int j = 0; j + 1 < ground->n; ++j)
			for (int i = 0; i + 1 < ground->n; ++i) {
				const double x = ground->x0 + i * cell, z = ground->z0 + j * cell;
				const double a = ground->heightAt(x, z) - level, b = ground->heightAt(x + cell, z) - level, c = ground->heightAt(x, z + cell) - level;
				if ((a < 0.0) == (b < 0.0) && (a < 0.0) == (c < 0.0)) continue;
				const double gx = b - a, gz = c - a, g = std::max(1e-6, std::hypot(gx, gz));
				out.push_back({x + 0.5 * cell, z + 0.5 * cell, level, -gx / g, -gz / g, std::max(1.0, cell), 1e30});
			}
	}
	for (const water::River &r : rivers) {
		if (r.along.empty()) continue;
		for (double s = 1.0; s < r.along.back(); s += 1.0) {
			const stream::Place p = stream::placeAt(r, s);
			if (stream::rapidAt(r, s) > 0.3) continue;   // (nothing roots in rapids)
			const double w = stream::widthAt(r, s);
			for (const double side : {-1.0, 1.0}) {
				const Vec3 q = p.at + p.side * (side * 0.5 * w);
				out.push_back({q.x, q.z, q.y, -side * p.side.x, -side * p.side.z, 1.0, 0.5 * w});   // (a point every metre)
			}
		}
	}
	return out;
}
// Lays out the plants.
inline void prepare(Bank &b, const std::vector<water::Sea> &seas, const std::vector<water::River> &rivers, const terrain::Field *ground) {
	const std::vector<Shore> shore = shoreOf(seas, rivers, ground);
	const auto groundAt = [&](double x, double z) { return ground && ground->inside(x, z) ? ground->heightAt(x, z) : -1e30; };
	const double level = !seas.empty() ? seas[0].level : !rivers.empty() && !rivers[0].path.empty() ? rivers[0].path[0].y : 0.0;   // (a patch's water: the sea's, or with none the river's, as it was drawn)
	const auto clumped = [&b](double x, double z, int salt) { return nature::noise2(x * 0.09 + b.seed, z * 0.09, salt); };
	int k = b.seed * 7001;
	for (const Shore &p : shore) {
		const double perPoint = p.every;   // (a sea's shore point stands for a cell of land; a river's for its metre: a river bank had `reeds` times the cell to the metre)
		const int clumps = static_cast<int>(b.reeds * perPoint + hash01(k, 1));
		if (clumped(p.x, p.z, 811) > 0.42)
			for (int n = 0; n < clumps && b.clumps.size() < 9000; ++n, ++k) {
				const double out = -0.4 + 2.2 * hash01(k, 2) * hash01(k, 3), along = perPoint * (hash01(k, 4) - 0.5), x = p.x + p.inX * out - p.inZ * along, z = p.z + p.inZ * out + p.inX * along;
				const double bed = groundAt(x, z);
				b.clumps.push_back({x, z, bed > -1e29 ? std::min(bed, p.y + 0.3) : p.y - 0.3, 1.1 + 1.1 * hash01(k, 5), 0.2 * (hash01(k, 6) - 0.5), k, hash01(k, 7) < 0.35});
			}
		const int pads = static_cast<int>(b.lilies * perPoint + hash01(k + 1, 1));
		if (clumped(p.x, p.z, 813) > 0.45)
			for (int n = 0; n < pads && b.pads.size() < 2500; ++n, ++k) {
				const double out = 1.2 + 6.0 * hash01(k, 2), along = 2.0 * perPoint * (hash01(k, 3) - 0.5), x = p.x + p.inX * out - p.inZ * along, z = p.z + p.inZ * out + p.inX * along;
				if (groundAt(x, z) > p.y - 0.15 || out > p.reach) continue;   // (it floats: there must be water under it, this bank's)
				b.pads.push_back({x, z, 0.1 + 0.16 * hash01(k, 4), 360.0 * hash01(k, 5), k, hash01(k, 6) < 0.14});
			}
		const int weed = static_cast<int>(6.0 * b.duckweed * perPoint);
		if (clumped(p.x, p.z, 815) > 0.5)
			for (int n = 0; n < weed && b.weed.size() < 60000; ++n, ++k) {
				const double out = 0.3 + 5.0 * hash01(k, 2) * hash01(k, 3), along = 2.0 * perPoint * (hash01(k, 4) - 0.5), x = p.x + p.inX * out - p.inZ * along, z = p.z + p.inZ * out + p.inX * along;
				if (groundAt(x, z) < p.y && out <= p.reach) b.weed.push_back({x, z, p.y, 0.008 + 0.012 * hash01(k, 5)});
			}
	}
	for (const Patch &q : b.patches) {
		const double area = kPi * q.r * q.r;
		for (int n = 0; n < static_cast<int>(area * b.reeds) && b.clumps.size() < 9000; ++n, ++k) {
			const double a = 6.283 * hash01(k, 1), d = q.r * std::sqrt(hash01(k, 2));
			b.clumps.push_back({q.x + d * std::cos(a), q.z + d * std::sin(a), level - 0.3, 1.1 + 1.1 * hash01(k, 3), 0.2 * (hash01(k, 4) - 0.5), k, hash01(k, 5) < 0.35});
		}
		for (int n = 0; n < static_cast<int>(area * b.lilies) && b.pads.size() < 2500; ++n, ++k) {
			const double a = 6.283 * hash01(k, 1), d = q.r * std::sqrt(hash01(k, 2));
			b.pads.push_back({q.x + d * std::cos(a), q.z + d * std::sin(a), 0.1 + 0.16 * hash01(k, 3), 360.0 * hash01(k, 4), k, hash01(k, 5) < 0.14});
		}
		for (int n = 0; n < static_cast<int>(40.0 * area * b.duckweed) && b.weed.size() < 60000; ++n, ++k) {
			const double a = 6.283 * hash01(k, 1), d = q.r * std::sqrt(hash01(k, 2));
			b.weed.push_back({q.x + d * std::cos(a), q.z + d * std::sin(a), level, 0.008 + 0.012 * hash01(k, 3)});
		}
	}
	for (int n = 0; n < b.willows && !shore.empty(); ++n, ++k) {
		const Shore &p = shore[static_cast<std::size_t>(hash01(k, 1) * shore.size()) % shore.size()];
		const double x = p.x - 1.6 * p.inX, z = p.z - 1.6 * p.inZ, bed = groundAt(x, z);
		b.trees.push_back({{x, bed > -1e29 ? bed : p.y, z}, std::atan2(p.inX, p.inZ), 7.0 + 3.0 * hash01(k, 2), k, p.y});
	}
	std::string words;
	char buf[96];
	for (const Weed &q : b.weed) std::snprintf(buf, sizeof buf, "%.3f,%.3f,%.3f;", q.x, q.z, q.y), words += buf;
	for (const Willow &w : b.trees) std::snprintf(buf, sizeof buf, "%.3f,%.3f,%.3f,%.4f,%.3f,%.3f;", w.foot.x, w.foot.y, w.foot.z, w.lean, w.height, w.water), words += buf;
	b.key = film_cache::nameOf(words);
}

namespace detail {
inline void blade(Mesh &m, Vec3 foot, Vec3 bend, double H, double width, int segs, const Vec3 &toEye) {
	const int base = static_cast<int>(m.verts.size());
	Vec3 last = foot;
	for (int i = 0; i <= segs; ++i) {
		const double u = i / double(segs);
		const Vec3 p = foot + Vec3{0.0, H * u, 0.0} + bend * (u * u);
		const Vec3 along = i == 0 ? Vec3{0.0, 1.0, 0.0} : normalize(p - last), side = normalize(cross(along, toEye) + Vec3{1e-6, 0.0, 0.0}), n = normalize(cross(side, along));
		const double w = width * (1.0 - 0.9 * u);
		m.verts.push_back(p - side * w), m.verts.push_back(p + side * w);
		m.normals.push_back(n), m.normals.push_back(n);
		last = p;
	}
	for (int i = 0; i < segs; ++i) {
		const int a = base + 2 * i;
		m.tris.push_back({a, a + 1, a + 2}), m.tris.push_back({a + 1, a + 3, a + 2});
	}
}
inline Mesh padMesh(double r) {
	Mesh m;
	const int spokes = 20;
	m.verts.push_back({0.0, 0.0, 0.0}), m.normals.push_back({0.0, 1.0, 0.0});
	for (int i = 0; i <= spokes; ++i) {
		const double a = (0.09 + 1.82 * i / spokes) * kPi, R = r * (1.0 + 0.03 * std::sin(5.0 * a));
		m.verts.push_back({R * std::sin(a), 0.004 * r / 0.15, R * std::cos(a)}), m.normals.push_back({0.0, 1.0, 0.0});
	}
	for (int i = 0; i < spokes; ++i) m.tris.push_back({0, i + 1, i + 2});
	return m;
}
inline Mesh flowerMesh(double r) {
	Mesh m;
	for (int ring = 0; ring < 3; ++ring) {
		const int petals = 10 - ring * 2;
		const double open = 62.0 - 18.0 * ring, L = r * (1.0 - 0.14 * ring);
		for (int p = 0; p < petals; ++p) {
			const int base = static_cast<int>(m.verts.size());
			const double a = (p + 0.5 * ring) * 6.283 / petals;
			const Vec3 out{std::sin(a), 0.0, std::cos(a)}, side{std::cos(a), 0.0, -std::sin(a)}, lift = out * std::sin(open * kPi / 180.0) + Vec3{0.0, 1.0, 0.0} * std::cos(open * kPi / 180.0);
			const Vec3 foot = out * (0.03 * r) + Vec3{0.0, 0.01, 0.0}, mid = foot + lift * (0.55 * L) + out * (0.1 * L), tip = foot + lift * L - out * (0.08 * L);
			for (const Vec3 &q : {foot, mid + side * (0.17 * L), tip, mid - side * (0.17 * L)}) m.verts.push_back(q), m.normals.push_back(normalize(lift + Vec3{0.0, 0.6, 0.0}));
			m.tris.push_back({base, base + 1, base + 2}), m.tris.push_back({base, base + 2, base + 3});
		}
	}
	return m;
}
inline Mesh willowTrunk(const Willow &w, std::vector<Vec3> &tips) {   // (pbrt's points, for a ply file)
	Mesh m;
	const Vec3 lean{std::sin(w.lean), 0.0, std::cos(w.lean)}, top = w.foot + lean * (0.3 * w.height) + Vec3{0.0, 0.62 * w.height, 0.0};
	const auto add = [&m](const Mesh &o) {
		const int base = static_cast<int>(m.verts.size());
		m.verts.insert(m.verts.end(), o.verts.begin(), o.verts.end()), m.normals.insert(m.normals.end(), o.normals.begin(), o.normals.end());
		for (const auto &t : o.tris) m.tris.push_back({t[0] + base, t[1] + base, t[2] + base});
	};
	add(sdf::tubeMesh({w.foot + Vec3{0.0, -0.3, 0.0}, lerp(w.foot, top, 0.5) + lean * 0.2, top}, 0.05 * w.height, 0.6, 10));
	for (int n = 0; n < 9; ++n) {
		const int k = w.seed * 17 + n;
		const double a = 6.283 * n / 9.0 + hash01(k, 1), reach = 0.3 * w.height * (0.7 + 0.5 * hash01(k, 2));
		const Vec3 out{std::sin(a), 0.0, std::cos(a)}, tip = top + out * reach + Vec3{0.0, 0.16 * w.height * (0.6 + 0.8 * hash01(k, 3)), 0.0};
		add(sdf::tubeMesh({top, lerp(top, tip, 0.5) + Vec3{0.0, 0.05 * w.height, 0.0}, tip}, 0.009 * w.height, 0.3, 6));
		for (int q = 0; q < 60; ++q) {   // (a dome: highest over the trunk, falling to its rim)
			const double u = 0.15 + 1.0 * hash01(k + q, 4);
			const Vec3 p = lerp(top, tip, u) + Vec3{1.2 * (hash01(k + q, 5) - 0.5), 0.0, 1.2 * (hash01(k + q, 7) - 0.5)};
			const double from = std::hypot(p.x - top.x, p.z - top.z) / std::max(0.1, reach * 1.2);
			tips.push_back({p.x, top.y + w.height * (0.3 * std::sqrt(std::max(0.0, 1.0 - from * from)) + 0.05 * hash01(k + q, 6)), p.z});
		}
	}
	return m;
}
// One of six hanging curtains of a willow: its strands from the branch tips, each ending just over the water.
inline Mesh willowCurtain(const Willow &w, const std::vector<Vec3> &tips, int group, double level) {
	Mesh m;
	for (std::size_t n = 0; n < tips.size(); ++n) {
		if (static_cast<int>(n % 6) != group) continue;
		const int k = w.seed * 31 + static_cast<int>(n);
		const Vec3 from = tips[n];
		const double reach = std::max(1.0, (from.y - level) * (0.5 + 0.48 * hash01(k, 1)));   // (some nearly to the water, most not)
		const int segs = 24;
		for (int i = 0; i < segs; ++i) {
			const double u = (i + 0.5) / segs, wob = 0.05 * std::sin(u * 9.0 + k);
			const Vec3 p = from + Vec3{wob, -reach * u, 0.04 * std::cos(u * 7.0 + k)};
			const double a = 6.283 * hash01(k + i, 2);
			const Vec3 d{0.09 * std::sin(a), -0.2, 0.09 * std::cos(a)}, s{0.055 * std::cos(a), 0.0, -0.055 * std::sin(a)};
			const int base = static_cast<int>(m.verts.size());
			for (const Vec3 &q : {p, p + d * 0.5 + s, p + d, p + d * 0.5 - s}) m.verts.push_back(q), m.normals.push_back(normalize(Vec3{0.2, 1.0, 0.1}));
			m.tris.push_back({base, base + 1, base + 2}), m.tris.push_back({base, base + 2, base + 3});
		}
		std::vector<Vec3> line;
		for (int i = 0; i <= 6; ++i) line.push_back(from + Vec3{0.05 * std::sin(i / 6.0 * 9.0 + k), -reach * i / 6.0, 0.04 * std::cos(i / 6.0 * 7.0 + k)});
		const Mesh stem = sdf::tubeMesh(line, 0.006, 0.6, 3);
		const int base = static_cast<int>(m.verts.size());
		m.verts.insert(m.verts.end(), stem.verts.begin(), stem.verts.end()), m.normals.insert(m.normals.end(), stem.normals.begin(), stem.normals.end());
		for (const auto &t : stem.tris) m.tris.push_back({t[0] + base, t[1] + base, t[2] + base});
	}
	return m;
}
inline Mesh storyOf(Mesh m) {   // (a mesh made in pbrt's coordinates, as a ply file wants it)
	for (Vec3 &q : m.verts) q.x = -q.x;
	for (Vec3 &q : m.normals) q.x = -q.x;
	for (auto &t : m.tris) std::swap(t[1], t[2]);
	return m;
}
inline Vec3 pushedBy(const std::vector<Mover> &movers, double x, double z) {
	Vec3 p{0.0, 0.0, 0.0};
	for (const Mover &m : movers) {
		const double dx = x - m.x, dz = z - m.z, d = std::hypot(dx, dz);
		if (d > 1e-6 && d < m.r) p = p + Vec3{dx / d, 0.0, dz / d} * ((m.r - d));
	}
	return p;
}
}  // namespace detail

// What grows by the water at t, as scene text. `wind`: the wind on it at (x, z), pbrt's. `eye`: pbrt's.
template <class WindAt, class LevelAt>
inline std::string text(const std::string &film, const Bank &b, double t, const Vec3 &eye, const Vec3 &ahead, double hfov, const stir::Field *stirred, const std::vector<Mover> &movers, WindAt windAt, LevelAt levelAt, double rise = 0.0) {
	if (!b.on) return std::string();
	std::string out = "# by the water (film_bankside.h)\n";
	const double wide = std::cos(std::min(kPi, (0.5 * hfov + 14.0) * kPi / 180.0));
	const auto inView = [&](double x, double z, double within) {
		const Vec3 to{x - eye.x, 0.0, z - eye.z};
		const double d = length(to);
		return d < within && (d < 3.0 || dot(to * (1.0 / d), ahead) > wide);
	};
	Mesh green, brown;
	for (const Clump &c : b.clumps) {
		if (!inView(c.x, c.z, 150.0)) continue;
		const double d = std::hypot(c.x - eye.x, c.z - eye.z);
		const Vec3 wind = windAt(c.x, c.z);
		const double shiver = 0.05 * std::sin(1.9 * t + c.seed) + 0.03 * std::sin(4.3 * t + 2.0 * c.seed);
		const Vec3 push = detail::pushedBy(movers, c.x, c.z);
		const Vec3 bend = Vec3{wind.x * 0.1 + shiver + c.lean, 0.0, wind.z * 0.1 + shiver - c.lean} + push * 0.8;
		const Vec3 foot{-c.x, c.y, c.z}, bendS{-bend.x, 0.0, bend.z}, toEye = normalize(Vec3{-(eye.x - c.x), 0.0, eye.z - c.z} + Vec3{1e-6, 0.0, 0.0});
		const int blades = d < 30.0 ? 5 : d < 70.0 ? 3 : 2, segs = d < 30.0 ? 5 : 3;
		for (int n = 0; n < blades; ++n) {
			const int k = c.seed + n;
			const Vec3 off{0.14 * (hash01(k, 11) - 0.5), 0.0, 0.14 * (hash01(k, 12) - 0.5)}, flare{0.4 * (hash01(k, 13) - 0.5), 0.0, 0.4 * (hash01(k, 14) - 0.5)};
			detail::blade(green, foot + off, bendS + flare * 0.6, c.height * (0.75 + 0.35 * hash01(k, 15)), 0.011, segs, toEye);
		}
		if (c.head) {
			const Vec3 tipAt = foot + Vec3{0.0, c.height * 1.08, 0.0} + bendS;
			detail::blade(green, foot, bendS, c.height * 1.08, 0.006, segs, toEye);
			if (d < 110.0) craft::detail::add(brown, capsule(0.017, 0.15, 6, 2), alignY(tipAt - Vec3{0.0, 0.2, 0.0} + bendS * -0.1, Vec3{bendS.x * 0.3, 1.0, bendS.z * 0.3}));
		}
	}
	if (!green.tris.empty()) out += attr(coated({0.27, 0.36, 0.13}, 0.45), emitMesh(green, identity()));
	if (!brown.tris.empty()) out += attr(diffuseMaterial({0.25, 0.15, 0.08}), emitMesh(brown, identity()));
	Mesh pads, flowers, hearts;
	for (const Pad &p : b.pads) {
		if (!inView(p.x, p.z, 110.0)) continue;
		const Vec3 push = detail::pushedBy(movers, p.x, p.z);
		const double x = p.x + 0.9 * push.x, z = p.z + 0.9 * push.z;
		double h = 0.0, sx = 0.0, sz = 0.0;
		if (stirred) stirred->at(x, z, t, h, sx, sz);
		const Vec3 up = normalize(Vec3{sx, 1.0, -sz});
		const Mat4 at = mul(translate(-x, levelAt(x, z) + h + 0.006, z), alignY({0.0, 0.0, 0.0}, up), rotate({0.0, 1.0, 0.0}, p.turn + 8.0 * std::sin(0.3 * t + p.seed)));
		craft::detail::add(pads, detail::padMesh(p.r), at);
		if (p.flower) {
			craft::detail::add(flowers, detail::flowerMesh(0.1), mul(at, translate(0.02, 0.012, 0.03)));
			craft::detail::add(hearts, uvSphere(0.018, 8, 5), mul(at, translate(0.02, 0.045, 0.03)));
		}
	}
	if (!pads.tris.empty()) out += attr(coated({0.09, 0.27, 0.09}, 0.12), emitMesh(pads, identity()));
	if (!flowers.tris.empty()) out += attr(diffuseMaterial({0.95, 0.88, 0.9}), emitMesh(flowers, identity()));
	if (!hearts.tris.empty()) out += attr(diffuseMaterial({0.92, 0.72, 0.12}), emitMesh(hearts, identity()));
	if (!b.weed.empty()) {
		const std::string weed = film_cache::ply(film, "bank2_weed_" + b.key, [&] {
			Mesh m;
			for (const Weed &q : b.weed) {
				const int base = static_cast<int>(m.verts.size());
				for (int i = 0; i < 4; ++i) {
					const double a = i * 1.5708 + q.x;
					m.verts.push_back({-(q.x + q.size * std::cos(a)), q.y + 0.004, q.z + q.size * std::sin(a)}), m.normals.push_back({0.0, 1.0, 0.0});   // (on its own water)
				}
				m.tris.push_back({base, base + 2, base + 1}), m.tris.push_back({base, base + 3, base + 2});
			}
			return m;
		});
		if (!weed.empty()) {   // (kept at the still water: moved up and down with a tide's rise, `rise`, as the pads are)
			char lift[64];
			std::snprintf(lift, sizeof lift, "  Translate 0 %.4f 0\n", rise);
			out += "AttributeBegin\n" + std::string(rise != 0.0 ? lift : "") + "  " + coated({0.2, 0.42, 0.08}, 0.3) + "\n  Shape \"plymesh\" \"string filename\" [ \"" + weed + "\" ]\nAttributeEnd\n";
		}
	}
	for (std::size_t n = 0; n < b.trees.size(); ++n) {
		const Willow &w = b.trees[n];
		std::vector<Vec3> tips;
		detail::willowTrunk(w, tips);
		const std::string base = "bank4_willow_" + b.key + "_" + std::to_string(n) + "_";
		const std::string trunk = film_cache::ply(film, base + "trunk", [&] {
			std::vector<Vec3> ignored;
			return detail::storyOf(detail::willowTrunk(w, ignored));
		});
		if (!trunk.empty()) out += "AttributeBegin\n  " + diffuseMaterial({0.24, 0.19, 0.14}) + "\n  Shape \"plymesh\" \"string filename\" [ \"" + trunk + "\" ]\nAttributeEnd\n";
		for (int g = 0; g < 6; ++g) {
			const std::string strands = film_cache::ply(film, base + std::to_string(g), [&] { return detail::storyOf(detail::willowCurtain(w, tips, g, w.water)); });   // (its shore's water: what the tide was at the first frame made was kept)
			if (strands.empty()) continue;
			const Vec3 wind = windAt(w.foot.x, w.foot.z);
			const Vec3 top = w.foot + Vec3{0.0, 0.62 * w.height, 0.0};
			const double a = (0.7 + 0.5 * std::sin(0.8 * t + 1.1 * g)) + 1.6 * wind.x, c = (0.5 * std::sin(0.63 * t + 1.7 * g)) + 1.6 * wind.z;
			char buf[220];
			std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.4f %.4f %.4f\n  Rotate %.3f 0 0 1\n  Rotate %.3f 1 0 0\n  Translate %.4f %.4f %.4f\n", top.x, top.y, top.z, -a, c, -top.x, -top.y, -top.z);
			out += buf + coated({0.33, 0.43, 0.13}, 0.4) + "\n  Shape \"plymesh\" \"string filename\" [ \"" + strands + "\" ]\nAttributeEnd\n";
		}
	}
	return out;
}
// The mud along the waterline: the land's own cells a little above the water, under a wet dark film. Static.
inline std::string mudText(const std::string &film, const Bank &b, const terrain::Field &g, double level) {
	if (b.mud <= 0.0) return std::string();
	char words[64];   // (its name: the land's shape and the water's line too, or an edited film kept the old mud)
	std::snprintf(words, sizeof words, "|%.3f|%.4f|%d", b.mud, level, g.n);
	const std::string name = film_cache::ply(film, "bank2_mud_" + film_cache::nameOf(g.spec.key() + words), [&] {
		Mesh m;
		for (int j = 0; j + 1 < g.n; ++j)
			for (int i = 0; i + 1 < g.n; ++i) {
				const double x = g.x0 + i * g.cell, z = g.z0 + j * g.cell, h = g.heightAt(x + 0.5 * g.cell, z + 0.5 * g.cell) - level;
				const double wander = 0.5 + 0.8 * nature::noise2(x * 0.1, z * 0.1, 817);
				if (h < -0.4 || h > 0.25 * b.mud * wander) continue;
				const int base = static_cast<int>(m.verts.size());
				for (const auto &[a, c] : {std::pair<double, double>{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}}) {
					const double px = x + a * g.cell, pz = z + c * g.cell;
					m.verts.push_back({-px, g.heightAt(px, pz) + 0.012, pz}), m.normals.push_back({0.0, 1.0, 0.0});
				}
				m.tris.push_back({base, base + 2, base + 1}), m.tris.push_back({base, base + 3, base + 2});
			}
		return m;
	});
	return name.empty() ? std::string() : "AttributeBegin\n  " + coated({0.075, 0.06, 0.045}, 0.08) + "\n  Shape \"plymesh\" \"string filename\" [ \"" + name + "\" ]\nAttributeEnd\n";
}
// Mist lifting off the water round the camera: soft puffs (film_air.h) in cells over wherever `wet(x, z)` says there is water, drifting and fading.
template <class Wet>
inline std::string mistText(const std::string &film, const Mist &m, double t, double wind, const Vec3 &eye, Wet wet, double level) {
	if (!m.on || t < m.t0 || t > m.t1) return std::string();
	const double fade = smoothstep(m.t0, m.t0 + 3.0, t) * (1.0 - smoothstep(m.t1 - 3.0, m.t1, t)), cell = 6.0, dx = 0.25 * std::sin(wind * kPi / 180.0), dz = 0.25 * std::cos(wind * kPi / 180.0);
	std::vector<airborne::Puff> puffs;
	const int ix = static_cast<int>(std::floor(eye.x / cell)), iz = static_cast<int>(std::floor(eye.z / cell));
	for (int a = ix - 14; a <= ix + 14; ++a)
		for (int b = iz - 14; b <= iz + 14; ++b)
			for (int n = 0; n < 2; ++n) {
				const int k = static_cast<int>((static_cast<unsigned>(a) * 73856093u ^ static_cast<unsigned>(b) * 19349663u ^ static_cast<unsigned>(n) * 83492791u) & 0x7fffffffu);
				const double period = 22.0 + 16.0 * hash01(k, 1), phase = t / period + hash01(k, 2), u = phase - std::floor(phase);
				const double x = (a + hash01(k, 3)) * cell + dx * u * period, z = (b + hash01(k, 4)) * cell + dz * u * period;
				if (!wet(x, z)) continue;
				airborne::Puff p;
				p.at = {x, level + 0.15 + (0.3 + 1.3 * hash01(k, 5)) * u, z};
				p.size = 1.6 + 2.0 * hash01(k, 6) + 1.4 * u;
				p.opacity = 0.32 * m.amount * fade * std::sin(kPi * u);
				if (p.opacity > 0.01) puffs.push_back(p);
			}
	return puffs.empty() ? std::string() : "# mist off the water (film_bankside.h)\n" + airborne::puffsText(film, puffs, {0.92, 0.94, 0.96}, eye, "watermist");
}

}  // namespace bankside
}  // namespace cartoon
