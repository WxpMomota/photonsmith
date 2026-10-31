#pragma once
// film_school.h -- the open ocean, round 15: schools that behave, and the big animals.
//
//   school sardines|anchovies|jacks|barracuda N at X Y Z [radius R] [strike T]... [hunter sharks|dolphins|sealions] [seed K]
//            a school of up to 3000 fish, flocking (each keeps clear of its nearest, turns as its seven nearest turn, closes on them, and is drawn to a wandering
//            centre - film_wildlife.h's way), worked out when the film is loaded. It parts round the camera and any diver that comes through it (they keep 2.5 m away,
//            more if it moves fast), and at each `strike` a hunter dashes through the middle: the fish near its path bolt away from it at three times their pace and the
//            school splits round it, turning together - the turn shows their silver flanks in a wave - and closes again behind it.
//   bigfish whaleshark|hammerheads|reefsharks|dolphins|humpbacks N at X Y Z [radius R] [seed K]
//            whale sharks (a broad flat head, a wide mouth open as they feed, white spots in rows on a dark back), hammerheads (the wide flat head with an eye at each
//            end), reef sharks, dolphins (their tails beating up and down) and humpbacks (long white flippers, slow strokes of the flukes) swimming round a circle.
//
// Coordinates: pbrt's in the film file (on a terrain heights are above the ground: lifted()); meshes in story space (x mirrored) unless said.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_deepsea.h"
#include "film_giants.h"
#include "film_sealife.h"
#include "nature_lib.h"

