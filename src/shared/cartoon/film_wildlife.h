#pragma once
// film_wildlife.h -- animals for a film file (film_file.h `flock`, `herd`): a murmuration of starlings, a skein of geese in a V, a school of fish, butterflies over a meadow,
// eagles circling up a thermal, a herd of deer, horses or wolves crossing the land.
//
// How they move:
//   starlings, fish   flocking (boids): each keeps apart from the nearest, turns as its seven nearest neighbours turn, closes on them, and is drawn to a centre that wanders
//                     round where the flock was put - so the flock swirls, stretches and folds. The flight is worked out once when the film is loaded, 24 steps a second
//                     over the film, and each frame takes the step at its time (frames stay independent and render in any order);
//   geese             a V behind a leader that flies from one point to another, each bird half a wingbeat after the one before;
//   butterflies       each wanders over its patch near the ground, flitting up and down;
//   eagles            each circling on the thermal at `at`, rising, banked into its circle, gliding and now and then flapping;
//   deer, horses,     a loose herd (a pack) walking from one point to another over the land, each stopping now and then to graze (wolves: to stand and look round), and
//   wolves            from `flee T` on breaking into a gallop, one after another, and running on.
// How they look: starlings, fish and butterflies are drawn from a few poses of their wingbeat or tail beat made here; deer, horses, wolves, eagles and geese are rigged
// models with their clips (film_animals.h: walking, trotting, galloping by their speed, without sliding; grazing; flapping and gliding). Either way the poses are pbrt
// objects written once a frame and placed once per animal, turned along its way and banked into its turns.
//
// Coordinates are pbrt's, as a film file's; the shapes are made in story space and placed with nature::placeObject.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_animals.h"
#include "film_terrain.h"
#include "nature_lib.h"

