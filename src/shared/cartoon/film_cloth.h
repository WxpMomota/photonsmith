#pragma once
// film_cloth.h -- cloth for film files (`cloth`): a flag on its pole, a character's cape, a sheet dropped over things and a curtain on its rail, each a sheet of cloth
// simulated as Blender's cloth modifier simulates it - points joined by springs that keep its weave's lengths (structure and shear) and resist folding (bending), pulled
// by gravity, blown by the wind (on each little triangle, by how squarely it meets the wind), held where it is held (pinned), and kept out of the ground, the character it
// hangs on and the boxes it falls over. Solved as position-based dynamics in small steps (Macklin et al., "Small Steps in Physics Simulation", 2019: one pass of the
// constraints in each of many short steps, stiff and stable), our own code.
//
//     cloth flag at 2 3 -4 size 1.2 0.8 colour red        # a flag flying from the top of a pole (the pole stands from the ground to 2 3 -4)
//     cloth cape on pip length 0.8 colour purple           # a cape from a character's shoulders, swinging as it walks
//     cloth sheet at 0 1.6 0 size 2 2 drop 3 over 0 0.4 0 1.2 0.8 0.8   # a sheet held flat, let go at 3 s, falling over a box (centre, size)
//     cloth curtain at 0 2.5 -3 size 2 2.2 facing 0 colour cream          # a curtain hanging from a rail, stirring in the wind
//
// A frame is a function of its time: the cloth is simulated from its start (a second before the film, so it has settled) in fixed steps up to the frame, and a process that
// renders frames in order keeps where it got to, so the next frame only takes the steps between. Coordinates are pbrt's (as a film file's).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "nature_lib.h"

