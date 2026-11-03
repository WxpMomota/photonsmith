#pragma once
// film_homefun.h -- a home's night and its slapstick (indoor round 2): a torch's beam, lightning flashing in at the windows, lamps flickering, shadow play on a wall, and things
// that fall and break, puddles, piles that topple, wet footprints (film_rooms.h Home; film_parse_home.h reads the lines, film_homeworld.cpp plays them).
//
//   torch WHO at T [until T]                    a torch in its hand, held up, a cone of light ahead of it
//   flicker ROOM [LAMP] at T [until T]          its lamps (or one) flickering, now and then nearly out
//   (lightning, film_parse_nature.h)            each strike flashes in at the windows, bluish white; the thunder follows as far behind as it is
//   shadowplay WHO [animal rabbit|bird|wolf] at T [until T]
//                                               a bright bulb on a stand behind it, the room's lamps turned down: its shadow on the wall it faces, bigger the nearer it stands to the
//                                               bulb; `animal`: its hands make one in front of it, its ears twitching, its wings flapping, its jaw snapping
//   fall THING at T          knock PET THING at T
//                                               a thing tipped off its piece: it tumbles to the floor and breaks (a mug, a plate, a bowl, a vase, a bottle: in pieces, and what was
//                                               in it spilt) or bounces and rolls
//   puddle ROOM X Z at T [size R]               a spill on the floor (pbrt's X Z); whoever walks over one slips, feet up, onto its back, and gets up again
//   pile ROOM books|boxes|plates [on ITEM] falls T
//                                               a stack (on the floor by a wall, or on ITEM) that topples at T, its pieces scattering
//   (a door opened into someone just inside it knocks them back; feet wet from a bath or a shower leave prints on the floor for a while)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_homelife.h"
#include "film_rooms.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace rooms {

// ---- the night ----
// A torch held at `hand` (story space) pointing `ahead`: its body, and its beam as a spot light (pbrt's), `on` 0 .. 1.
inline std::string torchText(Vec3 hand, Vec3 ahead, double on) {
	const Vec3 dir = normalize(ahead);
	const Mat4 M = alignY(hand - dir * 0.06, dir);
	Mesh body;
	detail::addMesh(body, lathe({{0.0, 0.0}, {0.018, 0.0}, {0.018, 0.12}, {0.028, 0.15}, {0.028, 0.17}, {0.0, 0.17}}, 14), M);
	std::string out = attr(coated({0.75, 0.12, 0.1}, 0.3), emitMesh(body, identity()));
	if (on <= 0.01) return out;
	const Vec3 lens = hand + dir * 0.112, to = lens + dir * 3.0;
	out += emissiveSphere(lens, 0.022, Vec3{1.0, 0.95, 0.8} * (12.0 * on));
	char buf[320];
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  LightSource \"spot\" \"point3 from\" [ %.4f %.4f %.4f ] \"point3 to\" [ %.4f %.4f %.4f ] \"float coneangle\" [ 17 ] \"float conedelta\" [ 6 ] \"rgb I\" [ %.4f %.4f %.4f ]\nAttributeEnd\n",
				  -(lens.x + dir.x * 0.03), lens.y + dir.y * 0.03, lens.z + dir.z * 0.03, -to.x, to.y, to.z, 7.0 * on, 6.6 * on, 5.6 * on);
	return out + buf;
}
// A flash of lightning in at a room's windows (`flash` 0 .. 1: the strike's brightness now): a cold, hard light from each.
inline std::string flashText(const Room &r, double flash) {
	if (flash <= 0.01) return std::string();
	const Mat4 F = frameOf(r);
	std::string out;
	for (const Opening &o : r.openings) {
		if (o.door) continue;
		const Vec3 at = wallPoint(r, o.wall, o.along, o.sill + 0.7 * o.height), in = wallInward(o.wall);
		const Vec3 from = transformPoint(F, at - in * 0.3), to = transformPoint(F, at + in * 2.0 + Vec3{0.0, -1.4, 0.0});
		const Vec3 I = Vec3{0.78, 0.84, 1.0} * (40.0 * o.width * o.height * flash);
		char buf[320];
		std::snprintf(buf, sizeof buf, "AttributeBegin\n  LightSource \"spot\" \"point3 from\" [ %.4f %.4f %.4f ] \"point3 to\" [ %.4f %.4f %.4f ] \"float coneangle\" [ 60 ] \"float conedelta\" [ 15 ] \"rgb I\" [ %.4f %.4f %.4f ]\nAttributeEnd\n",
					  -from.x, from.y, from.z, -to.x, to.y, to.z, I.x, I.y, I.z);
		out += buf;
	}
	return out;
}