namespace cartoon {
namespace wildlife {

inline const std::vector<std::string> &flockKinds() {
	static const std::vector<std::string> k = {"starlings", "geese", "fish", "butterflies", "eagles"};
	return k;
}
inline const std::vector<std::string> &herdKinds() {
	static const std::vector<std::string> k = {"deer", "horses", "wolves"};
	return k;
}

// A flock: its kind, how many, where (`at`, the middle it keeps round, and `radius`; geese: `from` and `to`; butterflies: the rectangle x0 z0 x1 z1 at height `at.y`),
// how fast (metres a second), when it flies (t0 to t1), and a seed.
struct Flock {
	std::string kind;
	int count = 100;
	Vec3 at, from, to;
	double radius = 30.0, speed = 0.0, t0 = 0.0, t1 = 1e30, x0 = 0.0, z0 = 0.0, x1 = 0.0, z1 = 0.0;
	int seed = 1;
	// (starlings, fish) the flight, worked out when the film is loaded: positions and velocities at each step (pbrt's)
	std::vector<std::vector<Vec3>> pos, vel;
	double dt = 1.0 / 24.0;
};
// A herd: its kind, how many, from where to where (pbrt's x z), how fast it walks (0: its kind's pace), when it sets off, when it flees (below 0: never), a seed.
struct Herd {
	std::string kind = "deer";
	int count = 10;
	double fx = 0.0, fz = 0.0, tx = 0.0, tz = 0.0, speed = 0.0, t0 = 0.0, flee = -1.0;
	int seed = 1;
};

namespace detail {

inline double speedOf(const std::string &kind) { return kind == "starlings" ? 11.0 : kind == "geese" ? 15.0 : kind == "fish" ? 1.4 : kind == "eagles" ? 12.0 : 1.2; }

// Flocking, stepped from t = 0 to `length`: the flock's positions and velocities every dt.
inline void fly(Flock &f, double seconds) {
	const int n = f.count;
	const double speed = f.speed > 0.0 ? f.speed : speedOf(f.kind);
	const double space = f.kind == "fish" ? 0.5 : 3.0;   // (how close they come: a murmuration seen from afar is a cloud of birds a few metres apart)
	std::vector<Vec3> p(static_cast<std::size_t>(n)), v(p.size());
	for (int i = 0; i < n; ++i) {   // a ball of them, all heading roughly one way
		const int s = f.seed * 4999 + i;
		const double u = hash01(s, 1), w = hash01(s, 2), r = 0.4 * f.radius * std::cbrt(hash01(s, 3));
		const double th = 2.0 * kPi * u, ph = std::acos(2.0 * w - 1.0);
		p[static_cast<std::size_t>(i)] = f.at + Vec3{r * std::sin(ph) * std::cos(th), 0.5 * r * std::cos(ph), r * std::sin(ph) * std::sin(th)};
		v[static_cast<std::size_t>(i)] = normalize(Vec3{1.0, 0.0, 0.2 * (hash01(s, 4) - 0.5)}) * speed;
	}
	const int steps = static_cast<int>(std::ceil(seconds / f.dt)) + 2;
	f.pos.assign(static_cast<std::size_t>(steps), {});
	f.vel.assign(static_cast<std::size_t>(steps), {});
	std::vector<std::pair<double, int>> around;
	const double cellSize = 3.0 * space;   // (neighbours are looked for in the 27 cells round each one: a flock of thousands costs no more per bird than one of tens)
	std::map<long long, std::vector<int>> grid;
	const auto cellOf = [&](const Vec3 &q, int dx, int dy, int dz) {
		const long long cx = static_cast<long long>(std::floor(q.x / cellSize)) + dx, cy = static_cast<long long>(std::floor(q.y / cellSize)) + dy, cz = static_cast<long long>(std::floor(q.z / cellSize)) + dz;
		return (cx * 73856093LL) ^ (cy * 19349663LL) ^ (cz * 83492791LL);
	};
	for (int k = 0; k < steps; ++k) {
		f.pos[static_cast<std::size_t>(k)] = p;
		f.vel[static_cast<std::size_t>(k)] = v;
		const double t = k * f.dt;
		// the centre it is drawn to wanders round where the flock was put (slowly; fish stay in a smaller sweep)
		const Vec3 centre = f.at + Vec3{f.radius * (nature::noise2(t * 0.07, 1.3, f.seed) - 0.5) * 1.6, f.radius * (nature::noise2(t * 0.09, 4.1, f.seed + 1) - 0.5) * 0.5,
										f.radius * (nature::noise2(t * 0.07, 7.7, f.seed + 2) - 0.5) * 1.6};
		std::vector<Vec3> nv = v;
		grid.clear();
		for (int i = 0; i < n; ++i) grid[cellOf(p[static_cast<std::size_t>(i)], 0, 0, 0)].push_back(i);
		for (int i = 0; i < n; ++i) {
			around.clear();
			for (int dz = -1; dz <= 1; ++dz)
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx) {
						const auto it = grid.find(cellOf(p[static_cast<std::size_t>(i)], dx, dy, dz));
						if (it == grid.end()) continue;
						for (int j : it->second)
							if (j != i) around.push_back({length(p[static_cast<std::size_t>(j)] - p[static_cast<std::size_t>(i)]), j});
					}
			const int topo = std::min(7, static_cast<int>(around.size()));
			std::partial_sort(around.begin(), around.begin() + topo, around.end());
			Vec3 sep{0, 0, 0}, ali{0, 0, 0}, coh{0, 0, 0};
			for (int q = 0; q < topo; ++q) {
				const auto [d, j] = around[static_cast<std::size_t>(q)];
				const Vec3 off = p[static_cast<std::size_t>(i)] - p[static_cast<std::size_t>(j)];
				if (d < space * 2.0) sep = sep + off * (1.0 / std::max(0.05, d * d));
				ali = ali + v[static_cast<std::size_t>(j)];
				coh = coh + p[static_cast<std::size_t>(j)];
			}
			if (topo > 0) {
				ali = ali * (1.0 / topo) - v[static_cast<std::size_t>(i)];
				coh = coh * (1.0 / topo) - p[static_cast<std::size_t>(i)];
			}
			const Vec3 home = centre - p[static_cast<std::size_t>(i)];
			const Vec3 accel = sep * (2.5 * space) + ali * 1.2 + coh * 0.35 + home * (1.2 / std::max(1.0, f.radius)) * speed;
			Vec3 w = v[static_cast<std::size_t>(i)] + accel * f.dt;
			w.y *= 0.96;   // (they turn more than they climb)
			const double sp = length(w);
			w = w * (std::clamp(sp, 0.6 * speed, 1.3 * speed) / std::max(1e-9, sp));
			nv[static_cast<std::size_t>(i)] = w;
		}
		v = nv;
		for (int i = 0; i < n; ++i) p[static_cast<std::size_t>(i)] = p[static_cast<std::size_t>(i)] + v[static_cast<std::size_t>(i)] * f.dt;
	}
}

// The turn that faces +z along `fwd` with its up as near `upHint` as it can be (story space; columns right, up, forward).
inline Mat4 facing(Vec3 fwd, Vec3 upHint) {
	fwd = normalize(fwd);
	Vec3 right = cross(upHint, fwd);
	right = length(right) > 1e-9 ? normalize(right) : Vec3{1, 0, 0};
	const Vec3 up = cross(fwd, right);
	return {right.x, up.x, fwd.x, 0, right.y, up.y, fwd.y, 0, right.z, up.z, fwd.z, 0, 0, 0, 0, 1};
}

// ---- the shapes, in poses ----
constexpr int kPoses = 8;
inline void wing(Mesh &m, Vec3 shoulder, double span, double chord, double angle, double side) {   // a wing out to one side, raised by `angle` (radians)
	const Vec3 out{side * std::cos(angle), std::sin(angle), 0.0};
	const Vec3 tip = shoulder + out * span, mid = shoulder + out * (span * 0.55);
	const int b = static_cast<int>(m.verts.size());
	for (const Vec3 &q : {shoulder + Vec3{0, 0, chord * 0.5}, shoulder - Vec3{0, 0, chord * 0.6}, mid - Vec3{0, 0, chord * 0.55}, tip - Vec3{0, 0, chord * 0.15}, mid + Vec3{0, 0, chord * 0.3}}) {
		m.verts.push_back(q);
		m.normals.push_back(normalize(Vec3{-out.y * side, out.x * side, 0.0}));
	}
	m.tris.push_back({b, b + 1, b + 2});
	m.tris.push_back({b, b + 2, b + 4});
	m.tris.push_back({b + 4, b + 2, b + 3});
}
inline void blob(Mesh &m, Vec3 at, Vec3 size) {
	const Mesh s = uvSphere(1.0, 10, 6);
	const int b = static_cast<int>(m.verts.size());
	for (std::size_t i = 0; i < s.verts.size(); ++i) {
		m.verts.push_back(at + Vec3{s.verts[i].x * size.x, s.verts[i].y * size.y, s.verts[i].z * size.z});
		m.normals.push_back(normalize(Vec3{s.normals[i].x / size.x, s.normals[i].y / size.y, s.normals[i].z / size.z}));
	}
	for (const auto &t : s.tris) m.tris.push_back({t[0] + b, t[1] + b, t[2] + b});
}
// A bird (forward +z, up +y; `span` metres wingtip to wingtip) at phase k of its wingbeat.
inline Mesh birdPose(int k, double span) {
	Mesh m;
	const double L = span * 0.42, beat = std::sin(2.0 * kPi * k / kPoses);
	blob(m, {0, 0, 0}, {0.09 * L, 0.08 * L, 0.32 * L});
	blob(m, {0, 0.05 * L, 0.33 * L}, {0.07 * L, 0.07 * L, 0.08 * L});
	for (const double side : {-1.0, 1.0}) wing(m, {side * 0.05 * L, 0.03 * L, 0.05 * L}, span * 0.48, 0.42 * L, 0.65 * beat + 0.1, side);
	const int b = static_cast<int>(m.verts.size());   // the tail, a little fan
	for (const Vec3 &q : {Vec3{0, 0, -0.25 * L}, Vec3{-0.12 * L, 0, -0.5 * L}, Vec3{0.12 * L, 0, -0.5 * L}}) {
		m.verts.push_back(q);
		m.normals.push_back({0, 1, 0});
	}
	m.tris.push_back({b, b + 1, b + 2});
	return m;
}
// A fish (forward +z) `len` long, at phase k of its tail beat.
inline Mesh fishPose(int k, double len) {
	Mesh m;
	const double sway = 0.35 * std::sin(2.0 * kPi * k / kPoses);
	const Mesh body = lathe({{0.0, -0.5}, {0.08, -0.35}, {0.14, -0.05}, {0.12, 0.2}, {0.06, 0.4}, {0.0, 0.5}}, 10);
	for (std::size_t i = 0; i < body.verts.size(); ++i) {   // (the lathe's axis is y: lay it along z, squash it side to side, and bend its tail)
		const Vec3 v = body.verts[i], n = body.normals[i];
		const double z = v.y * len, bend = z < 0.0 ? sway * (z / len) * (z / len) * len : 0.0;
		m.verts.push_back({v.x * len * 0.55 + bend, v.z * len, z});
		m.normals.push_back(normalize(Vec3{n.x / 0.55, n.z, n.y}));
	}
	m.tris = body.tris;
	const int b = static_cast<int>(m.verts.size());
	const double tailX = sway * 0.25 * len;
	for (const Vec3 &q : {Vec3{tailX * 0.8, 0, -0.45 * len}, Vec3{tailX, 0.16 * len, -0.68 * len}, Vec3{tailX, -0.16 * len, -0.68 * len}}) {
		m.verts.push_back(q);
		m.normals.push_back({1, 0, 0});
	}
	m.tris.push_back({b, b + 1, b + 2});
	return m;
}
// A butterfly (forward +z) at phase k of its wingbeat: two pairs of wings.
inline Mesh butterflyPose(int k) {
	Mesh m;
	const double a = 1.2 * std::sin(2.0 * kPi * k / kPoses) + 0.25;
	blob(m, {0, 0, 0}, {0.006, 0.006, 0.03});
	for (const double side : {-1.0, 1.0}) {
		const Vec3 out{side * std::cos(a), std::sin(a), 0.0};
		const int b = static_cast<int>(m.verts.size());
		for (const Vec3 &q : {Vec3{0, 0, 0.01}, out * 0.05 + Vec3{0, 0, 0.035}, out * 0.055 + Vec3{0, 0, -0.005}, Vec3{0, 0, -0.012}, out * 0.04 + Vec3{0, 0, -0.04}}) {
			m.verts.push_back(q);
			m.normals.push_back(normalize(Vec3{-out.y * side, out.x * side, 0.0}));
		}
		m.tris.push_back({b, b + 1, b + 2});
		m.tris.push_back({b, b + 2, b + 3});
		m.tris.push_back({b + 3, b + 2, b + 4});
	}
	return m;
}
inline std::string poseObjects(const std::string &kind) {
	std::string out;
	for (int k = 0; k < kPoses; ++k) {
		const std::string name = "wild_" + kind + "_" + std::to_string(k);
		if (kind == "starlings") out += nature::objectBegin(name) + attr(coated({0.03, 0.03, 0.035}, 0.3), emitMesh(birdPose(k, 0.6), identity())) + nature::objectEnd();   // (a little larger than life)
		else if (kind == "fish") out += nature::objectBegin(name) + attr(coated({0.45, 0.48, 0.5}, 0.15), emitMesh(fishPose(k, 0.3), identity())) + nature::objectEnd();
		else if (kind == "butterflies")
			for (int c = 0; c < 3; ++c) {   // three colours: orange, yellow, blue
				static const Vec3 col[3] = {{0.75, 0.32, 0.04}, {0.8, 0.7, 0.15}, {0.12, 0.25, 0.7}};
				out += nature::objectBegin(name + "_" + std::to_string(c)) + attr(translucentMaterial(col[c], col[c] * 0.4), emitMesh(butterflyPose(k), identity())) + nature::objectEnd();
			}
	}
	return out;
}

}  // namespace detail

// Works out what a flock does over a film `length` seconds long (starlings and fish: their flight).
inline void prepare(Flock &f, double seconds) {
	if (f.kind == "starlings" || f.kind == "fish") detail::fly(f, std::min(seconds, std::max(0.0, f.t1)) + 1.0);
}

// Where eagle i of a flock is at t (pbrt's), circling its thermal: its way (`fwd`) and its circle's radius too.
inline Vec3 eagleAt(const Flock &f, int i, double t, Vec3 *fwd = nullptr, double *radius = nullptr) {
	const double speed = f.speed > 0.0 ? f.speed : detail::speedOf("eagles"), rel = t - f.t0;
	const int s = f.seed * 37 + i;
	const double r = f.radius * (0.55 + 0.6 * hash01(s, 1)), w = speed / r, a = 2.0 * kPi * hash01(s, 2) + w * rel;
	const Vec3 c = f.at + Vec3{f.radius * 0.25 * (hash01(s, 3) - 0.5), 12.0 * hash01(s, 4) + 0.6 * rel, f.radius * 0.25 * (hash01(s, 5) - 0.5)};
	if (fwd) *fwd = {-std::sin(a), 0.0, std::cos(a)};
	if (radius) *radius = r;
	return c + Vec3{r * std::cos(a), 1.5 * std::sin(0.3 * rel + i), r * std::sin(a)};
}

// A flock at t, as scene text: the poses of its kind (unless `objects` is false: another flock of the kind wrote them; a rigged kind's go through `poses`) and a placement
// for each animal. "" outside its time.
inline std::string flockText(const Flock &f, double t, const terrain::Field *ground, animals::Poses &poses, bool objects = true) {
	if (t < f.t0 || t > f.t1) return std::string();
	const bool rigged = f.kind == "geese" || f.kind == "eagles";
	std::string out = "# a flock of " + f.kind + " (film_wildlife.h)\n" + (objects && !rigged ? detail::poseObjects(f.kind) : std::string());
	const auto place = [&](const std::string &name, Vec3 at, Vec3 fwd, double bank, double s) {   // pbrt's at and fwd
		const Vec3 storyAt{-at.x, at.y, at.z}, storyFwd{-fwd.x, fwd.y, fwd.z};
		const Mat4 turn = detail::facing(storyFwd, Vec3{0, 1, 0});
		out += nature::placeObject(name, mul(translate(storyAt), turn, rotate({0, 0, 1}, bank), scale(s)));
	};
	const double rel = t - f.t0;
	if (f.kind == "starlings" || f.kind == "fish") {
		if (f.pos.empty()) return std::string();
		const double step = rel / f.dt;
		const std::size_t k = std::min(f.pos.size() - 2, static_cast<std::size_t>(std::max(0.0, step)));
		const double u = std::clamp(step - k, 0.0, 1.0);
		for (int i = 0; i < f.count; ++i) {
			const Vec3 p = f.pos[k][static_cast<std::size_t>(i)] * (1.0 - u) + f.pos[k + 1][static_cast<std::size_t>(i)] * u;
			const Vec3 v = f.vel[k][static_cast<std::size_t>(i)] * (1.0 - u) + f.vel[k + 1][static_cast<std::size_t>(i)] * u;
			const Vec3 dv = f.vel[k + 1][static_cast<std::size_t>(i)] - f.vel[k][static_cast<std::size_t>(i)];   // (bank into the turn)
			const double bank = std::clamp(-(dv.x * v.z - dv.z * v.x) / std::max(1e-6, length(v) * length(v)) * 900.0, -50.0, 50.0);
			const double beats = f.kind == "fish" ? 3.0 : 12.0;
			const int pose = static_cast<int>(std::floor((t * beats + hash01(f.seed + i, 9)) * detail::kPoses)) % detail::kPoses;
			place("wild_" + f.kind + "_" + std::to_string(pose), p, v, bank, 0.85 + 0.3 * hash01(f.seed + i, 10));
		}
	} else if (f.kind == "geese") {   // a V behind the leader, on its way from `from` to `to`
		const double speed = f.speed > 0.0 ? f.speed : detail::speedOf(f.kind);
		const Vec3 dir = normalize(f.to - f.from + Vec3{1e-9, 0, 0});
		const Vec3 side = normalize(cross(Vec3{0, 1, 0}, dir) + Vec3{1e-9, 0, 0});
		const double dist = std::min(length(f.to - f.from), rel * speed);
		const Vec3 lead = f.from + dir * dist;
		for (int i = 0; i < f.count; ++i) {
			const int rank = (i + 1) / 2, arm = i % 2 ? 1 : -1;
			const Vec3 p = lead - dir * (2.2 * rank) + side * (1.8 * rank * arm) + Vec3{0, 0.4 * std::sin(t * 0.8 + i), 0};
			const int pose = static_cast<int>(std::floor((t * 3.0 - 0.5 * rank) * animals::posesOf("flap")));
			place(poses.poseName("geese", "flap", pose, out), p, dir, 0.0, 1.0);
		}
	} else if (f.kind == "eagles") {   // each circling the thermal, rising; gliding, a few wingbeats now and then
		const double speed = f.speed > 0.0 ? f.speed : detail::speedOf(f.kind);
		for (int i = 0; i < f.count; ++i) {
			const int s = f.seed * 37 + i;
			double r = 0.0;
			Vec3 fwd;
			const Vec3 p = eagleAt(f, i, t, &fwd, &r);
			const double bank = std::atan(speed * speed / (r * 9.81)) * 180.0 / kPi;
			const double cycle = 9.0 + 6.0 * hash01(s, 6), when = std::fmod(t + cycle * hash01(s, 7), cycle);
			const std::string pose = when < 2.0 ? poses.poseName("eagles", "flap", static_cast<int>(std::floor(t * 2.2 * animals::posesOf("flap"))), out)
												: poses.poseName("eagles", "glide", static_cast<int>(std::floor(t / 4.0 * animals::posesOf("glide"))), out);
			place(pose, p, fwd, bank, 1.0);
		}
	} else if (f.kind == "butterflies") {   // each wandering its patch, flitting up and down
		for (int i = 0; i < f.count; ++i) {
			const int s = f.seed * 31 + i;
			const double wx = f.x0 + (f.x1 - f.x0) * nature::noise2(t * 0.12 + 3.1 * i, 0.5, s), wz = f.z0 + (f.z1 - f.z0) * nature::noise2(t * 0.12 + 3.1 * i, 7.5, s + 1);
			const double h = 0.4 + 1.2 * nature::noise2(t * 0.9 + i, 2.5, s + 2) + (ground ? ground->heightAt(wx, wz) : 0.0) + f.at.y;
			const double wx2 = f.x0 + (f.x1 - f.x0) * nature::noise2((t + 0.05) * 0.12 + 3.1 * i, 0.5, s), wz2 = f.z0 + (f.z1 - f.z0) * nature::noise2((t + 0.05) * 0.12 + 3.1 * i, 7.5, s + 1);
			const int pose = static_cast<int>(std::floor((t * 9.0 + hash01(s, 3)) * detail::kPoses)) % detail::kPoses;
			place("wild_butterflies_" + std::to_string(pose) + "_" + std::to_string(i % 3), {wx, h, wz}, Vec3{wx2 - wx, 0.0, wz2 - wz} + Vec3{1e-6, 0, 0}, 0.0, 2.0);   // (twice life size: they read on screen)
		}
	}
	return out;
}

// Where an animal of a herd is along its way at `rel` seconds after the herd sets off, and how fast it goes then: walking at `pace` but for its pauses (a few seconds
// in every cycle of its own), after `flee` (relative; below 0: never) breaking into a run up to `fleeSpeed` over a second and a half, without pausing again.
struct Going {
	double covered = 0.0, speed = 0.0;
	bool paused = false;
	double still = 0.0;   // how settled into its pause, 0 .. 1, easing in and out over most of a second (what turns smoothly as it stops and sets off)
};
inline Going goingAt(double rel, double pace, double cycle, double pause, double offset, double flee, double fleeSpeed) {
	const auto W = [&](double x) { return pace * (std::floor(x / cycle) * (cycle - pause) + std::min(std::fmod(x, cycle), cycle - pause)); };
	Going g;
	if (flee < 0.0 || rel <= flee) {
		g.covered = W(rel + offset) - W(offset);
		const double phase = std::fmod(rel + offset, cycle), ease = std::min(0.8, 0.4 * pause);
		g.paused = phase > cycle - pause;
		g.still = smoothstep(cycle - pause, cycle - pause + ease, phase) * (1.0 - smoothstep(cycle - ease, cycle, phase));
		g.speed = g.paused ? 0.0 : pace;
		return g;
	}
	const double ramp = 1.5, x = rel - flee, u = std::min(1.0, x / ramp);
	const double before = W(flee + offset) - W(offset), start = std::fmod(flee + offset, cycle) > cycle - pause ? 0.0 : pace;   // (from a standstill if it was grazing)
	// the speed from `start` to fleeSpeed by smoothstep over the ramp, integrated: ramp (u^3 - u^4/2) of the difference over the ramp, then steady
	const double rise = x < ramp ? ramp * (u * u * u - 0.5 * u * u * u * u) : 0.5 * ramp + (x - ramp);
	g.covered = before + start * x + (fleeSpeed - start) * rise;
	g.speed = start + (fleeSpeed - start) * u * u * (3.0 - 2.0 * u);
	return g;
}

// Where animal i of a herd is at t (pbrt's, on the ground), how it goes, and the way the herd is heading: what herdText draws and film_soundscape.h hears.
struct Animal {
	Vec3 at, dir;
	Going going;
	std::string clip;     // its gait's clip while it moves ("" when it has stopped)
	double cycles = 0.0;  // how many cycles of that clip it has made (with its own offset): its footfalls
};
inline Animal animalAt(const Herd &h, const animals::Species &sp, int i, double t, const terrain::Field *ground) {
	Animal a;
	const Vec3 from{h.fx, 0, h.fz}, to{h.tx, 0, h.tz};
	a.dir = normalize(to - from + Vec3{1e-9, 0, 0});
	const Vec3 side{-a.dir.z, 0, a.dir.x};
	const double total = length(to - from), size = sp.length / 0.95, pace = h.speed > 0.0 ? h.speed : sp.walkSpeed;
	const int s = h.seed * 61 + i;
	const double behind = size * (3.0 * std::sqrt(static_cast<double>(i)) + 2.0 * hash01(s, 1)), across = size * 4.0 * (hash01(s, 2) - 0.5) * std::sqrt(static_cast<double>(i + 1));
	const double rel = std::max(0.0, t - h.t0);
	const double cycle = 9.0 + 5.0 * hash01(s, 3), pause = (sp.grazes ? 2.5 : 1.2) + 2.0 * hash01(s, 4), offset = cycle * hash01(s, 5);
	const double flee = h.flee >= 0.0 ? std::max(0.0, h.flee - h.t0) + 0.15 * std::sqrt(static_cast<double>(i)) + 0.3 * hash01(s, 9) : -1.0;   // (one after another)
	const double fleeSpeed = sp.fleeSpeed * (0.92 + 0.16 * hash01(s, 10));
	a.going = goingAt(rel, pace, cycle, pause, offset, flee, fleeSpeed);
	if (flee < 0.0 && a.going.covered >= total) a.going.covered = total, a.going.paused = true, a.going.speed = 0.0, a.going.still = 1.0;   // (there: it stops)
	a.at = from + a.dir * a.going.covered - a.dir * behind + side * across;
	a.at.y = ground ? ground->heightAt(a.at.x, a.at.z) : 0.0;
	if (!a.going.paused) {
		animals::gaitOf(sp, a.going.speed, a.going.covered, a.clip, a.cycles);
		if (flee >= 0.0 && rel > flee) {   // fleeing, through the gaits: the cycles counted a step at a time at each gait's stride, so the legs carry on from where they were
			const auto strideAt = [&](double v) { return v < sp.trotFrom ? sp.walkStride : v < sp.runFrom ? sp.trotStride : sp.runStride; };
			Going before = goingAt(flee, pace, cycle, pause, offset, flee, fleeSpeed);
			double cycles = before.covered / strideAt(pace);
			const double dt = 0.05;
			for (double x = flee; x < rel; x += dt) {
				const Going next = goingAt(std::min(rel, x + dt), pace, cycle, pause, offset, flee, fleeSpeed);
				cycles += (next.covered - before.covered) / strideAt(0.5 * (before.speed + next.speed));
				before = next;
			}
			a.cycles = cycles;
		}
		a.cycles += hash01(s, 6);
	}
	return a;
}

// A herd at t, as scene text: each animal walking from `from` towards `to` in a loose bunch, standing on the land, stopping now and then to graze (wolves: to look round),
// fleeing at a gallop from `flee` on; its pose from its kind's clips (film_animals.h) through `poses`.
inline std::string herdText(const Herd &h, double t, const terrain::Field *ground, animals::Poses &poses) {
	const animals::Species *sp = animals::species(h.kind);
	if (!sp) return std::string();
	std::string out = "# a herd of " + h.kind + " (film_wildlife.h)\n";
	for (int i = 0; i < h.count; ++i) {
		const int s = h.seed * 61 + i;
		const Animal a = animalAt(h, *sp, i, t, ground);
		const Vec3 p = a.at, dir = a.dir;
		const auto groundAt = [&](const Vec3 &q) { return ground ? ground->heightAt(q.x, q.z) : 0.0; };
		const double pitch = std::atan2(groundAt(p + dir * (0.5 * sp->length)) - groundAt(p - dir * (0.5 * sp->length)), sp->length) * 180.0 / kPi;   // (to the slope)
		std::string pose;
		const double turn = (6.0 + 44.0 * a.going.still) * (hash01(s, 7) - 0.5);   // (turned further aside as it settles to graze: it snapped 22 degrees in a frame)
		if (a.going.paused) {
			const std::string clip = sp->grazes ? "graze" : "idle";
			const double len = clip == "graze" ? 4.0 : 5.0;
			pose = poses.poseName(h.kind, clip, static_cast<int>(std::floor((t / len + hash01(s, 11)) * animals::posesOf(clip))), out);
		} else {
			pose = poses.poseName(h.kind, a.clip, static_cast<int>(std::floor(a.cycles * animals::posesOf(a.clip))), out);
		}
		const Vec3 storyAt{-p.x, p.y, p.z}, storyFwd{-dir.x, 0.0, dir.z};
		out += nature::placeObject(pose, mul(translate(storyAt), detail::facing(storyFwd, {0, 1, 0}), rotate({0, 1, 0}, turn), rotate({1, 0, 0}, -pitch), scale(0.9 + 0.2 * hash01(s, 8))));
	}
	return out;
}

}  // namespace wildlife
}  // namespace cartoon