namespace cartoon {
namespace school {

struct School {
	std::string kind = "sardines", hunter = "sharks";
	int count = 400, seed = 1;
	Vec3 at;
	double radius = 5.0;
	std::vector<double> strikes;
	// worked out when the film is loaded (prepare): positions and velocities every dt, and the hunter's dashes
	std::vector<std::vector<Vec3>> pos, vel;
	double dt = 1.0 / 12.0;
	struct Dash {
		Vec3 from, dir;
		double start;
	};
	std::vector<Dash> dashes;
};
struct BigFish {
	std::string kind = "reefsharks";
	int count = 1, seed = 1;
	Vec3 at;
	double radius = 12.0;
};
inline const std::vector<std::string> &kinds() {
	static const std::vector<std::string> k = {"sardines", "anchovies", "jacks", "barracuda"};
	return k;
}
inline const std::vector<std::string> &bigKinds() {
	static const std::vector<std::string> k = {"whaleshark", "hammerheads", "reefsharks", "dolphins", "humpbacks"};
	return k;
}
inline double lengthOf(const std::string &kind) { return kind == "anchovies" ? 0.11 : kind == "jacks" ? 0.5 : kind == "barracuda" ? 1.0 : 0.18; }
inline double paceOf(const std::string &kind) { return kind == "anchovies" ? 1.0 : kind == "jacks" ? 1.4 : kind == "barracuda" ? 0.7 : 1.2; }
constexpr double kDashSpeed = 9.0;

// Where a dash's hunter is at t (pbrt's), or false while it is not under way (in from 14 m out, through the middle, on 14 m past it).
inline bool hunterAt(const School::Dash &d, double t, Vec3 &at) {
	const double since = t - d.start;
	if (since < 0.0 || since > 28.0 / kDashSpeed) return false;
	at = d.from + d.dir * (kDashSpeed * since);
	return true;
}

// Works out the school's swim over `seconds`: `avoid(t)` gives the things it keeps clear of at t (pbrt's: the camera, divers).
inline void prepare(School &s, double seconds, const std::function<std::vector<Vec3>(double)> &avoid) {
	const int n = s.count;
	const double L = lengthOf(s.kind), pace = paceOf(s.kind), space = std::max(0.25, 1.6 * L);
	std::vector<Vec3> p(static_cast<std::size_t>(n)), v(p.size());
	for (int i = 0; i < n; ++i) {
		const int q = s.seed * 4999 + i;
		const double u = hash01(q, 1), w = hash01(q, 2), r = 0.5 * s.radius * std::cbrt(hash01(q, 3));
		const double th = 2.0 * kPi * u, ph = std::acos(2.0 * w - 1.0);
		p[static_cast<std::size_t>(i)] = s.at + Vec3{r * std::sin(ph) * std::cos(th), 0.45 * r * std::cos(ph), r * std::sin(ph) * std::sin(th)};
		v[static_cast<std::size_t>(i)] = normalize(Vec3{1.0, 0.0, 0.3 * (hash01(q, 4) - 0.5)}) * pace;
	}
	const int steps = static_cast<int>(std::ceil(seconds / s.dt)) + 2;
	s.pos.assign(static_cast<std::size_t>(steps), {});
	s.vel.assign(static_cast<std::size_t>(steps), {});
	s.dashes.clear();
	std::vector<double> pending = s.strikes;
	std::sort(pending.begin(), pending.end());
	std::size_t nextStrike = 0;
	const double cell = 3.0 * space;
	std::map<long long, std::vector<int>> grid;
	const auto cellOf = [&](const Vec3 &q, int dx, int dy, int dz) {
		const long long cx = static_cast<long long>(std::floor(q.x / cell)) + dx, cy = static_cast<long long>(std::floor(q.y / cell)) + dy, cz = static_cast<long long>(std::floor(q.z / cell)) + dz;
		return (cx * 73856093LL) ^ (cy * 19349663LL) ^ (cz * 83492791LL);
	};
	std::vector<std::pair<double, int>> around;
	for (int k = 0; k < steps; ++k) {
		s.pos[static_cast<std::size_t>(k)] = p;
		s.vel[static_cast<std::size_t>(k)] = v;
		const double t = k * s.dt;
		Vec3 mid{0, 0, 0};
		for (const Vec3 &q : p) mid = mid + q;
		mid = mid * (1.0 / std::max(1, n));
		// a strike's dash is laid out as it begins: in from 14 m away, on a line through the school's middle as it is then, reaching it at the strike's time
		while (nextStrike < pending.size() && t >= pending[nextStrike] - 14.0 / kDashSpeed) {
			const int q = s.seed * 31 + static_cast<int>(nextStrike);
			const double a = 2.0 * kPi * hash01(q, 1), el = 0.35 * (hash01(q, 2) - 0.5);
			const Vec3 dir = normalize(Vec3{std::cos(a), el, std::sin(a)});
			s.dashes.push_back({mid - dir * 14.0, dir, pending[nextStrike] - 14.0 / kDashSpeed});
			++nextStrike;
		}
		std::vector<Vec3> threats = avoid ? avoid(t) : std::vector<Vec3>();
		std::vector<std::pair<Vec3, Vec3>> hunters;   // (where each is and the way it dashes)
		for (const School::Dash &d : s.dashes) {
			Vec3 h;
			if (hunterAt(d, t, h)) hunters.push_back({h, d.dir});
		}
		const Vec3 centre = s.at + Vec3{s.radius * (nature::noise2(t * 0.06, 1.3, s.seed) - 0.5) * 1.4, s.radius * (nature::noise2(t * 0.08, 4.1, s.seed + 1) - 0.5) * 0.4,
										s.radius * (nature::noise2(t * 0.06, 7.7, s.seed + 2) - 0.5) * 1.4};
		grid.clear();
		for (int i = 0; i < n; ++i) grid[cellOf(p[static_cast<std::size_t>(i)], 0, 0, 0)].push_back(i);
		std::vector<Vec3> nv = v;
		for (int i = 0; i < n; ++i) {
			const Vec3 &pi = p[static_cast<std::size_t>(i)];
			around.clear();
			for (int dz = -1; dz <= 1; ++dz)
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx) {
						const auto it = grid.find(cellOf(pi, dx, dy, dz));
						if (it == grid.end()) continue;
						for (int j : it->second)
							if (j != i) around.push_back({length(p[static_cast<std::size_t>(j)] - pi), j});
					}
			const int topo = std::min(7, static_cast<int>(around.size()));
			std::partial_sort(around.begin(), around.begin() + topo, around.end());
			Vec3 sep{0, 0, 0}, ali{0, 0, 0}, coh{0, 0, 0};
			for (int q = 0; q < topo; ++q) {
				const auto [d, j] = around[static_cast<std::size_t>(q)];
				if (d < space * 2.0) sep = sep + (pi - p[static_cast<std::size_t>(j)]) * (1.0 / std::max(0.02, d * d));
				ali = ali + v[static_cast<std::size_t>(j)];
				coh = coh + p[static_cast<std::size_t>(j)];
			}
			if (topo > 0) ali = ali * (1.0 / topo) - v[static_cast<std::size_t>(i)], coh = coh * (1.0 / topo) - pi;
			Vec3 flee{0, 0, 0};
			double fright = 0.0;
			for (const auto &[h, way] : hunters) {   // bolting from the hunter: out sideways from the line it comes along, the more the nearer it is
				const Vec3 rel = pi - h;
				const double along = dot(rel, way);
				if (along < -2.0 || along > 9.0) continue;   // (behind it, or far ahead)
				Vec3 side = rel - way * along;
				const double d = length(side);
				side = d > 1e-6 ? side * (1.0 / d) : normalize(cross(way, Vec3{0.0, 1.0, 0.0}) + Vec3{1e-9, 0, 0});
				const double close = std::clamp(1.0 - d / 4.0, 0.0, 1.0) * std::clamp(1.0 - along / 9.0, 0.0, 1.0);
				if (close > 0.0) flee = flee + side * (40.0 * close), fright = std::max(fright, close);
			}
			for (const Vec3 &h : threats) {   // and keeping clear of the camera, a diver
				const Vec3 off = pi - h;
				const double d = length(off);
				if (d < 2.5) flee = flee + off * (1.0 / std::max(0.1, d)) * (2.5 - d) * 3.0, fright = std::max(fright, 0.5 * (1.0 - d / 2.5));
			}
			const Vec3 accel = sep * (2.5 * space) + ali * (1.2 + 2.0 * fright) + coh * 0.35 * (1.0 - fright) + (centre - pi) * (1.0 / std::max(1.0, s.radius)) * pace + flee;
			Vec3 w = v[static_cast<std::size_t>(i)] + accel * s.dt;
			w.y *= 0.95;
			const double sp = length(w), top = pace * (1.3 + 1.7 * fright);
			w = w * (std::clamp(sp, 0.6 * pace, top) / std::max(1e-9, sp));
			nv[static_cast<std::size_t>(i)] = w;
		}
		v = nv;
		for (int i = 0; i < n; ++i) p[static_cast<std::size_t>(i)] = p[static_cast<std::size_t>(i)] + v[static_cast<std::size_t>(i)] * s.dt;
	}
}
// A worked-out swim as bytes (positions and velocities as floats, the dashes as doubles) and back: a film's frames each render in a process of their own, and working a
// big school out again in every one cost seconds a frame - the first keeps it on disk for the rest (DataFilm::schoolsSwum). False when the bytes are not a whole swim of `s`.
inline std::string swumBytes(const School &s) {
	std::string out;
	const auto put = [&out](const void *p, std::size_t n) { out.append(static_cast<const char *>(p), n); };
	const std::uint32_t steps = static_cast<std::uint32_t>(s.pos.size()), n = static_cast<std::uint32_t>(s.count), dashes = static_cast<std::uint32_t>(s.dashes.size());
	put(&steps, 4), put(&n, 4), put(&dashes, 4);
	out.reserve(out.size() + static_cast<std::size_t>(steps) * n * 24 + dashes * 56);
	for (const auto *all : {&s.pos, &s.vel})
		for (const std::vector<Vec3> &row : *all)
			for (const Vec3 &q : row) {
				const float f[3] = {static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z)};
				put(f, 12);
			}
	for (const School::Dash &d : s.dashes) {
		const double v[7] = {d.from.x, d.from.y, d.from.z, d.dir.x, d.dir.y, d.dir.z, d.start};
		put(v, 56);
	}
	return out;
}
inline bool swumFrom(School &s, const std::string &bytes) {
	std::uint32_t head[3];
	if (bytes.size() < 12) return false;
	std::memcpy(head, bytes.data(), 12);
	const std::size_t steps = head[0], n = head[1], dashes = head[2];
	if (n != static_cast<std::size_t>(s.count) || steps < 2 || bytes.size() != 12 + steps * n * 24 + dashes * 56) return false;
	const char *p = bytes.data() + 12;
	for (auto *all : {&s.pos, &s.vel}) {
		all->assign(steps, std::vector<Vec3>(n));
		for (std::vector<Vec3> &row : *all)
			for (Vec3 &q : row) {
				float f[3];
				std::memcpy(f, p, 12), p += 12;
				q = {f[0], f[1], f[2]};
			}
	}
	s.dashes.resize(dashes);
	for (School::Dash &d : s.dashes) {
		double v[7];
		std::memcpy(v, p, 56), p += 56;
		d.from = {v[0], v[1], v[2]}, d.dir = {v[3], v[4], v[5]}, d.start = v[6];
	}
	return true;
}
// Where fish i is at t (pbrt's) and its velocity, between the worked-out steps.
inline Vec3 fishAt(const School &s, int i, double t, Vec3 *vel = nullptr) {
	if (s.pos.empty()) return s.at;
	const double step = std::max(0.0, t / s.dt);
	const std::size_t k = std::min(s.pos.size() - 2, static_cast<std::size_t>(step));
	const double u = std::clamp(step - k, 0.0, 1.0);
	if (vel) *vel = lerp(s.vel[k][static_cast<std::size_t>(i)], s.vel[k + 1][static_cast<std::size_t>(i)], u);
	return lerp(s.pos[k][static_cast<std::size_t>(i)], s.pos[k + 1][static_cast<std::size_t>(i)], u);
}

