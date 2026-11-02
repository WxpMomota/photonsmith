#pragma once
// film_craft.h -- boats on the water (water round 2): `boat KIND NAME at X Z [to X Z]... [speed S] [from T] [length L] [colour R G B]`. Round 16's `boat X Z` is a hull seen from under
// the sea; these are seen from above it, with people in them.
//
//   kinds     rowboat (planked, thwarts, a pair of oars that pull when it moves), canoe, raft (logs lashed together), sailboat (cabin, mast, a mainsail and a jib that fill and swing to
//             the side the wind is not on, heeling her over), fishing (wheelhouse, mast, net drum, tyres down her side) and ferry (two decks, a funnel, rails).
//   riding    a hull does not follow every wave: the sea is read at bow, stern and both sides, smoothed over a third of her length, and those four heights say how she lifts, pitches
//             and rolls. Her floor lies above the water (the sea's mesh runs right through her), so no sea shows inside.
//   wake      her wake is rings (film_stir.h trail): the V and the waves inside it come from those; an engine churns froth behind her, and oars leave a ring at each catch.
//
// Coordinates: pbrt's in a film file; meshes in story space (x mirrored), a hull's own forward +z, its waterline at y 0.

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "cartoon_lib.h"
#include "film_stir.h"
#include "film_stream.h"
#include "film_water.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace craft {

struct Craft {
	std::string kind = "rowboat", name;
	std::vector<std::pair<double, double>> points;   // (pbrt's x z: one point, she lies there)
	double facing = 0.0, speed = 0.0, from = 0.0, length = 0.0;
	Vec3 colour{-1.0, 0.0, 0.0};
	int seed = 1;
	std::vector<Vec3> path;   // (worked out on load: x, metres along, z)
	int river = -1;           // (`river N`: down that river, `across` metres from its middle, carried by it and going at `speed` on top)
	double across = 0.0;
};
// What a boat floats on: the film's sea or lake, or its rivers.
struct Afloat {
	const water::Sea *sea = nullptr;
	const std::vector<water::River> *rivers = nullptr;
};
inline const std::vector<std::string> &kinds() {
	static const std::vector<std::string> k = {"rowboat", "canoe", "raft", "sailboat", "fishing", "ferry"};
	return k;
}

// A hull's measures. `free`: how high her side stands out of the water at the middle; `floor`: the height of what is inside; `round`: how flat her bottom is (2 a U, 4 a box); `stern`: her
// transom's width to her beam (0 pointed, as a canoe).
struct Shape {
	double length, beam, draft, free, floor, round, stern, speed;
	Vec3 paint;
	bool decked;
};
inline Shape shapeOf(const std::string &kind, double length, Vec3 colour) {
	Shape h{3.4, 1.25, 0.16, 0.42, 0.1, 2.2, 0.62, 1.2, {0.78, 0.2, 0.14}, false};
	if (kind == "canoe") h = {4.6, 0.85, 0.11, 0.3, 0.08, 2.0, 0.0, 1.4, {0.13, 0.4, 0.5}, false};
	else if (kind == "raft") h = {4.0, 2.2, 0.08, 0.18, 0.18, 2.0, 1.0, 0.7, {0.33, 0.22, 0.12}, true};
	else if (kind == "sailboat") h = {7.5, 2.5, 0.45, 0.72, 0.72, 3.0, 0.75, 2.6, {0.92, 0.92, 0.88}, true};
	else if (kind == "fishing") h = {10.0, 3.4, 0.9, 1.3, 1.3, 3.0, 0.8, 3.2, {0.14, 0.33, 0.5}, true};
	else if (kind == "ferry") h = {28.0, 9.0, 1.4, 2.2, 2.2, 4.0, 0.85, 5.0, {0.9, 0.9, 0.85}, true};
	if (length > 0.0) {
		const double f = length / h.length;
		h.length *= f, h.beam *= f, h.draft *= f, h.free *= f, h.floor *= f, h.speed *= std::sqrt(f);
	}
	if (colour.x >= 0.0) h.paint = colour;
	return h;
}