// ---- shadow play ----
// The bulb behind a character playing shadows (story space: its feet, its forward, its height): where it is.
inline Vec3 shadowBulb(Vec3 feet, Vec3 forward, double height) { return feet - forward * (0.55 * height + 0.6) + Vec3{0.0, 0.62 * height, 0.0}; }
// The bulb and its stand, its light (pbrt's point light).
inline std::string shadowLampText(Vec3 bulb, double floorY, double on) {
	Mesh stand;
	const double tall = bulb.y - floorY - 0.04;
	detail::addMesh(stand, lathe({{0.0, 0.0}, {0.14, 0.0}, {0.14, 0.02}, {0.015, 0.03}, {0.015, tall}, {0.0, tall}}, 14), translate(bulb.x, floorY, bulb.z));
	std::string out = attr(coated({0.08, 0.08, 0.08}, 0.3), emitMesh(stand, identity()));
	if (on <= 0.01) return out;
	out += emissiveSphere(bulb, 0.03, Vec3{1.0, 0.92, 0.75} * (5.0 * on));
	char buf[200];
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  LightSource \"point\" \"point3 from\" [ %.4f %.4f %.4f ] \"rgb I\" [ %.4f %.4f %.4f ]\nAttributeEnd\n", -bulb.x, bulb.y + 0.06, bulb.z, 3.2 * on, 3.0 * on, 2.6 * on);
	return out + buf;
}
inline const std::vector<std::string> &shadowAnimals() {
	static const std::vector<std::string> k = {"rabbit", "bird", "wolf"};
	return k;
}
// The shape a pair of hands makes for an animal's shadow, at `at` (story space) facing `forward` (its flat side to the bulb behind): thin, dark - hands seen from behind.
inline std::string shadowAnimalText(const std::string &animal, Vec3 at, Vec3 forward, double t, double amount) {
	if (amount <= 0.01) return std::string();
	const Vec3 f = normalize(Vec3{forward.x, 0.0, forward.z}), side{-f.z, 0.0, f.x};
	const double s = 0.6 + 0.4 * amount;
	const auto blob = [&](Mesh &m, double x, double y, double rx, double ry, double tilt) {   // (an ellipse in the plane across `forward`: x to its side, y up)
		const Mat4 M = mul(translate(at + side * (x * s) + Vec3{0.0, y * s, 0.0}), rotate(f, tilt), Mat4{side.x, 0.0, f.x, 0.0, side.y, 1.0, f.y, 0.0, side.z, 0.0, f.z, 0.0, 0.0, 0.0, 0.0, 1.0});   // (row-major: its columns side, up, forward)
		detail::addMesh(m, uvSphere(1.0, 14, 8), mul(M, scale(rx * s, ry * s, 0.012)));
	};
	Mesh hands;
	if (animal == "rabbit") {   // a head, two long ears twitching
		blob(hands, 0.0, 0.0, 0.07, 0.06, 0.0);
		blob(hands, 0.06, -0.01, 0.04, 0.03, 0.0);
		for (const double e : {-1.0, 1.0}) blob(hands, -0.02 + 0.03 * e, 0.11, 0.022, 0.08, 12.0 * e + 10.0 * std::sin(5.0 * t + e) * (std::sin(1.3 * t) > 0.6 ? 1.0 : 0.15));
	} else if (animal == "bird") {   // a body and two wings beating
		blob(hands, 0.0, 0.0, 0.08, 0.035, 0.0);
		blob(hands, 0.09, 0.015, 0.03, 0.02, 0.0);
		const double flap = 35.0 * std::sin(6.0 * t);
		for (const double e : {-1.0, 1.0}) blob(hands, -0.01, 0.05 * e + 0.02, 0.06, 0.02, 90.0 * e + flap * e);
	} else {   // a wolf's head: an ear up, a long snout, its jaw opening and snapping
		blob(hands, 0.0, 0.0, 0.08, 0.06, 0.0);
		blob(hands, -0.04, 0.08, 0.025, 0.05, -15.0);
		blob(hands, 0.1, 0.015, 0.07, 0.025, 0.0);
		const double jaw = 18.0 * std::max(0.0, std::sin(2.2 * t));
		blob(hands, 0.09, -0.035, 0.065, 0.018, -jaw);
	}
	return attr(coated({0.55, 0.38, 0.3}, 0.5), emitMesh(hands, identity()));
}