namespace detail {
// A school fish of its kind at phase k of its tail beat (forward +z), with the material it is drawn in.
inline Mesh fishOf(const std::string &kind, int k, double L) {
	if (kind == "barracuda") {   // long and thin, a pointed jaw
		const double sway = 0.18 * std::sin(2.0 * kPi * k / sealife::detail::kPoses);
		Mesh m = sealife::detail::body(L, 0.45, 0.55, sway, {{0.0, -0.5}, {0.03, -0.4}, {0.06, -0.15}, {0.065, 0.1}, {0.05, 0.35}, {0.02, 0.48}, {0.0, 0.5}});
		const double tx = sway * 0.25 * L;
		sealife::detail::fin(m, {tx * 0.8, 0.0, -0.45 * L}, {tx, 0.08 * L, -0.56 * L}, {tx, -0.08 * L, -0.56 * L});
		return m;
	}
	return sealife::detail::fish(k, L, kind == "jacks" ? 1.6 : 1.0);
}
inline std::string fishMaterial(const std::string &kind) {
	return kind == "jacks" ? metalMaterial({0.62, 0.7, 0.78}, 0.18) : kind == "barracuda" ? metalMaterial({0.72, 0.74, 0.76}, 0.22) : metalMaterial({0.8, 0.84, 0.88}, 0.2);
}
}  // namespace detail