// A frame with its +z along `f`.
inline Mat4 sealifeFacing(Vec3 f) {
	f = normalize(f);
	Vec3 right = cross(Vec3{0.0, 1.0, 0.0}, f);
	right = length(right) > 1e-9 ? normalize(right) : Vec3{1.0, 0.0, 0.0};
	const Vec3 up = cross(f, right);
	return {right.x, up.x, f.x, 0.0, right.y, up.y, f.y, 0.0, right.z, up.z, f.z, 0.0, 0.0, 0.0, 0.0, 1.0};
}
namespace detail {
inline Vec3 story(Vec3 p) { return {-p.x, p.y, p.z}; }
inline void add(Mesh &into, const Mesh &m, const Mat4 &place = identity()) {
	const int base = static_cast<int>(into.verts.size());
	const Mat4 inv = transposeRotation(invertAffine(place));
	for (std::size_t i = 0; i < m.verts.size(); ++i) {
		into.verts.push_back(transformPoint(place, m.verts[i]));
		if (i < m.normals.size()) {
			const Vec3 n = m.normals[i];
			into.normals.push_back(normalize(Vec3{inv[0] * n.x + inv[1] * n.y + inv[2] * n.z, inv[4] * n.x + inv[5] * n.y + inv[6] * n.z, inv[8] * n.x + inv[9] * n.y + inv[10] * n.z}));
		}
	}
	for (const auto &t : m.tris) into.tris.push_back({t[0] + base, t[1] + base, t[2] + base});
}
inline void tube(Mesh &into, const std::vector<Vec3> &pts, double r, int sides = 6) { add(into, sdf::tubeMesh(pts, r, 1.0, sides)); }
inline void bar(Mesh &into, Vec3 a, Vec3 b, double r) { tube(into, {a, lerp(a, b, 0.5), b}, r); }
inline void boxAt(Mesh &into, Vec3 centre, double sx, double sy, double sz) { add(into, box(sx, sy, sz), translate(centre)); }
inline void roundBoxAt(Mesh &into, Vec3 centre, double sx, double sy, double sz, double r) { add(into, roundedBox(sx, sy, sz, r, 2), translate(centre)); }

// How wide she is at s (-0.5 stern to 0.5 bow), to her beam.
inline double widthAt(const Shape &h, double s) {
	const double fore = s > 0.0 ? std::cos(0.5 * kPi * std::pow(s / 0.5, 1.7)) : 1.0;
	const double aft = s < 0.0 ? lerp(1.0, h.stern, std::pow(-s / 0.5, 2.2)) : 1.0;
	const double back = s >= 0.0 ? 1.0 : h.stern > 0.0 ? aft : std::cos(0.5 * kPi * std::pow(std::min(1.0, -s / 0.5), 1.7));
	return std::max(0.003, fore * back);
}
inline double keelAt(const Shape &h, double s) { return h.draft * (1.0 - 0.75 * std::pow(std::abs(2.0 * s), 4.0)); }
inline double sheerAt(const Shape &h, double s) { return h.free * (1.0 + 0.55 * std::pow(std::max(0.0, 2.0 * s), 2.0) + 0.14 * std::pow(std::min(0.0, 2.0 * s), 2.0) + (h.stern == 0.0 ? 0.5 * std::pow(std::min(0.0, 2.0 * s), 4.0) : 0.0)); }
// The point of her skin at s, q (q -1 at one rail, 0 at the keel, 1 at the other).
inline Vec3 skinAt(const Shape &h, double s, double q, double inset = 0.0) {
	const double b = std::max(0.002, 0.5 * h.beam * widthAt(h, s) - inset), k = keelAt(h, s), sh = sheerAt(h, s);
	return {b * q, -k + (k + sh) * std::pow(std::abs(q), h.round), s * h.length};
}
// The q at which her skin is at height y.
inline double qAt(const Shape &h, double s, double y) {
	const double k = keelAt(h, s), sh = sheerAt(h, s);
	return std::pow(std::clamp((y + k) / (k + sh), 0.0, 1.0), 1.0 / h.round);
}
inline Mesh skin(const Shape &h, bool inner) {
	Mesh m;
	const int nz = 44, na = 20, w = na + 1;
	const double wall = inner ? 0.018 * h.length / std::max(1.0, h.length / 4.0) : 0.0;
	for (int j = 0; j <= nz; ++j) {
		const double s = -0.5 + j / double(nz);
		for (int i = 0; i <= na; ++i) {
			Vec3 p = skinAt(h, s, -1.0 + 2.0 * i / na, wall);
			if (inner) p.y = std::max(p.y, h.floor);
			m.verts.push_back(p);
		}
	}
	for (int j = 0; j < nz; ++j)
		for (int i = 0; i < na; ++i) {
			const int a = j * w + i;
			if (inner) m.tris.push_back({a, a + 1, a + w}), m.tris.push_back({a + 1, a + w + 1, a + w});
			else m.tris.push_back({a, a + w, a + 1}), m.tris.push_back({a + 1, a + w, a + w + 1});
		}
	sdf::computeNormals(m);
	if (h.stern > 0.0) {   // her transom: a flat stern
		const int base = static_cast<int>(m.verts.size());
		m.verts.push_back({0.0, 0.4 * sheerAt(h, -0.5), -0.5 * h.length});
		m.normals.push_back({0.0, 0.0, -1.0});
		for (int i = 0; i <= na; ++i) m.verts.push_back(skinAt(h, -0.5, -1.0 + 2.0 * i / na)), m.normals.push_back({0.0, 0.0, -1.0});
		for (int i = 0; i < na; ++i) m.tris.push_back({base, base + i + 2, base + i + 1});
	}
	return m;
}
// A line along her skin: the rail (y < 0: at q) or a painted line at height y.
inline std::vector<Vec3> lineAlong(const Shape &h, double q, double y, double out) {
	std::vector<Vec3> pts;
	for (int j = 0; j <= 28; ++j) {
		const double s = -0.5 + j / 28.0, sign = q < 0.0 ? -1.0 : 1.0;
		Vec3 p = skinAt(h, s, y < 1e8 ? sign * qAt(h, s, y) : q);
		p.x += sign * out * widthAt(h, s);
		pts.push_back(p);
	}
	return pts;
}
// A deck over a decked hull, at its rail.
inline Mesh deckOf(const Shape &h) {
	Mesh m;
	const int nz = 32;
	for (int j = 0; j <= nz; ++j) {
		const double s = -0.5 + j / double(nz);
		const Vec3 l = skinAt(h, s, -1.0), r = skinAt(h, s, 1.0);
		m.verts.push_back(l), m.verts.push_back(r);
		m.normals.push_back({0.0, 1.0, 0.0}), m.normals.push_back({0.0, 1.0, 0.0});
	}
	for (int j = 0; j < nz; ++j) {
		const int a = 2 * j;
		m.tris.push_back({a, a + 1, a + 2}), m.tris.push_back({a + 1, a + 3, a + 2});
	}
	return m;
}
}  // namespace detail

