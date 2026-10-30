#pragma once
// film_battle.h -- action in space (film_file.h `laser`, `explode`, `shield`; round 11): laser bolts flying from a ship's guns to a ship or a place, lighting what they pass;
// explosions as they are in a vacuum - a flash, a fireball gone in a second (there is no air to burn or to hold smoke), a shock ring racing out and the wreck flying apart
// in pieces that tumble on forever; and shields - a bubble round a ship, unseen until a bolt strikes it, then rippling out from the hit. All heard the cinema's way
// (film_voyage.h's sound in space): the bolts' zap, the hits' crack, the blasts' boom.
//
// Times are nature's (as the ships'); coordinates pbrt's. A ship that explodes is gone from its moment on.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_soundscape.h"
#include "film_voyage.h"

namespace cartoon {
namespace battle {

// `laser FROM at T [to TARGET|X Y Z] [colour red|green|blue|R G B] [count N every S] [speed V]`
struct Laser {
	std::string from, to;   // ships' names ("" to: straight ahead of it, or the point)
	Vec3 toPoint;
	bool toIsPoint = false;
	double at = 0.0, every = 0.4, speed = 300.0;
	int count = 1;
	Vec3 colour{1.0, 0.15, 0.1};
};
// `explode SHIP|X Y Z at T [size S]`
struct Explosion {
	std::string ship;   // ("" for a place)
	Vec3 at;
	double t = 0.0, size = 0.0;   // (0: the ship's size, or 20)
	int seed = 1;
};
// `shield SHIP [radius R] [colour R G B]`
struct Shield {
	std::string ship;
	double radius = 0.0;   // (0: three quarters of the ship's length)
	Vec3 colour{0.35, 0.7, 1.0};
};
struct Battle {
	std::vector<Laser> lasers;
	std::vector<Explosion> explosions;
	std::vector<Shield> shields;
	bool empty() const { return lasers.empty() && explosions.empty() && shields.empty(); }
};

// ---- where things are ----
inline const cosmos::Ship *shipNamed(const std::vector<cosmos::Ship> &ships, const std::string &name) {
	for (const cosmos::Ship &s : ships)
		if (s.name == name) return &s;
	return nullptr;
}
inline Vec3 pbrtOf(Vec3 story) { return {-story.x, story.y, story.z}; }
// A ship's middle at nature's time `nt` (pbrt's), and a point of it in its own unit frame.
inline Vec3 shipPoint(const cosmos::Ship &s, double nt, Vec3 local = {0.0, 0.0, 0.0}) { return pbrtOf(transformPoint(cosmos::shipPlacement(s, nt), local)); }

// One bolt: fired at `t0` from `start`, reaching `end` at `t1`.
struct Bolt {
	double t0 = 0.0, t1 = 0.0;
	Vec3 start, end, colour;
	const Shield *shield = nullptr;   // (it strikes this shield, at `end`)
	Vec3 shieldCentre;
	std::string target;
};
// Every bolt of the battle: each shot's muzzle (its guns either side of its nose by turns), its target where it will be when the bolt gets there (worked out twice over),
// stopped by the target's shield when it has one.
inline std::vector<Bolt> boltsOf(const Battle &b, const std::vector<cosmos::Ship> &ships) {
	std::vector<Bolt> out;
	for (const Laser &l : b.lasers) {
		const cosmos::Ship *from = shipNamed(ships, l.from);
		if (!from) continue;
		const cosmos::Ship *to = l.to.empty() ? nullptr : shipNamed(ships, l.to);
		const Shield *shield = nullptr;
		for (const Shield &sh : b.shields)
			if (to && sh.ship == to->name) shield = &sh;
		for (int k = 0; k < l.count; ++k) {
			Bolt bolt;
			bolt.t0 = l.at + k * l.every, bolt.colour = l.colour, bolt.target = l.to;
			bolt.start = shipPoint(*from, bolt.t0, {0.45, 0.0, k % 2 ? 0.12 : -0.12});
			Vec3 aim = l.toIsPoint ? l.toPoint : (to ? shipPoint(*to, bolt.t0) : shipPoint(*from, bolt.t0, {40.0, 0.0, 0.0}));
			for (int pass = 0; pass < 2 && to; ++pass) aim = shipPoint(*to, bolt.t0 + length(aim - bolt.start) / l.speed);
			if (shield && to) {   // (stopped on the bubble: where the line to the target's middle meets it)
				bolt.shield = shield;
				const double r = shield->radius > 0.0 ? shield->radius : 0.75 * to->size;
				const Vec3 d = normalize(bolt.start - aim);
				bolt.shieldCentre = aim;
				aim = aim + d * r;
			}
			bolt.end = aim;
			bolt.t1 = bolt.t0 + length(bolt.end - bolt.start) / l.speed;
			out.push_back(bolt);
		}
	}
	return out;
}

// ---- drawing ----
// A glowing rod from a to b (pbrt's): pbrt's own cylinder, one light.
inline std::string glowRod(Vec3 a, Vec3 b, double radius, Vec3 emission) {
	const Vec3 d = b - a;
	const double len = length(d);
	if (len < 1e-9) return std::string();
	const Vec3 z = d * (1.0 / len);
	const Vec3 axis = cross(Vec3{0.0, 0.0, 1.0}, z);
	const double angle = std::acos(std::clamp(z.z, -1.0, 1.0)) * 180.0 / kPi;
	char buf[400];
	if (length(axis) > 1e-9)
		std::snprintf(buf, sizeof buf, "  Translate %.5f %.5f %.5f\n  Rotate %.5f %.5f %.5f %.5f\n  Shape \"cylinder\" \"float radius\" [ %.5f ] \"float zmin\" [ 0 ] \"float zmax\" [ %.5f ]\n", a.x, a.y, a.z,
					  angle, axis.x, axis.y, axis.z, radius, len);
	else
		std::snprintf(buf, sizeof buf, "  Translate %.5f %.5f %.5f\n%s  Shape \"cylinder\" \"float radius\" [ %.5f ] \"float zmin\" [ 0 ] \"float zmax\" [ %.5f ]\n", a.x, a.y, a.z,
					  z.z < 0.0 ? "  Rotate 180 1 0 0\n" : "", radius, len);
	return attr("Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", buf, &emission);
}
// A band round a sphere (centre c, radius r, pbrt's) about the axis `n`, from angle a0 to a1 (radians) off it, as a mesh in story space.
inline Mesh sphereBand(Vec3 c, double r, Vec3 n, double a0, double a1, int segments = 48) {
	Mesh m;
	const Vec3 u = normalize(cross(n, std::fabs(n.y) < 0.9 ? Vec3{0.0, 1.0, 0.0} : Vec3{1.0, 0.0, 0.0})), v = cross(n, u);
	for (int ring = 0; ring < 2; ++ring) {
		const double a = ring ? a1 : a0;
		for (int i = 0; i <= segments; ++i) {
			const double phi = 2.0 * kPi * i / segments;
			const Vec3 dir = n * std::cos(a) + (u * std::cos(phi) + v * std::sin(phi)) * std::sin(a), p = c + dir * r;
			m.verts.push_back({-p.x, p.y, p.z}), m.normals.push_back({-dir.x, dir.y, dir.z});
		}
	}
	const int w = segments + 1;
	for (int i = 0; i < segments; ++i) m.tris.push_back({i, i + 1, w + i + 1}), m.tris.push_back({i, w + i + 1, w + i});
	return m;
}

// The battle at nature's time `nt` as scene text: the bolts in flight, the sparks where they strike, the shields' ripples, the explosions.
inline std::string battleText(const Battle &b, const std::vector<cosmos::Ship> &ships, double nt) {
	std::string out;
	for (const Bolt &bolt : boltsOf(b, ships)) {
		const double span = std::max(1e-6, bolt.t1 - bolt.t0);
		if (nt >= bolt.t0 && nt < bolt.t1) {   // in flight: a rod a sixth of its way long (at most 30 m), its head where it has got to
			const double u = (nt - bolt.t0) / span, len = std::min(30.0, length(bolt.end - bolt.start) / 6.0);
			const Vec3 dir = normalize(bolt.end - bolt.start), head = bolt.start + (bolt.end - bolt.start) * u;
			const Vec3 tail = head - dir * std::min(len, length(head - bolt.start));
			const double r = std::max(0.05, 0.04 * len);   // (its colour round a white-hot core)
			out += "# a laser bolt (film_battle.h)\n" + glowRod(tail, head, r, bolt.colour * 4.0) + glowRod(tail + dir * (0.1 * len), head + dir * (0.02 * len), 0.45 * r, Vec3{1.0, 1.0, 1.0} * 6.0);
		}
		const double since = nt - bolt.t1;
		if (since >= 0.0 && since < 0.3) {   // the strike: a spark flaring and gone
			const double f = 1.0 - since / 0.3, r = std::max(0.3, length(bolt.end - bolt.start) / 60.0) * (0.6 + 0.8 * f);
			out += emissiveSphere({-bolt.end.x, bolt.end.y, bolt.end.z}, r, (bolt.colour * 0.5 + Vec3{0.6, 0.6, 0.6}) * (30.0 * f * f));
		}
		if (bolt.shield && since >= 0.0 && since < 1.0) {   // the shield's ripple: a ring of light spreading from the hit over the bubble, fading as it goes
			const double r = length(bolt.end - bolt.shieldCentre), spread = 0.2 + 1.2 * since, fade = (1.0 - since) * (1.0 - since);
			const Vec3 n = normalize(bolt.end - bolt.shieldCentre);
			const Vec3 glow = bolt.shield->colour * (1.6 * fade);
			out += "# a shield struck (film_battle.h)\n" + attr(diffuseMaterial({0.0, 0.0, 0.0}), emitMesh(sphereBand(bolt.shieldCentre, r, n, std::max(0.0, spread - 0.05), spread), identity()), &glow);
			const Vec3 core = bolt.shield->colour * (1.2 * fade * std::max(0.0, 1.0 - 4.0 * since));
			if (length(core) > 0.01) out += attr(diffuseMaterial({0.0, 0.0, 0.0}), emitMesh(sphereBand(bolt.shieldCentre, r * 1.002, n, 0.0, 0.25, 24), identity()), &core);
		}
	}
	for (const Explosion &e : b.explosions) {
		const double since = nt - e.t;
		if (since < 0.0 || since > 30.0) continue;
		const cosmos::Ship *ship = e.ship.empty() ? nullptr : shipNamed(ships, e.ship);
		const Vec3 c = ship ? shipPoint(*ship, e.t) : e.at;
		const Vec3 drift = ship && ship->orbit.on ? (shipPoint(*ship, e.t + 0.05) - shipPoint(*ship, e.t - 0.05)) * 10.0 : (ship ? ship->move : Vec3{});   // (the wreck flies on as the ship did)
		const double size = e.size > 0.0 ? e.size : (ship ? ship->size : 20.0);
		const Vec3 centre = c + drift * since, story{-centre.x, centre.y, centre.z};
		out += "# an explosion (film_battle.h)\n";
		if (since < 0.5) {   // the flash: blinding, swelling and gone in half a second
			const double f = 1.0 - since / 0.5;
			out += emissiveSphere(story, size * (0.3 + 1.2 * std::sqrt(since / 0.5)), Vec3{1.0, 0.95, 0.8} * (60.0 * f * f));
		}
		if (since < 1.4) {   // the fireball: a cluster of balls of fire, yellow to red, swelling as it burns out (nothing to feed it, no smoke)
			const double u = since / 1.4;
			for (int k = 0; k < 7; ++k) {
				const Vec3 off = Vec3{hash01(e.seed * 31 + k, 1) - 0.5, hash01(e.seed * 31 + k, 2) - 0.5, hash01(e.seed * 31 + k, 3) - 0.5} * (size * (0.4 + 1.1 * u));
				const Vec3 fire = lerp(Vec3{1.0, 0.75, 0.3}, Vec3{0.9, 0.2, 0.05}, u) * (14.0 * (1.0 - u) * (1.0 - u));
				out += emissiveSphere(story + Vec3{-off.x, off.y, off.z}, size * (0.25 + 0.45 * u) * (0.7 + 0.6 * hash01(e.seed * 31 + k, 4)), fire);
			}
		}
		if (since < 3.0) {   // the shock ring: a thin ring of light racing out, tipped at random
			const double u = since / 3.0, radius = size * (0.5 + 5.0 * u);
			const Vec3 glow = Vec3{0.6, 0.8, 1.0} * (5.0 * (1.0 - u) * (1.0 - u));
			const double tipA = 360.0 * hash01(e.seed, 5), tipB = 60.0 * (hash01(e.seed, 6) - 0.5);
			out += attr(diffuseMaterial({0.0, 0.0, 0.0}), emitMesh(torus(radius, size * 0.04 * (1.0 + u), 64, 6), mul(translate(story.x, story.y, story.z), rotate({0.0, 1.0, 0.0}, tipA), rotate({1.0, 0.0, 0.0}, tipB))), &glow);
		}
		// the wreck: pieces of its hull flying apart and tumbling on, and embers cooling
		const Vec3 hull = ship ? ship->colour : Vec3{0.5, 0.5, 0.5};
		for (int k = 0; k < 16; ++k) {
			const Vec3 dir = normalize(Vec3{hash01(e.seed * 53 + k, 1) - 0.5, hash01(e.seed * 53 + k, 2) - 0.5, hash01(e.seed * 53 + k, 3) - 0.5} + Vec3{1e-6, 0.0, 0.0});
			const double speed = size * (0.4 + 1.2 * hash01(e.seed * 53 + k, 4)), piece = size * (0.06 + 0.12 * hash01(e.seed * 53 + k, 5));
			const Vec3 p = story + Vec3{-dir.x, dir.y, dir.z} * (speed * since);
			const Vec3 axis = normalize(Vec3{hash01(k, 6) - 0.5, hash01(k, 7) - 0.5, hash01(k, 8) - 0.5} + Vec3{0.0, 1e-6, 0.0});
			const double turn = (90.0 + 400.0 * hash01(e.seed * 53 + k, 9)) * since;
			const Mat4 place = mul(translate(p.x, p.y, p.z), rotate(axis, turn), scale(piece, piece * 0.3, piece * 0.7));
			out += attr(diffuseMaterial(hull * (0.5 + 0.5 * hash01(k, 10))), emitMesh(box(1.0, 1.0, 1.0), place));
			if (k % 3 == 0 && since < 4.0) out += emissiveSphere(p, piece * 0.25, Vec3{1.0, 0.45, 0.1} * (8.0 * (1.0 - since / 4.0)));   // (embers)
		}
	}
	return out;
}

// When a ship is gone (an explosion's moment; below 0: never).
inline double goneAt(const Battle &b, const std::string &ship) {
	double t = -1.0;
	for (const Explosion &e : b.explosions)
		if (e.ship == ship && (t < 0.0 || e.t < t)) t = e.t;
	return t;
}

// The battle's sound: each bolt's zap from its gun, each strike's crack, each explosion's boom and rumble (heard at once, quieter far off: film_voyage.h's `heard`).
inline void battleSound(audio::Mix &m, const soundscape::Ears &ears, const Battle &b, const std::vector<cosmos::Ship> &ships, const std::function<double(double)> &natureAt) {
	const double T = m.seconds();
	const auto filmTime = [&](double nt) {   // (the film time nature's time `nt` comes at: the first film time at or after it)
		for (double t = 0.0; t < T; t += 1.0 / 48.0)
			if ((natureAt ? natureAt(t) : t) >= nt) return t;
		return -1.0;
	};
	if (natureAt && natureAt(0.0) > 0.0) {}   // (a bolt or a blast before the film began is not heard: filmTime finds none for it)
	unsigned seed = 3101u;
	for (const Bolt &bolt : boltsOf(b, ships)) {
		const double t0 = filmTime(bolt.t0), t1 = filmTime(bolt.t1);
		if (t0 >= 0.0 && (natureAt ? natureAt(0.0) : 0.0) <= bolt.t0) cosmos::heard(m, ears, t0, bolt.start, audio::chirp(2400.0, 380.0, 0.22, 0.0, 0.0), 0.35, 40.0);
		if (t1 >= 0.0 && (natureAt ? natureAt(0.0) : 0.0) <= bolt.t1) cosmos::heard(m, ears, t1, bolt.end, audio::noiseBurst(0.3, 0.002, 0.2, 400.0, 5000.0, seed++), 0.4, 40.0);
	}
	for (const Explosion &e : b.explosions) {
		const double t = filmTime(e.t);
		if (t < 0.0 || (natureAt ? natureAt(0.0) : 0.0) > e.t) continue;
		const cosmos::Ship *ship = e.ship.empty() ? nullptr : shipNamed(ships, e.ship);
		const Vec3 c = ship ? shipPoint(*ship, e.t) : e.at;
		const double size = e.size > 0.0 ? e.size : (ship ? ship->size : 20.0);
		cosmos::heard(m, ears, t, c, audio::noiseBurst(4.0, 0.003, 2.5, 30.0, 2500.0, seed++), 1.0, 10.0 * size);
		const std::vector<std::vector<float>> after = soundscape::roar(25.0, 300.0, 1.0, seed++);
		for (int g = 0; g < 5 && t + 0.6 + g < T; ++g) cosmos::heard(m, ears, t + 0.6 + g, c, after[static_cast<std::size_t>(g) % after.size()], 0.4 * std::exp(-g / 2.0), 10.0 * size);
	}
}

inline std::string laserUsage() { return "`laser FROM at T [to SHIP|X Y Z] [colour red|green|blue|R G B] [count N every S] [speed V]` (ships named before it; count 1 to 50)"; }
inline std::string explodeUsage() { return "`explode SHIP|X Y Z at T [size S]` (a ship named before it)"; }
inline std::string shieldUsage() { return "`shield SHIP [radius R] [colour R G B]` (a ship named before it)"; }

}  // namespace battle
}  // namespace cartoon