// A school at t: its fish (instanced: a pose of the tail beat each, banking into their turns - the turn shows the silver flank) and its hunters on their dashes.
inline std::string schoolText(const School &s, int index, double t) {
	const std::string base = "school_" + std::to_string(index) + "_";
	const double L = lengthOf(s.kind);
	std::string out = "# a school of " + s.kind + " (film_school.h)\n";
	for (int k = 0; k < sealife::detail::kPoses; ++k) out += nature::objectBegin(base + std::to_string(k)) + attr(detail::fishMaterial(s.kind), emitMesh(detail::fishOf(s.kind, k, L), identity())) + nature::objectEnd();
	for (int i = 0; i < s.count; ++i) {
		Vec3 v, v2;
		const Vec3 p = fishAt(s, i, t, &v);
		fishAt(s, i, t + s.dt, &v2);
		const Vec3 dv = v2 - v;
		const double bank = std::clamp(-(dv.x * v.z - dv.z * v.x) / std::max(1e-6, length(v) * length(v)) * 600.0, -70.0, 70.0);
		const double beat = 2.0 + 2.0 * length(v) / paceOf(s.kind);
		const int pose = static_cast<int>(std::floor((t * beat + hash01(s.seed + i, 9)) * sealife::detail::kPoses)) % sealife::detail::kPoses;
		out += nature::placeObject(base + std::to_string(pose), mul(translate(sealife::detail::story(p)), sealife::detail::facingOf(sealife::detail::story(v)), rotate({0, 0, 1}, bank),
																	   scale(0.85 + 0.3 * hash01(s.seed + i, 10))));
	}
	for (const School::Dash &d : s.dashes) {   // the hunters
		Vec3 h;
		if (!hunterAt(d, t, h)) continue;
		const int pose = static_cast<int>(std::floor(t * 2.5 * sealife::detail::kPoses)) % sealife::detail::kPoses;
		const Mat4 place = mul(translate(sealife::detail::story(h)), sealife::detail::facingOf(sealife::detail::story(d.dir)));
		if (s.hunter == "sharks") out += attr(coated({0.42, 0.46, 0.5}, 0.3), emitMesh(sealife::detail::shark(pose, 2.6), place));
		else {
			const double sway = 0.15 * std::sin(t * 12.0);
			out += attr(coated(s.hunter == "sealions" ? Vec3{0.3, 0.22, 0.15} : Vec3{0.45, 0.5, 0.55}, 0.2),
						emitMesh(sealife::detail::body(s.hunter == "sealions" ? 2.0 : 2.3, 0.65, 0.7, sway, {{0.0, -0.5}, {0.1, -0.3}, {0.15, 0.0}, {0.12, 0.3}, {0.04, 0.5}}), place));
		}
	}
	return out;
}