// ---- where she is ----
// Lays her way through her points (Catmull-Rom, a point about every half metre; y is metres along it).
inline void prepare(Craft &c) {
	c.path.clear();
	const std::size_t n = c.points.size();
	if (n == 0) return;
	if (n == 1) {
		c.path.push_back({c.points[0].first, 0.0, c.points[0].second});
		return;
	}
	double along = 0.0;
	Vec3 last{c.points[0].first, 0.0, c.points[0].second};
	c.path.push_back(last);
	for (std::size_t i = 0; i + 1 < n; ++i) {
		const auto at = [&](std::size_t k) { return Vec3{c.points[k].first, 0.0, c.points[k].second}; };
		const Vec3 p0 = at(i > 0 ? i - 1 : i), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2 < n ? i + 2 : i + 1);
		const int steps = static_cast<int>(std::clamp(2.0 * length(p2 - p1), 2.0, 20000.0));   // (a point a world away: not billions of steps)
		for (int k = 1; k <= steps; ++k) {
			const double u = k / double(steps), u2 = u * u, u3 = u2 * u;
			Vec3 p = (p1 * 2.0 + (p2 - p0) * u + (p0 * 2.0 - p1 * 5.0 + p2 * 4.0 - p3) * u2 + (p3 - p0 + p1 * 3.0 - p2 * 3.0) * u3) * 0.5;
			p.y = 0.0;
			along += length(p - Vec3{last.x, 0.0, last.z});
			last = p;
			p.y = along;
			c.path.push_back(p);
		}
	}
}
// Her way down a river.
inline void downRiver(Craft &c, const water::River &r) {
	c.points.clear();
	if (r.along.empty()) return;
	for (double s = 0.0; s < r.along.back(); s += 5.0) {
		const stream::Place p = stream::placeAt(r, s);
		const Vec3 q = p.at + p.side * c.across;
		c.points.push_back({q.x, q.z});
	}
	prepare(c);
	c.speed = (c.speed > 0.0 ? c.speed : 0.0) + 0.9 * r.speed;
}
inline double totalLength(const Craft &c) { return c.path.empty() ? 0.0 : c.path.back().y; }
// How far along her way she is at t (nature's): she gathers way over 3 s and loses it over her last 4 s.
inline double alongAt(const Craft &c, double U, double t) {
	const double L = totalLength(c);
	if (L <= 0.0 || U <= 0.0) return 0.0;
	const double u = std::max(0.0, t - c.from), ramp = 3.0, s = u < ramp ? 0.5 * U * u * u / ramp : U * (u - 0.5 * ramp);
	const double stop = 2.0 * U;   // (the last stretch, slowing to rest)
	return s < L - stop ? s : L - stop * std::exp(-(s - (L - stop)) / stop);
}
inline Vec3 placeAt(const Craft &c, double along, Vec3 *ahead = nullptr) {
	if (c.path.size() < 2) {
		const Vec3 p = c.path.empty() ? Vec3{} : c.path[0];
		if (ahead) *ahead = {std::sin(c.facing * kPi / 180.0), 0.0, std::cos(c.facing * kPi / 180.0)};
		return {p.x, 0.0, p.z};
	}
	const auto it = std::lower_bound(c.path.begin(), c.path.end(), along, [](const Vec3 &p, double v) { return p.y < v; });
	const std::size_t b = std::clamp<std::size_t>(static_cast<std::size_t>(it - c.path.begin()), 1, c.path.size() - 1), a = b - 1;
	const double u = std::clamp((along - c.path[a].y) / std::max(1e-9, c.path[b].y - c.path[a].y), 0.0, 1.0);
	if (ahead) {   // (a little ahead, so that she turns before she gets there)
		const auto far_ = std::lower_bound(c.path.begin(), c.path.end(), along + 1.0, [](const Vec3 &p, double v) { return p.y < v; });
		const Vec3 q = far_ == c.path.end() ? c.path.back() : *far_, p = lerp(c.path[a], c.path[b], u);
		const Vec3 d{q.x - p.x, 0.0, q.z - p.z};
		*ahead = length(d) > 1e-6 ? normalize(d) : normalize(Vec3{c.path[b].x - c.path[a].x, 0.0, c.path[b].z - c.path[a].z});
	}
	return {lerp(c.path[a].x, c.path[b].x, u), 0.0, lerp(c.path[a].z, c.path[b].z, u)};
}
// How she rides at t: where her middle is, her heading, and her pitch and roll in degrees, from the water under her. (A roll turns her frame's +x - port, as frameOf's story-space
// facing puts it - up: so it is port's water over starboard's.)
struct Riding {
	Vec3 at, ahead{0.0, 0.0, 1.0};
	double pitch = 0.0, roll = 0.0, way = 0.0;   // (way: metres a second)
};
inline Riding ridingAt(const Craft &c, const Shape &h, const Afloat &water, double t) {
	Riding r;
	const double U = c.speed > 0.0 ? c.speed : h.speed, along = alongAt(c, U, t);
	r.at = placeAt(c, along, &r.ahead);
	r.way = (alongAt(c, U, t + 0.1) - along) / 0.1;
	const Vec3 side{r.ahead.z, 0.0, -r.ahead.x};
	const double L = 0.42 * h.length, B = 0.42 * h.beam, width = 0.33 * h.length;
	const auto heightAt = [&](Vec3 p) {
		if (c.river >= 0 && water.rivers) return stream::heightAt((*water.rivers)[static_cast<std::size_t>(c.river)], p.x, p.z, t);
		Vec3 q, n;
		double fold = 0.0;
		water::seaAt(*water.sea, p.x, p.z, t, 0.0, q, n, fold, width);
		return q.y;
	};
	const double bow = heightAt(r.at + r.ahead * L), stern = heightAt(r.at - r.ahead * L), port = heightAt(r.at - side * B), star = heightAt(r.at + side * B);
	r.at.y = 0.25 * (bow + stern + port + star) + 0.02 * h.draft * std::sin(1.7 * t + c.seed);
	r.pitch = std::atan2(stern - bow, 2.0 * L) * 57.29578 + 0.6 * std::sin(0.9 * t + c.seed);
	r.roll = std::atan2(port - star, 2.0 * B) * 57.29578 +(h.length < 6.0 ? 1.4 : 0.7) * std::sin(0.73 * t + 2.0 * c.seed);
	return r;
}
// The hull's frame at a riding (story space).
inline Mat4 frameOf(const Riding &r, double heel = 0.0) {
	return mul(translate(detail::story(r.at)), sealifeFacing(detail::story(r.ahead)), rotate({0.0, 0.0, 1.0}, r.roll + heel), rotate({1.0, 0.0, 0.0}, r.pitch));
}