// ---- things falling ----
// Where a thing falling at `t0` from `from` to land at `to` (both story space, `to` on the floor) is at t, and how far it has turned: a tip off the edge, a fall under
// gravity, a bounce or two where it lands (`breaks`: none - it smashes).
struct Tumble {
	Vec3 at;
	double spin = 0.0;
	bool landed = false;
	double since = 0.0;   // (time since it first hit the floor)
};
inline Tumble tumbleAt(Vec3 from, Vec3 to, double t0, double t, bool breaks) {
	Tumble k;
	const double drop = std::max(0.05, from.y - to.y), fall = std::sqrt(2.0 * drop / 9.81), u = t - t0;
	if (u <= 0.0) {
		k.at = from;
		return k;
	}
	if (u < fall) {
		const double w = u / fall;
		k.at = Vec3{lerp(from.x, to.x, w), from.y - 0.5 * 9.81 * u * u, lerp(from.z, to.z, w)};
		k.spin = 260.0 * u / fall;
		return k;
	}
	k.landed = true;
	k.since = u - fall;
	const double b = breaks ? 0.0 : 0.12 * std::exp(-5.0 * k.since) * std::abs(std::sin(9.0 * k.since));   // (it bounces, less each time)
	const Vec3 roll = breaks ? Vec3{0.0, 0.0, 0.0} : normalize(Vec3{to.x - from.x, 0.0, to.z - from.z} + Vec3{1e-6, 0.0, 0.0}) * (0.25 * (1.0 - std::exp(-2.5 * k.since)));
	k.at = to + roll + Vec3{0.0, b, 0.0};
	k.spin = breaks ? 260.0 : 260.0 + 200.0 * (1.0 - std::exp(-2.5 * k.since));
	return k;
}
// A broken thing's pieces at `since` seconds after it hit the floor at `at` (story space): skittering out, settling.
inline std::string shardsText(const std::string &kind, const std::string &name, Vec3 at, double since) {
	Mesh m;
	for (int q = 0; q < 9; ++q) {
		const double a = 2.0 * kPi * hash01(q, 501) + q, out = 0.12 + 0.28 * hash01(q, 502), u = 1.0 - std::exp(-6.0 * since);
		const Vec3 p = at + Vec3{out * u * std::cos(a), 0.008 + 0.06 * std::sin(kPi * std::min(1.0, since * 3.0)) * (1.0 - u), out * u * std::sin(a)};
		const double s = 0.02 + 0.025 * hash01(q, 503);
		detail::addMesh(m, uvSphere(1.0, 4, 3), mul(translate(p), rotate(normalize(Vec3{hash01(q, 504), 1.0, hash01(q, 505)}), 400.0 * u + 60.0 * q), scale(s, 0.006, s * 0.7)));
	}
	return attr(coated(thingColour(kind, name), 0.3), emitMesh(m, identity()));
}
// A puddle at `at` (story space, on the floor), `radius` across once spread (over a second and a half from `since` 0).
inline std::string puddleText(Vec3 at, double radius, double since) {
	if (since <= 0.0) return std::string();
	const double r = radius * std::sqrt(smoothstep(0.0, 1.5, since));
	Mesh m;
	for (int q = 0; q < 3; ++q) {   // (a blob of three overlapping ones: not a perfect round)
		const double a = 2.1 * q, off = q ? 0.35 * r : 0.0;
		detail::addMesh(m, uvSphere(1.0, 20, 6), mul(translate(at + Vec3{off * std::cos(a), 0.002, off * std::sin(a)}), scale(r * (q ? 0.6 : 0.85), 0.002, r * (q ? 0.5 : 0.7))));
	}
	return attr("Material \"dielectric\" \"float eta\" [ 1.33 ]", emitMesh(m, identity()));
}
// A pile's pieces (local to its foot: x across, y up, z out from the wall it is against), standing or, from `falls`, toppled and scattered.
struct PilePiece {
	Vec3 at;
	double turn = 0.0;
	Vec3 size;
};
inline std::vector<PilePiece> pileOf(const std::string &of, int seed) {
	std::vector<PilePiece> out;
	double y = 0.0;
	const int n = of == "boxes" ? 4 : of == "plates" ? 12 : 9;
	for (int k = 0; k < n; ++k) {
		PilePiece p;
		p.size = of == "boxes" ? Vec3{0.34 - 0.04 * k, 0.26 - 0.03 * k, 0.3 - 0.03 * k} : of == "plates" ? Vec3{0.24, 0.02, 0.24} : Vec3{0.17 + 0.05 * hash01(k, seed), 0.035 + 0.02 * hash01(k, seed + 1), 0.24};
		p.at = Vec3{0.03 * (hash01(k, seed + 2) - 0.5) + 0.01 * k, y, 0.03 * (hash01(k, seed + 3) - 0.5)};
		p.turn = 14.0 * (hash01(k, seed + 4) - 0.5);
		y += p.size.y;
		out.push_back(p);
	}
	return out;
}
// The pile at t (`frame`: its foot's frame, story space; `drop`: how far below its foot the floor is): each piece tipping from the top first, flung out the more the higher it
// stood, landing on the floor and lying there.
inline std::string pileText(const std::string &of, int seed, const Mat4 &frame, double drop, double falls, double t) {
	const std::vector<PilePiece> pile = pileOf(of, seed);
	Mesh m[3];
	for (std::size_t k = 0; k < pile.size(); ++k) {
		const PilePiece &p = pile[k];
		Vec3 at = p.at + Vec3{0.0, 0.5 * p.size.y, 0.0};
		double tip = 0.0, turn = p.turn;
		const double start = falls + 0.06 * (pile.size() - 1 - k), u = t - start;
		if (u > 0.0) {   // (out along +z, the way it leans: a little to either side)
			const double h = p.at.y + 0.5 * p.size.y, flung = 0.25 + 0.9 * h + 0.2 * hash01(static_cast<int>(k), seed + 7), dropBy = h + drop - 0.5 * p.size.y;
			const double fall = std::sqrt(2.0 * std::max(0.02, dropBy) / 9.81), w = std::min(1.0, u / fall), side = 0.5 * (hash01(static_cast<int>(k), seed + 8) - 0.5);
			at = at + Vec3{side * flung * w, -std::min(dropBy, 0.5 * 9.81 * u * u), flung * w};
			tip = (of == "plates" ? 180.0 : 90.0) * std::min(1.0, u / std::max(0.15, fall)) * (k % 2 ? 1.0 : -1.0) * (hash01(static_cast<int>(k), seed + 9) < 0.5 ? 1.0 : 0.6);
			turn += 70.0 * w * (hash01(static_cast<int>(k), seed + 10) - 0.5);
		}
		Mesh piece = of == "plates" ? bitMesh("plate") : box(p.size.x, p.size.y, p.size.z);
		const Mat4 M = mul(frame, translate(at), rotate({0.0, 1.0, 0.0}, turn), rotate({1.0, 0.0, 0.0}, tip), translate(0.0, of == "plates" ? -0.01 : 0.0, 0.0));
		detail::addMesh(m[k % 3], piece, M);
	}
	std::string out;
	for (int k = 0; k < 3; ++k) out += attr(of == "plates" ? coated({0.92, 0.92, 0.9}, 0.2) : coated(of == "boxes" ? Vec3{0.62, 0.46, 0.28} * (0.85 + 0.1 * k) : partyColour(k + seed) * 0.7, 0.4), emitMesh(m[k], identity()));
	return out;
}
// Wet footprints: each (story space, its heading, when it was made) a dark print on the floor, drying away over `dries` seconds.
struct Print {
	Vec3 at;
	double heading = 0.0, t = 0.0;
};
inline std::string printsText(const std::vector<Print> &prints, double t, double dries) {
	Mesh m;
	for (const Print &p : prints) {
		if (t < p.t || t > p.t + dries) continue;
		const double fade = 1.0 - (t - p.t) / dries;
		detail::addMesh(m, uvSphere(1.0, 10, 4), mul(translate(p.at + Vec3{0.0, 0.0015, 0.0}), rotate({0.0, 1.0, 0.0}, p.heading), scale(0.045 * (0.6 + 0.4 * fade), 0.001, 0.1 * (0.6 + 0.4 * fade))));
	}
	return m.tris.empty() ? std::string() : attr("Material \"dielectric\" \"float eta\" [ 1.33 ]", emitMesh(m, identity()));
}

}  // namespace rooms
}  // namespace cartoon