namespace cartoon {
namespace cloth {

struct Box {
	Vec3 centre, size;   // (pbrt's; the whole size, not half)
};

struct Cloth {
	std::string kind;          // flag, cape, sheet, curtain; worn: skirt, coat, scarf
	std::string who;           // (a cape, a garment) who wears it
	Vec3 at;                   // a flag's pole top, a sheet's centre, a curtain's rail's middle
	double width = 1.2, height = 0.8;   // metres (a cape: across the shoulders and its length)
	double facing = 0.0;       // degrees (a curtain: the way it faces, 0 +z; a sheet: its turn)
	double pole = -1.0;        // (a flag) the pole's height below `at` (negative: down to the ground, y 0)
	Vec3 colour{0.78, 0.12, 0.10};
	double drop = 0.0;         // (a sheet) when it is let go
	double stiffness = 0.5;    // 0..1: how much it resists folding (0 silk, 1 canvas)
	double windScale = 1.0;    // how much the wind blows it
	double ground = 0.0;       // the ground's height under it
	double flare = 0.25;       // (a skirt, a coat) how far its hem stands out from where it is held, a share of its length
	std::vector<Box> over;     // (a sheet) what it falls over
	int line = 0;
};

inline const std::vector<std::string> &kinds() {
	static const std::vector<std::string> k = {"flag", "cape", "sheet", "curtain", "skirt", "coat", "scarf"};
	return k;
}
// Whether a kind is worn (held by a character: World::holdAt), and whether it closes round on itself (a skirt: its first and last columns are joined).
inline bool worn(const std::string &kind) { return kind == "cape" || kind == "skirt" || kind == "coat" || kind == "scarf"; }
inline bool ring(const std::string &kind) { return kind == "skirt"; }

// What the world does to the cloth at a moment: where its held points are (pbrt's: a cape's along the character's shoulders) and the round bodies it must stay out of.
struct Sphere {
	Vec3 centre;
	double radius = 0.0;
};
struct World {
	Vec3 wind;   // m/s (pbrt's)
	// (a cape) where its top edge is held at time t: `count` points from its left to its right (pbrt's), and the character's body as spheres; false: no such character
	std::function<bool(double t, int count, std::vector<Vec3> &held, std::vector<Sphere> &body)> holdAt;
};

// ---- the simulation ----

struct State {
	int nx = 0, ny = 0;           // points across and down
	std::vector<Vec3> x, prev;    // where each point is, and was a step ago
	std::vector<double> invMass;  // 0: held
	double t = 0.0;               // the time it has reached
	long step = 0;
};
struct Link {
	int a = 0, b = 0;
	double rest = 0.0, compliance = 0.0;
};

namespace detail {

constexpr double kStep = 1.0 / 300.0;   // seconds a step
constexpr int kPasses = 3;              // passes over the constraints in a step
constexpr double kStart = -1.0;         // (it starts a second before the film, to settle)

inline int pointsAlong(double metres) { return std::clamp(static_cast<int>(std::lround(metres / 0.07)) + 1, 6, 36); }

// The cloth's rest shape and its first place: a grid of points, row 0 the held edge (a flag's along its pole, a cape's along the shoulders, a curtain's along its rail).
inline State initial(const Cloth &c, const World &w) {
	State s;
	s.nx = pointsAlong(ring(c.kind) ? kPi * c.width : c.kind == "coat" ? 0.85 * kPi * c.width : c.width), s.ny = pointsAlong(c.height);
	if (c.kind == "flag") std::swap(s.nx, s.ny);   // (a flag: across is down the pole, down is out from it)
	s.x.resize(static_cast<std::size_t>(s.nx) * s.ny);
	s.invMass.assign(s.x.size(), 1.0);
	const double a = c.facing * kPi / 180.0;
	const Vec3 side{std::cos(a), 0.0, -std::sin(a)};   // (a curtain's rail, a sheet's width: across the way it faces)
	const Vec3 face{std::sin(a), 0.0, std::cos(a)};
	std::vector<Vec3> held;
	std::vector<Sphere> body;
	if (worn(c.kind) && w.holdAt) w.holdAt(kStart, s.nx, held, body);
	Vec3 mid{};
	for (const Vec3 &h : held) mid = mid + h * (1.0 / std::max<std::size_t>(1, held.size()));
	for (int j = 0; j < s.ny; ++j)
		for (int i = 0; i < s.nx; ++i) {
			const double u = static_cast<double>(i) / (s.nx - 1), v = static_cast<double>(j) / (s.ny - 1);
			Vec3 p;
			if (c.kind == "flag") {   // out from the pole along +x (the wind turns it), with a slight ripple (a sheet perfectly flat in the plane gravity pulls in never folds)
				p = c.at + Vec3{v * c.width, -u * c.height, 0.03 * v * std::sin(5.0 * u + 1.0)};
			} else if (c.kind == "cape" || c.kind == "scarf") {
				p = (held.empty() ? c.at : held[static_cast<std::size_t>(i)]) + Vec3{0.0, -v * c.height, 0.0};
			} else if (c.kind == "skirt" || c.kind == "coat") {   // (hanging from the waist, its hem standing out a little)
				const Vec3 top = held.empty() ? c.at : held[static_cast<std::size_t>(i)];
				Vec3 out = top - mid;
				out.y = 0.0;
				out = length(out) > 1e-9 ? normalize(out) : Vec3{};
				p = top + out * (c.flare * v * c.height) + Vec3{0.0, -v * c.height, 0.0};
			} else if (c.kind == "sheet") {
				p = c.at + side * ((u - 0.5) * c.width) + face * ((v - 0.5) * c.height);
			} else {   // a curtain
				p = c.at + side * ((u - 0.5) * c.width) + Vec3{0.0, -v * c.height, 0.0};
			}
			s.x[static_cast<std::size_t>(j * s.nx + i)] = p;
		}
	s.prev = s.x;
	for (int i = 0; i < s.nx; ++i) {   // the held edge
		if (c.kind == "flag" || worn(c.kind) || c.kind == "curtain") s.invMass[static_cast<std::size_t>(i)] = 0.0;
	}
	if (c.kind == "sheet")
		for (double &m : s.invMass) m = 0.0;   // (held flat until it is let go)
	s.t = kStart;
	return s;
}

inline std::vector<Link> linksOf(const Cloth &c, const State &s) {
	std::vector<Link> out;
	const double bend = std::pow(10.0, 1.0 - 4.0 * std::clamp(c.stiffness, 0.0, 1.0));   // (compliance: silk 10, canvas 0.001)
	const bool closed = ring(c.kind);
	const auto add = [&](int i0, int j0, int i1, int j1, double compliance) {
		if (closed) i0 = (i0 + s.nx) % s.nx, i1 = (i1 + s.nx) % s.nx;   // (round the ring)
		if (i0 >= s.nx || j0 >= s.ny || i1 < 0 || i1 >= s.nx || j1 < 0 || j1 >= s.ny) return;
		const int a = j0 * s.nx + i0, b = j1 * s.nx + i1;
		out.push_back({a, b, length(s.x[static_cast<std::size_t>(a)] - s.x[static_cast<std::size_t>(b)]), compliance});
	};
	for (int j = 0; j < s.ny; ++j)
		for (int i = 0; i < s.nx; ++i) {
			add(i, j, i + 1, j, 0.0), add(i, j, i, j + 1, 0.0);           // the weave
			add(i, j, i + 1, j + 1, 0.5), add(i + 1, j, i, j + 1, 0.5);    // shear (soft: cloth gives way across its weave, so it can fold)
			add(i, j, i + 2, j, bend), add(i, j, i, j + 2, bend);         // folding
		}
	return out;
}

// One step of dt: the forces, the constraints, the collisions.
inline void step(const Cloth &c, const World &w, const std::vector<Link> &links, State &s, double dt) {
	const double t = s.t + dt;
	if (c.kind == "sheet" && t >= c.drop && s.t < c.drop)
		for (double &m : s.invMass) m = 1.0;
	std::vector<Vec3> held;
	std::vector<Sphere> body;
	if (worn(c.kind) && w.holdAt && w.holdAt(t, s.nx, held, body))
		for (int i = 0; i < s.nx; ++i) s.x[static_cast<std::size_t>(i)] = held[static_cast<std::size_t>(i)];
	// the wind on each triangle (by how squarely it meets the air coming past it), gusting
	std::vector<Vec3> force(s.x.size());
	const double gust = 0.65 + 0.7 * nature::noise2(t * 0.9, 0.3, 4417 + c.line);
	const Vec3 wind = w.wind * (gust * c.windScale);
	if (length(wind) > 1e-6) {
		for (int j = 0; j + 1 < s.ny; ++j)
			for (int i = 0; i + 1 < s.nx; ++i) {
				const int q[4] = {j * s.nx + i, j * s.nx + i + 1, (j + 1) * s.nx + i + 1, (j + 1) * s.nx + i};
				for (int tri = 0; tri < 2; ++tri) {
					const int a = q[0], b = q[1 + tri], d = q[2 + tri];
					const Vec3 pa = s.x[static_cast<std::size_t>(a)], pb = s.x[static_cast<std::size_t>(b)], pd = s.x[static_cast<std::size_t>(d)];
					const Vec3 n2 = cross(pb - pa, pd - pa);   // (twice the area along the normal)
					const Vec3 vel = ((pa - s.prev[static_cast<std::size_t>(a)]) + (pb - s.prev[static_cast<std::size_t>(b)]) + (pd - s.prev[static_cast<std::size_t>(d)])) * (1.0 / (3.0 * dt));
					const Vec3 rel = wind - vel;
					const double flutter = 1.0 + 0.35 * (nature::noise2(t * 6.0 + i * 0.4, j * 0.4, 811 + c.line) - 0.5);
					const Vec3 f = normalize(n2) * (0.5 * 1.2 * 0.5 * dot(normalize(n2), rel) * length(rel) * length(n2) * flutter);   // (0.5 rho C A |v| v.n: air 1.2 kg/m3)
					for (int k : {a, b, d}) force[static_cast<std::size_t>(k)] = force[static_cast<std::size_t>(k)] + f * (1.0 / 3.0);
				}
			}
	}
	// the points move (Verlet, a little air drag), the cloth weighing 0.2 kg a square metre
	const double around = ring(c.kind) ? kPi * c.width : c.width;   // (a skirt's width is its waist's: its cloth goes round it)
	const double cellArea = (around / (s.nx - 1)) * (c.height / (s.ny - 1)), mass = std::max(1e-4, 0.2 * cellArea);
	for (std::size_t k = 0; k < s.x.size(); ++k) {
		if (s.invMass[k] == 0.0) {
			s.prev[k] = s.x[k];
			continue;
		}
		const Vec3 v = (s.x[k] - s.prev[k]) * 0.998;
		const Vec3 a = Vec3{0.0, -9.81, 0.0} + force[k] * (1.0 / mass);
		s.prev[k] = s.x[k];
		s.x[k] = s.x[k] + v + a * (dt * dt);
	}
	// the lengths (XPBD: a compliance of 0 is a hard link; the masses are all `mass`, so the compliance is scaled by it)
	for (int pass = 0; pass < kPasses; ++pass)
	for (const Link &l : links) {
		Vec3 &pa = s.x[static_cast<std::size_t>(l.a)], &pb = s.x[static_cast<std::size_t>(l.b)];
		const double wa = s.invMass[static_cast<std::size_t>(l.a)], wb = s.invMass[static_cast<std::size_t>(l.b)];
		if (wa + wb == 0.0) continue;
		const Vec3 d = pa - pb;
		const double len = length(d);
		if (len < 1e-9) continue;
		const double alpha = l.compliance * mass / (dt * dt * kPasses), lambda = -(len - l.rest) / ((wa + wb) + alpha);
		const Vec3 g = d * (lambda / len);
		pa = pa + g * wa, pb = pb - g * wb;
	}
	// out of the ground, the boxes and the character's body (with friction: a point that is pushed out keeps little of its sliding)
	const auto pushOut = [&](std::size_t k, Vec3 to) {
		const Vec3 slide = (to - s.prev[k]);
		s.x[k] = to;
		s.prev[k] = to - slide * 0.3;
	};
	for (std::size_t k = 0; k < s.x.size(); ++k) {
		if (s.invMass[k] == 0.0) continue;
		Vec3 p = s.x[k];
		const double skin = 0.01;
		if (p.y < c.ground + skin) pushOut(k, {p.x, c.ground + skin, p.z}), p = s.x[k];
		for (const Box &b : c.over) {
			const Vec3 h = b.size * 0.5 + Vec3{skin, skin, skin}, d = p - b.centre;
			if (std::fabs(d.x) >= h.x || std::fabs(d.y) >= h.y || std::fabs(d.z) >= h.z) continue;
			const double ox = h.x - std::fabs(d.x), oy = h.y - std::fabs(d.y), oz = h.z - std::fabs(d.z);   // (out through the nearest face)
			if (oy <= ox && oy <= oz) p.y = b.centre.y + (d.y < 0.0 ? -h.y : h.y);
			else if (ox <= oz) p.x = b.centre.x + (d.x < 0.0 ? -h.x : h.x);
			else p.z = b.centre.z + (d.z < 0.0 ? -h.z : h.z);
			pushOut(k, p);
		}
		for (const Sphere &sp : body) {
			const Vec3 d = p - sp.centre;
			const double r = sp.radius + skin, len = length(d);
			if (len < r && len > 1e-9) pushOut(k, sp.centre + d * (r / len)), p = s.x[k];
		}
	}
	s.t = t;
	++s.step;
}

// Where a cloth has got to, kept between the frames a process renders (keyed by the film and the cloth's line); a frame earlier than the kept one starts again.
struct Kept {
	std::mutex lock;
	std::map<std::string, std::shared_ptr<State>> states;
};
inline Kept &kept() {
	static Kept k;
	return k;
}

}  // namespace detail

// The cloth at film time t (simulated from its start, or on from where this process got to).
inline State simulate(const std::string &film, const Cloth &c, const World &w, double t) {
	char shape[200];
	std::snprintf(shape, sizeof shape, "#%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g", c.at.x, c.at.y, c.at.z, c.width, c.height, c.facing, c.drop, c.stiffness, c.windScale, c.flare,
				  c.ground);
	const std::string key = film + "#" + std::to_string(c.line) + "#" + c.kind + "#" + c.who + shape;   // (an edited cloth starts again)
	State s;
	{
		std::lock_guard<std::mutex> guard(detail::kept().lock);
		const auto it = detail::kept().states.find(key);
		if (it != detail::kept().states.end() && it->second->t <= t + 1e-9) s = *it->second;
	}
	if (s.x.empty()) s = detail::initial(c, w);
	const std::vector<Link> links = detail::linksOf(c, detail::initial(c, w));
	const long last = static_cast<long>(std::floor((t - detail::kStart) / detail::kStep + 1e-6));
	while (s.step < last) detail::step(c, w, links, s, detail::kStep);
	std::lock_guard<std::mutex> guard(detail::kept().lock);
	detail::kept().states[key] = std::make_shared<State>(s);
	return s;
}

// ---- the scene text ----

inline std::string text(const std::string &film, const Cloth &c, const World &w, double t) {
	const State s = simulate(film, c, w, t);
	std::string P, N, UV, I;
	char buf[128];
	// smooth normals: each point's from the triangles round it
	std::vector<Vec3> normal(s.x.size());
	const bool closed = ring(c.kind);
	for (int j = 0; j + 1 < s.ny; ++j)
		for (int i = 0; i + (closed ? 0 : 1) < s.nx; ++i) {
			const int a = j * s.nx + i, b = j * s.nx + (i + 1) % s.nx, d = a + s.nx, e = b + s.nx;
			const Vec3 n = cross(s.x[static_cast<std::size_t>(b)] - s.x[static_cast<std::size_t>(a)], s.x[static_cast<std::size_t>(d)] - s.x[static_cast<std::size_t>(a)]);
			for (int k : {a, b, d, e}) normal[static_cast<std::size_t>(k)] = normal[static_cast<std::size_t>(k)] + n;
			std::snprintf(buf, sizeof buf, "%d %d %d %d %d %d ", a, d, b, b, d, e);
			I += buf;
		}
	for (std::size_t k = 0; k < s.x.size(); ++k) {
		const Vec3 n = normalize(normal[k]);
		std::snprintf(buf, sizeof buf, "%.4f %.4f %.4f ", s.x[k].x, s.x[k].y, s.x[k].z);
		P += buf;
		std::snprintf(buf, sizeof buf, "%.4f %.4f %.4f ", n.x, n.y, n.z);
		N += buf;
		std::snprintf(buf, sizeof buf, "%.4f %.4f ", static_cast<double>(k % static_cast<std::size_t>(s.nx)) / (s.nx - 1), static_cast<double>(k / static_cast<std::size_t>(s.nx)) / (s.ny - 1));
		UV += buf;
	}
	std::string out = "# cloth: a " + c.kind + " (film_cloth.h)\nAttributeBegin\n";
	std::snprintf(buf, sizeof buf, "  Material \"diffuse\" \"rgb reflectance\" [ %.3f %.3f %.3f ]\n", c.colour.x, c.colour.y, c.colour.z);
	out += buf;
	out += "  Shape \"trianglemesh\" \"integer indices\" [ " + I + "] \"point3 P\" [ " + P + "] \"normal N\" [ " + N + "] \"point2 uv\" [ " + UV + "]\nAttributeEnd\n";
	if (c.kind == "flag") {   // its pole, and a ball on top
		const double bottom = c.pole >= 0.0 ? c.at.y - c.pole : c.ground;
		std::snprintf(buf, sizeof buf, "AttributeBegin\n  Material \"conductor\" \"float roughness\" [ 0.25 ]\n  Translate %.4f %.4f %.4f\n", c.at.x, bottom, c.at.z);
		out += buf;
		std::snprintf(buf, sizeof buf, "  Rotate -90 1 0 0\n  Shape \"cylinder\" \"float radius\" [ 0.025 ] \"float zmin\" [ 0 ] \"float zmax\" [ %.4f ]\n", c.at.y - bottom + 0.04);
		out += buf;
		std::snprintf(buf, sizeof buf, "  Translate 0 0 %.4f\n  Shape \"sphere\" \"float radius\" [ 0.045 ]\nAttributeEnd\n", c.at.y - bottom + 0.06);
		out += buf;
	}
	return out;
}

}  // namespace cloth
}  // namespace cartoon