// ---- how she looks ----
// An oar in her own frame: its pin in the rowlock, the way it points, and so its blade and its handle. Stroke: 0 the blade going in at the far end of the reach; through pi it is pulled
// through the water, then feathered back above it.
struct Oar {
	Vec3 pivot, tip, handle;
	bool wet;
};
inline Oar oarAt(const Shape &h, int side, double stroke, bool pulling) {
	const double through = std::sin(stroke);
	const bool wet = pulling && through > 0.0;
	const double swing = pulling ? 34.0 * std::cos(stroke) : -6.0;   // (degrees towards her bow)
	const double reach = 1.47 * h.beam, inboard = 0.38 * reach, y = h.free + 0.06;
	const Vec3 pivot{side * 0.46 * h.beam, y, 0.02 * h.length};
	const double a = swing * kPi / 180.0, drop = (y + (wet ? 0.03 : -0.12 - 0.05 * std::abs(through))) / reach;
	const Vec3 dir = normalize(Vec3{side * std::cos(a), -drop, std::sin(a)});
	return {pivot, pivot + dir * reach, pivot - dir * inboard, wet};
}
inline std::string oarsText(const Shape &h, const Mat4 &frame, double stroke, bool pulling) {
	Mesh wood;
	for (int side = -1; side <= 1; side += 2) {
		const Oar o = oarAt(h, side, stroke, pulling);
		detail::tube(wood, {o.handle, o.pivot, lerp(o.pivot, o.tip, 0.8), o.tip}, 0.016, 6);
		const Vec3 along = normalize(o.tip - o.pivot);
		detail::add(wood, box(0.012, 0.11, 0.42), mul(translate(o.tip), sealifeFacing(along), rotate({0.0, 0.0, 1.0}, o.wet ? 0.0 : 80.0)));
		detail::tube(wood, {o.pivot + Vec3{0.0, -0.07, 0.0}, o.pivot + Vec3{0.0, 0.06, 0.0}}, 0.012, 5);
	}
	return attr(coated({0.55, 0.38, 0.2}, 0.3), emitMesh(wood, frame));
}
// A paddle in a canoe's own frame, held by whoever sits at `seat` (z along her): it dips on one side, then the other, one stroke in two.
struct Paddle {
	Vec3 top, lower, blade;
	bool wet;
};
inline Paddle paddleAt(const Shape &h, double seatZ, double stroke) {
	const int side = std::sin(0.5 * stroke) > 0.0 ? 1 : -1;
	const double through = std::cos(stroke), wet = std::sin(stroke);
	const double out = 0.5 * h.beam + 0.1, z = seatZ + 0.4 * through + 0.2;
	const Vec3 blade{side * out, wet > 0.0 ? -0.06 : 0.14, z - 0.1};
	const Vec3 top{side * 0.1, 0.92, z + 0.2};
	return {top, lerp(top, blade, 0.5), blade, wet > 0.0};
}
inline std::string paddleText(const Shape &h, const Mat4 &frame, double seatZ, double stroke) {
	const Paddle p = paddleAt(h, seatZ, stroke);
	Mesh wood;
	detail::tube(wood, {p.top + (p.top - p.blade) * 0.05, p.lower, p.blade}, 0.016, 6);
	detail::add(wood, box(0.03, 0.42, 0.17), alignY(p.blade + normalize(p.blade - p.top) * 0.16, p.blade - p.top));
	return attr(coated({0.6, 0.42, 0.22}, 0.3), emitMesh(wood, frame));
}
// Where an oar's blade is in her own frame, on `side` (-1, 1), at the catch of a stroke.
inline Vec3 catchOf(const Shape &h, int side) { return oarAt(h, side, 0.02, true).tip; }
constexpr double kStrokeRate = 0.42;   // (strokes a second)
inline double strokeAt(double t) { return 2.0 * kPi * kStrokeRate * t; }