// ---- the big animals ----
// Where big animal i is at t (pbrt's), its way and its speed: round a circle about `at`, a slow rise and fall.
inline Vec3 bigAt(const BigFish &b, int i, double t, Vec3 &fwd) {
	const int s = b.seed * 271 + i;
	const double speed = b.kind == "whaleshark" ? 1.0 : b.kind == "humpbacks" ? 1.6 : b.kind == "dolphins" ? 3.0 : 1.6;
	const double R = b.radius * (0.75 + 0.5 * hash01(s, 1)), w = speed / R * (hash01(s, 2) < 0.5 ? -1.0 : 1.0), a = w * t + 2.0 * kPi * (i + 0.4 * hash01(s, 3)) / std::max(1, b.count);
	const double rise = (b.kind == "dolphins" ? 1.5 : 0.6) * std::sin(0.25 * t + s);
	fwd = normalize(Vec3{-std::sin(a) * w * R, (b.kind == "dolphins" ? 1.5 : 0.6) * 0.25 * std::cos(0.25 * t + s), std::cos(a) * w * R});
	return b.at + Vec3{R * std::cos(a), rise, R * std::sin(a)};
}
// A whale shark (forward +z) `L` long: its body, the mouth, its fins; its spots as little white bumps over the back (`spots`).
inline Mesh whaleShark(double L, double sway, Mesh &mouth, Mesh &spots) {
	Mesh m = sealife::detail::body(L, 1.15, 0.7, sway, {{0.0, -0.5}, {0.04, -0.42}, {0.1, -0.2}, {0.13, 0.05}, {0.13, 0.3}, {0.115, 0.44}, {0.06, 0.495}, {0.0, 0.5}});
	const double tx = sway * 0.3 * L;
	sealife::detail::fin(m, {0.0, 0.08 * L, 0.02 * L}, {0.0, 0.2 * L, -0.08 * L}, {0.0, 0.085 * L, -0.12 * L});   // (the first dorsal)
	sealife::detail::fin(m, {0.0, 0.05 * L, -0.25 * L}, {0.0, 0.1 * L, -0.3 * L}, {0.0, 0.05 * L, -0.32 * L});
	for (const double side : {-1.0, 1.0}) sealife::detail::fin(m, {side * 0.12 * L, -0.04 * L, 0.18 * L}, {side * 0.38 * L, -0.12 * L, 0.02 * L}, {side * 0.12 * L, -0.05 * L, 0.08 * L});
	sealife::detail::fin(m, {tx * 0.8, 0.0, -0.45 * L}, {tx * 1.2, 0.28 * L, -0.62 * L}, {tx, 0.0, -0.55 * L});
	sealife::detail::fin(m, {tx * 0.8, 0.0, -0.45 * L}, {tx, 0.0, -0.55 * L}, {tx * 1.1, -0.2 * L, -0.6 * L});
	deepsea::detail::add(mouth, roundedBox(0.2 * L, 0.035 * L, 0.02 * L, 0.01 * L, 2), translate(0.0, -0.01 * L, 0.495 * L));
	std::vector<Vec3> dots;   // the spots: in rows across the back and down the flanks, each row a little offset (on the body's own profile, outside it)
	const std::vector<std::pair<double, double>> profile{{0.0, -0.5}, {0.04, -0.42}, {0.1, -0.2}, {0.13, 0.05}, {0.13, 0.3}, {0.115, 0.44}, {0.06, 0.495}, {0.0, 0.5}};
	for (int row = 0; row < 34; ++row) {
		const double u = -0.38 + 0.8 * row / 33.0;
		double r = 0.0;
		for (std::size_t i = 0; i + 1 < profile.size(); ++i)
			if (u >= profile[i].second && u <= profile[i + 1].second) r = lerp(profile[i].first, profile[i + 1].first, (u - profile[i].second) / (profile[i + 1].second - profile[i].second));
		for (int c = -5; c <= 5; ++c) {
			const double a = (c + (row % 2 ? 0.5 : 0.0)) * 0.22;   // (radians from the top)
			if (std::abs(a) > 1.25) continue;
			const double bend = u < 0.0 ? sway * u * u * L : 0.0;
			dots.push_back({std::sin(a) * r * L * 1.15 * 1.02 + bend, std::cos(a) * r * L * 0.7 * 1.02, u * L});
		}
	}
	spots = deepsea::detail::beads(dots, 0.006 * L);
	return m;
}
// A hammerhead (forward +z) `L` long at phase k of its tail beat: a shark with the wide flat head and its eyes at the ends.
inline Mesh hammerhead(int k, double L) {
	Mesh m = sealife::detail::shark(k, L);
	deepsea::detail::add(m, roundedBox(0.3 * L, 0.025 * L, 0.06 * L, 0.012 * L, 2), translate(0.0, 0.0, 0.47 * L));
	for (const double side : {-1.0, 1.0}) deepsea::detail::add(m, uvSphere(0.012 * L, 8, 6), translate(side * 0.15 * L, 0.0, 0.47 * L));
	return m;
}
// Big animals at t as scene text.
inline std::string bigText(const BigFish &b, double t) {
	std::string out = "# " + b.kind + " (film_school.h)\n";
	for (int i = 0; i < b.count; ++i) {
		const int s = b.seed * 53 + i;
		Vec3 fwd;
		const Vec3 at = bigAt(b, i, t, fwd);
		if (b.kind == "dolphins" || b.kind == "humpbacks") {   // the giants' bodies (film_giants.h), their tails beating up and down along the spine
			const bool whale = b.kind == "humpbacks";
			const double L = whale ? 13.0 + 2.0 * hash01(s, 1) : 2.4 + 0.3 * hash01(s, 1);
			const double beat = 2.0 * kPi * t / (whale ? 4.5 : 0.7) + s, A = (whale ? 0.07 : 0.1) * L;
			const Vec3 F = normalize(fwd), side = normalize(cross(F, Vec3{0.0, 1.0, 0.0}) + Vec3{1e-9, 0, 0}), U = normalize(cross(side, F));
			const auto frameAt = [&](double x) {   // (x metres from the middle, + to the head; pbrt's)
				const double u = std::max(0.0, -x / (0.6 * L));
				const double lift = A * u * u * std::sin(beat - 2.5 * u), slope = A * 2.0 * u * std::sin(beat - 2.5 * u) / (0.6 * L);
				giants::Frame f;
				f.at = at + F * x + U * lift;
				f.forward = normalize(F - U * slope);
				f.up = normalize(U + F * slope);   // (square to the forward way)
				return f;
			};
			out += giants::bodyText(whale ? giants::whaleBody() : giants::dolphinBody(), L, frameAt, whale ? Vec3{0.05, 0.055, 0.06} : Vec3{0.32, 0.36, 0.4},
									whale ? Vec3{0.85, 0.86, 0.86} : Vec3{0.78, 0.8, 0.82});
			continue;
		}
		const int pose = static_cast<int>(std::floor(t * (b.kind == "whaleshark" ? 0.5 : 1.5) * sealife::detail::kPoses + s)) % sealife::detail::kPoses;
		const Mat4 place = mul(translate(sealife::detail::story(at)), sealife::detail::facingOf(sealife::detail::story(fwd)));
		if (b.kind == "whaleshark") {
			Mesh mouth, spots;
			const double L = 8.0 + 3.0 * hash01(s, 1);
			const Mesh body = whaleShark(L, 0.12 * std::sin(2.0 * kPi * t / 3.5 + s), mouth, spots);
			out += attr(coated({0.12, 0.15, 0.2}, 0.3), emitMesh(body, place)) + attr(diffuseMaterial({0.02, 0.02, 0.025}), emitMesh(mouth, place)) +
				   attr(coated({0.8, 0.82, 0.8}, 0.3), emitMesh(spots, place));
		} else if (b.kind == "hammerheads") {
			out += attr(coated({0.5, 0.5, 0.46}, 0.3), emitMesh(hammerhead(pose, 3.0 + 0.8 * hash01(s, 1)), place));
		} else {
			out += attr(coated({0.45, 0.42, 0.36}, 0.3), emitMesh(sealife::detail::shark(pose, 1.6 + 0.4 * hash01(s, 1)), place));
		}
	}
	return out;
}
// The film's schools and big animals with their heights made pbrt's (on a terrain heights are above the ground there).
inline std::vector<BigFish> lifted(std::vector<BigFish> big, const std::function<double(double, double)> &groundAt) {
	for (BigFish &b : big) b.at.y += groundAt(b.at.x, b.at.z);
	return big;
}

}  // namespace school
}  // namespace cartoon