// Who sits where, in her own frame: the place under them (on her floor), how high what they sit on is above that (0: they stand), which way they face (1 ahead, -1 astern).
struct Seat {
	Vec3 at;
	double height, faces;
};
inline Seat seatOf(const Shape &h, const std::string &kind, int n, bool rows) {
	if (kind == "rowboat") {
		const double z[3] = {0.04, -0.22, 0.3};
		const int k = std::clamp(n, 0, 2);
		return {{0.0, h.floor, z[k] * h.length - (k == 0 && rows ? 0.05 : 0.0)}, 0.29 * h.length / 3.4 - h.floor + 0.02, rows ? -1.0 : 1.0};
	}
	if (kind == "canoe") return {{0.0, h.floor, (n == 0 ? -0.24 : 0.26) * h.length}, 0.14, 1.0};
	if (kind == "raft") return {{0.5 * (n % 2 == 0 ? -1.0 : 1.0) * (n / 2 + 1) * 0.4, h.floor + 0.05, -0.1 * h.length * (n % 3)}, 0.0, 1.0};
	if (kind == "sailboat") return {{(n % 2 == 0 ? -0.2 : 0.2) * h.beam, h.free, (-0.34 - 0.04 * (n / 2)) * h.length}, 0.0, 1.0};
	return {{(n % 2 == 0 ? -0.3 : 0.3) * h.beam, h.free, (-0.2 - 0.05 * (n / 2)) * h.length}, 0.0, 1.0};
}

// How she sails: which way her boom swings and how far she heels, from where the wind (`wind`, degrees, the way it blows) is on her.
struct Sails {
	double side = 1.0, heel = 0.0, boom = 25.0;
};
inline Sails sailsOf(const Riding &r, double wind) {
	const Vec3 w{std::sin(wind * kPi / 180.0), 0.0, std::cos(wind * kPi / 180.0)}, right = normalize(cross(Vec3{0.0, 1.0, 0.0}, detail::story(r.ahead)));
	const double across = dot(detail::story(w), right);
	Sails s;
	s.side = across >= 0.0 ? 1.0 : -1.0;   // (blown out on the side the wind goes to)
	s.heel = -s.side * (5.0 + 9.0 * std::abs(across));
	s.boom = 12.0 + 40.0 * (0.5 + 0.5 * dot(w, r.ahead));   // (let right out before the wind, hauled in close to it)
	return s;
}
// The frame an offset along `f` is turned to (as the sea's animals').
inline Mesh sailMesh(Vec3 tack, Vec3 clew, Vec3 head, double camber, double t) {
	Mesh m;
	const int nu = 16, nv = 14;
	const Vec3 foot = clew - tack, luff = head - tack, out = normalize(cross(foot, luff));
	for (int j = 0; j <= nv; ++j)
		for (int i = 0; i <= nu; ++i) {
			const double v = j / double(nv), u = i / double(nu) * (1.0 - v);
			const double belly = camber * std::sin(kPi * std::min(1.0, u / std::max(0.05, 1.0 - v))) * (1.0 - 0.75 * v), flutter = 0.015 * u * std::sin(9.0 * u + 7.0 * v + 3.1 * t);
			m.verts.push_back(tack + foot * u + luff * v + out * (belly + flutter));
		}
	for (int j = 0; j < nv; ++j)
		for (int i = 0; i < nu; ++i) {
			const int a = j * (nu + 1) + i;
			m.tris.push_back({a, a + 1, a + nu + 1}), m.tris.push_back({a + 1, a + nu + 2, a + nu + 1});
		}
	sdf::computeNormals(m);
	return m;
}

// A boat at t (nature's) as scene text.
// `paddlers`: the z along her of each who paddles.
inline std::string craftText(const Craft &c, const Afloat &water, double t, bool rowed = false, const std::vector<double> &paddlers = {}) {
	const Shape h = shapeOf(c.kind, c.length, c.colour);
	const Riding r = ridingAt(c, h, water, t);
	const Sails sails = sailsOf(r, water.sea ? water.sea->wind : 0.0);
	const double heel = c.kind == "sailboat" ? sails.heel * std::clamp(r.way / std::max(0.3, h.speed), 0.3, 1.0) : 0.0;
	const Mat4 frame = frameOf(r, heel);
	const double S = h.length / shapeOf(c.kind, 0.0, c.colour).length;   // (her size to the usual)
	std::string out = "# a boat, " + c.kind + " (film_craft.h)\n";
	Mesh paint, trim, wood, dark, cloth, glass, metal;
	if (c.kind == "raft") {
		for (int i = -3; i <= 3; ++i) detail::add(wood, capsule(0.15 * S + 0.012 * S * ((i + 7) % 3), h.length * (0.92 + 0.02 * ((i + 9) % 4)), 10, 3), mul(translate(0.31 * S * i, 0.03, 0.04 * ((i + 7) % 3) * S), rotate({1.0, 0.0, 0.0}, 90.0), translate(0.0, -0.46 * h.length, 0.0)));
		for (const double z : {-0.34, 0.0, 0.34}) {
			detail::boxAt(dark, {0.0, 0.2 * S, z * h.length}, 2.25 * S, 0.05 * S, 0.1 * S);
			for (int i = -3; i <= 3; i += 2) detail::add(dark, torus(0.17 * S, 0.012 * S, 12, 5), mul(translate(0.31 * S * i, 0.05, z * h.length), rotate({1.0, 0.0, 0.0}, 0.0), rotate({0.0, 0.0, 1.0}, 90.0)));
		}
		out += attr(coated({0.34, 0.23, 0.12}, 0.5), emitMesh(wood, frame)) + attr(diffuseMaterial({0.23, 0.17, 0.1}), emitMesh(dark, frame));
		return out;
	}
	detail::add(paint, detail::skin(h, false));
	if (h.decked) detail::add(wood, detail::deckOf(h));
	else detail::add(wood, detail::skin(h, true));
	detail::tube(trim, detail::lineAlong(h, 1.0, 1e9, 0.0), 0.022 * std::cbrt(h.length));
	detail::tube(trim, detail::lineAlong(h, -1.0, 1e9, 0.0), 0.022 * std::cbrt(h.length));
	detail::tube(dark, detail::lineAlong(h, 1.0, 0.012 * h.length, 0.002), 0.006 * std::cbrt(h.length));
	detail::tube(dark, detail::lineAlong(h, -1.0, 0.012 * h.length, 0.002), 0.006 * std::cbrt(h.length));
	if (c.kind == "rowboat") {
		for (const double y : {0.14, 0.24, 0.33})   // her planks' lines
			for (const double side : {-1.0, 1.0}) detail::tube(dark, detail::lineAlong(h, side, y * S, 0.003), 0.0045, 4);
		for (const double z : {-0.22, 0.04, 0.3}) {   // thwarts
			const double b = 0.5 * h.beam * detail::widthAt(h, z);
			detail::boxAt(wood, {0.0, 0.29 * S, z * h.length}, 2.0 * b * 0.97, 0.03 * S, 0.2 * S);
		}
	} else if (c.kind == "canoe") {
		for (const double z : {-0.24, 0.26}) detail::boxAt(wood, {0.0, 0.2 * S, z * h.length}, h.beam * 0.72 * detail::widthAt(h, z), 0.02 * S, 0.16 * S);
		for (const double z : {-0.34, -0.1, 0.12, 0.36}) detail::boxAt(wood, {0.0, 0.3 * S, z * h.length}, h.beam * 0.95 * detail::widthAt(h, z), 0.015 * S, 0.04 * S);
	} else if (c.kind == "sailboat") {
		detail::roundBoxAt(trim, {0.0, h.free + 0.27, -0.02 * h.length}, 0.56 * h.beam, 0.55, 0.36 * h.length, 0.12);
		detail::boxAt(dark, {0.0, h.free + 0.4, -0.02 * h.length}, 0.57 * h.beam, 0.14, 0.26 * h.length);
		const double mast = 1.3 * h.length, foot = h.free + 0.5, z0 = 0.08 * h.length;
		const Vec3 tack{0.0, foot + 0.35, z0 - 0.04}, head{0.0, foot + mast * 0.95, z0 - 0.02};
		detail::bar(metal, {0.0, foot, z0}, {0.0, foot + mast, z0}, 0.05 * S);
		const double boom = sails.boom * kPi / 180.0;
		const Vec3 clew = tack + Vec3{sails.side * std::sin(boom), 0.0, -std::cos(boom)} * (0.46 * h.length);
		detail::bar(metal, tack, clew, 0.035 * S);
		detail::add(cloth, sailMesh(tack + Vec3{0.0, 0.05, 0.0}, clew + Vec3{0.0, 0.05, 0.0}, head, sails.side * 0.09 * h.length * 0.5, t));
		const Vec3 fore{0.0, h.free * 1.3, 0.49 * h.length};
		const Vec3 jibClew = Vec3{sails.side * 0.3 * h.beam, h.free + 0.35, z0 - 0.1};
		detail::add(cloth, sailMesh(fore + Vec3{0.0, 0.2, -0.05}, jibClew, Vec3{0.0, foot + mast * 0.8, z0 + 0.06}, -sails.side * 0.05 * h.length, t + 1.0));
		detail::bar(dark, fore, {0.0, foot + mast * 0.88, z0}, 0.012 * S);
		detail::bar(dark, {0.0, h.free, -0.5 * h.length}, {0.0, foot + mast, z0}, 0.01 * S);
	} else if (c.kind == "fishing") {
		detail::roundBoxAt(trim, {0.0, h.free + 1.05, 0.14 * h.length}, 0.5 * h.beam, 2.1, 0.26 * h.length, 0.08);
		detail::boxAt(glass, {0.0, h.free + 1.55, 0.14 * h.length}, 0.51 * h.beam, 0.62, 0.27 * h.length);
		detail::boxAt(paint, {0.0, h.free + 2.16, 0.14 * h.length}, 0.58 * h.beam, 0.1, 0.32 * h.length);
		detail::bar(dark, {0.0, h.free + 2.1, 0.08 * h.length}, {0.0, h.free + 6.0, 0.08 * h.length}, 0.08);
		detail::bar(dark, {0.0, h.free + 4.4, 0.08 * h.length}, {0.0, h.free + 2.0, -0.36 * h.length}, 0.05);
		detail::bar(dark, {0.0, h.free + 5.8, 0.08 * h.length}, {0.0, h.free, 0.48 * h.length}, 0.015);
		detail::add(metal, capsule(0.5, 1.7, 14, 3), mul(translate(0.85, h.free + 0.55, -0.26 * h.length), rotate({0.0, 0.0, 1.0}, 90.0)));
		for (int k = 0; k < 4; ++k)
			for (const double side : {-1.0, 1.0}) {
				const double s = -0.28 + 0.17 * k, b = 0.5 * h.beam * detail::widthAt(h, s) + 0.05;
				detail::add(dark, torus(0.3, 0.09, 16, 6), mul(translate(side * b, 0.7 * h.free, s * h.length), rotate({0.0, 0.0, 1.0}, 90.0 + 10.0 * side)));
			}
	} else if (c.kind == "ferry") {
		const double deck = h.free, L = h.length, B = h.beam;
		detail::roundBoxAt(trim, {0.0, deck + 1.3, -0.04 * L}, 0.84 * B, 2.6, 0.66 * L, 0.15);
		detail::roundBoxAt(trim, {0.0, deck + 3.7, -0.02 * L}, 0.68 * B, 2.2, 0.4 * L, 0.15);
		detail::boxAt(glass, {0.0, deck + 1.55, -0.04 * L}, 0.845 * B, 1.0, 0.6 * L);
		detail::boxAt(glass, {0.0, deck + 3.8, -0.02 * L}, 0.685 * B, 0.95, 0.36 * L);
		detail::boxAt(paint, {0.0, deck + 4.9, -0.02 * L}, 0.74 * B, 0.12, 0.43 * L);
		Mesh funnel = lathe({{0.8, 0.0}, {0.74, 2.6}, {0.9, 3.0}}, 20);
		detail::add(dark, funnel, translate(0.0, deck + 5.0, -0.1 * L));
		for (const double side : {-1.0, 1.0}) {
			detail::tube(metal, detail::lineAlong(h, side, 1e9, 0.0), 0.03);
			for (int k = 0; k < 18; ++k) {
				const double s = -0.46 + 0.054 * k;
				const Vec3 p = detail::skinAt(h, s, side);
				detail::bar(metal, p, p + Vec3{0.0, 1.0, 0.0}, 0.025);
			}
			std::vector<Vec3> top = detail::lineAlong(h, side, 1e9, 0.0);
			for (Vec3 &p : top) p.y += 1.0;
			detail::tube(metal, top, 0.03);
		}
	}
	out += attr(coated(h.paint, 0.18), emitMesh(paint, frame));
	if (!wood.tris.empty()) out += attr(coated({0.62, 0.44, 0.24}, 0.35), emitMesh(wood, frame));
	if (!trim.tris.empty()) out += attr(coated({0.93, 0.91, 0.84}, 0.3), emitMesh(trim, frame));
	if (!dark.tris.empty()) out += attr(diffuseMaterial({0.09, 0.08, 0.08}), emitMesh(dark, frame));
	if (!cloth.tris.empty()) out += attr(diffuseMaterial(c.colour.x >= 0.0 && c.kind == "sailboat" ? Vec3{0.95, 0.93, 0.86} : Vec3{0.93, 0.9, 0.82}), emitMesh(cloth, frame));
	if (!glass.tris.empty()) out += attr(coated({0.05, 0.08, 0.11}, 0.04), emitMesh(glass, frame));
	if (!metal.tris.empty()) out += attr(metalMaterial({0.72, 0.72, 0.7}, 0.25), emitMesh(metal, frame));
	if (c.kind == "rowboat" && (rowed || r.way > 0.05)) out += oarsText(h, frame, strokeAt(t), r.way > 0.05);
	for (const double z : paddlers) out += paddleText(h, frame, z, r.way > 0.05 ? strokeAt(t + 0.3 * z) : 0.0);
	return out;
}

// What she does to the water by t: her wake, her engine's froth, her oars' rings.
inline void stirred(stir::Field &f, const Craft &c, const Afloat &water, double t) {
	if (c.path.size() < 2) return;
	const Shape h = shapeOf(c.kind, c.length, c.colour);
	const double U = c.speed > 0.0 ? c.speed : h.speed;
	const auto where = [&c, U](double u) { return placeAt(c, alongAt(c, U, u)); };
	const double every = std::clamp(0.4 * U / stir::kG, 0.1, 0.4);
	stir::trail(f, where, c.from, 1e30, t, U, 0.12 * h.length, 0.0016 * h.length, every);
	if (c.kind == "fishing" || c.kind == "ferry" || c.kind == "sailboat") {   // an engine's froth, or the wash under her quarter
		const double gap = 0.3;
		for (double u = std::floor(t / gap) * gap, back = 0.0; back < 16.0; u -= gap, back += gap) {
			if (u < c.from) break;
			Vec3 ahead;
			const double along = alongAt(c, U, u), way = (alongAt(c, U, u + 0.1) - along) / 0.1;
			if (way < 0.3) continue;
			const Vec3 p = placeAt(c, along, &ahead) - ahead * (0.5 * h.length);
			f.froth.push_back({p.x, p.z, u, 0.14 * h.beam * (c.kind == "sailboat" ? 0.5 : 1.0), c.kind == "sailboat" ? 2.0 : 5.0});
		}
	}
	if (c.kind == "rowboat") {   // a ring from each blade at each catch
		const double period = 1.0 / kStrokeRate;
		for (double n = std::floor(t / period); n > std::floor(t / period) - 8.0; n -= 1.0) {
			const double u = (n + 0.25) * period;
			if (u > t || u < c.from) continue;
			const Riding r = ridingAt(c, h, water, u);
			if (r.way < 0.05) continue;
			const Mat4 frame = frameOf(r);
			for (int side = -1; side <= 1; side += 2) {
				const Vec3 p = transformPoint(frame, catchOf(h, side));
				f.rings.push_back(stir::ringOf(-p.x, p.z, u, 0.05, 0.004, true));
			}
		}
	}
}

}  // namespace craft
}  // namespace cartoon
