#pragma once
// film_interior.h -- the insides of ships and stations for a film (film_file.h `interior`; round 11), each built round where it stands and the way it faces:
//
//   corridor   a six-sided passage of panels, ribbed every metre and a half, strips of light along its upper corners, a dark grating for a floor, small lights blinking
//              on its walls; open at both ends (to join another, or a cockpit)
//   cockpit    a room with a great window in front onto space (the film's `space` shows through it), a console of glowing screens under it, two seats, a light above
//   airlock    a small room between two doors, warning stripes on its walls; `cycle T`: from T the inner door shuts, amber lights turn while the air is pumped out, and the
//              outer door slides open in a burst of venting mist
//   ring       a stretch of a ring station's floor: it curves up ahead and behind out of sight (the station turning gives its weight), lit by strips along its
//              walls, its ceiling broken by skylights
//
// Coordinates are pbrt's; `facing` as a character's (0 +z, 90 +x): a corridor runs along it, a cockpit and an airlock look along it.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "cartoon_lib.h"

namespace cartoon {
namespace interior {

struct Interior {
	std::string kind = "corridor";
	Vec3 at;              // pbrt's: the middle of its floor at its start
	double facing = 0.0, length = 10.0;
	Vec3 light{1.0, 0.95, 0.85};   // the colour of its lights
	double cycle = -1.0;  // (an airlock) when it cycles (nature's time; below 0: never - its inner door open)
	int seed = 1;
};
inline const std::vector<std::string> &kinds() {
	static const std::vector<std::string> k = {"corridor", "cockpit", "airlock", "ring"};
	return k;
}

namespace detail {
// Its frame: local x across (to its right as it faces), y up, z along the way it faces; story space, as a mesh is.
inline Mat4 frameOf(const Interior &in) { return mul(translate(-in.at.x, in.at.y, in.at.z), rotate({0.0, 1.0, 0.0}, -in.facing)); }
inline std::string slab(const Mat4 &F, Vec3 centre, Vec3 size, const std::string &material, const Vec3 *glow = nullptr) {
	return attr(material, emitMesh(box(size.x, size.y, size.z), mul(F, translate(centre.x, centre.y, centre.z))), glow);
}
}  // namespace detail

inline std::string corridorText(const Interior &in, double nt) {
	using namespace detail;
	const Mat4 F = frameOf(in);
	const double R = 1.4, L = in.length;   // (the hexagon's radius: some 2.4 m across)
	const std::string panel = diffuseMaterial({0.72, 0.73, 0.75}), dark = diffuseMaterial({0.12, 0.12, 0.13}), rib = metalMaterial({0.55, 0.56, 0.58}, 0.3);
	std::string out = "# a corridor (film_interior.h)\n";
	const double centreY = R * std::cos(kPi / 6.0);   // (the floor flat at y = 0)
	for (int k = 0; k < 6; ++k) {   // the six sides, the floor a grating
		const double ang = -kPi / 2.0 + kPi / 3.0 * k;   // (the middle of each side, the floor's at -90 degrees)
		const Vec3 mid{std::cos(ang) * centreY, centreY + std::sin(ang) * centreY, 0.5 * L};
		const Mat4 side = mul(F, translate(mid.x, mid.y, mid.z), rotate({0.0, 0.0, 1.0}, ang * 180.0 / kPi + 90.0));
		out += attr(k == 0 ? dark : panel, emitMesh(box(R, 0.04, L), side));
	}
	for (double z = 0.75; z < L; z += 1.5)   // ribs: a hexagonal ring of bars
		for (int k = 0; k < 6; ++k) {
			const double ang = -kPi / 2.0 + kPi / 3.0 * k;
			const Vec3 mid{std::cos(ang) * (centreY - 0.06), centreY + std::sin(ang) * (centreY - 0.06), z};
			out += attr(rib, emitMesh(box(R, 0.1, 0.12), mul(F, translate(mid.x, mid.y, mid.z), rotate({0.0, 0.0, 1.0}, ang * 180.0 / kPi + 90.0))));
		}
	const Vec3 strip = in.light * 2.5;   // light strips along the two upper corners
	for (const double side : {-1.0, 1.0}) {
		const Vec3 corner{side * R * 0.5, centreY + centreY - 0.05, 0.5 * L};
		out += slab(F, {corner.x * 0.98, corner.y - 0.02, corner.z}, {0.08, 0.03, L * 0.98}, diffuseMaterial({0.0, 0.0, 0.0}), &strip);
	}
	for (int k = 0; k < static_cast<int>(L / 2.0); ++k) {   // small lights on the walls, blinking
		const double z = 1.0 + 2.0 * k, side = k % 2 ? 1.0 : -1.0;
		const bool on = std::fmod(nt * (0.7 + 0.3 * hash01(in.seed * 31 + k, 1)) + hash01(in.seed * 31 + k, 2), 1.0) < 0.5;
		const Vec3 c = (hash01(in.seed * 31 + k, 3) < 0.5 ? Vec3{0.2, 1.0, 0.3} : Vec3{1.0, 0.3, 0.15}) * (on ? 6.0 : 0.3);
		out += emissiveSphere(transformPoint(F, {side * (R - 0.08), 1.1, z}), 0.025, c);
	}
	return out;
}

inline std::string cockpitText(const Interior &in, double nt) {
	using namespace detail;
	const Mat4 F = frameOf(in);
	const double W = 4.0, H = 2.6, D = 3.4;   // (across, high, deep: the window at its front, z = D)
	const std::string wall = diffuseMaterial({0.6, 0.62, 0.65}), dark = diffuseMaterial({0.1, 0.1, 0.11}), seat = diffuseMaterial({0.25, 0.2, 0.18});
	std::string out = "# a cockpit (film_interior.h)\n";
	out += slab(F, {0.0, -0.05, 0.5 * D}, {W, 0.1, D}, dark) + slab(F, {0.0, H + 0.05, 0.5 * D}, {W, 0.1, D}, wall);
	out += slab(F, {-0.5 * W - 0.05, 0.5 * H, 0.5 * D}, {0.1, H, D}, wall) + slab(F, {0.5 * W + 0.05, 0.5 * H, 0.5 * D}, {0.1, H, D}, wall);
	out += slab(F, {0.0, 0.5 * H, -0.05}, {W, H, 0.1}, wall);
	// the front: a frame round the great window (open: what is outside shows through)
	out += slab(F, {0.0, 0.45, D}, {W, 0.9, 0.12}, wall) + slab(F, {0.0, H - 0.15, D}, {W, 0.3, 0.12}, wall);
	out += slab(F, {-0.5 * W + 0.2, 0.5 * H, D}, {0.4, H, 0.12}, wall) + slab(F, {0.5 * W - 0.2, 0.5 * H, D}, {0.4, H, 0.12}, wall);
	out += slab(F, {0.0, 0.5 * (0.9 + H - 0.3), D}, {0.08, H - 1.2, 0.1}, dark);   // (its middle strut)
	// the console under the window, sloping, its screens glowing
	out += attr(dark, emitMesh(box(W - 0.6, 0.08, 0.7), mul(F, translate(0.0, 0.95, D - 0.45), rotate({1.0, 0.0, 0.0}, -20.0))));
	for (int k = 0; k < 5; ++k) {
		const double x = -1.4 + 0.7 * k, flick = 0.85 + 0.15 * std::sin(nt * (1.0 + k) + k);
		const Vec3 glow = (k % 2 ? Vec3{0.2, 0.7, 1.0} : Vec3{0.3, 1.0, 0.5}) * (1.6 * flick);
		out += attr(diffuseMaterial({0.0, 0.0, 0.0}), emitMesh(box(0.5, 0.012, 0.32), mul(F, translate(x, 1.0, D - 0.45), rotate({1.0, 0.0, 0.0}, -20.0))), &glow);
	}
	for (int k = 0; k < 14; ++k) {   // buttons blinking along its edge
		const bool on = hash01(in.seed * 17 + k, static_cast<int>(nt * 3.0)) < 0.6;
		const Vec3 c = (k % 3 == 0 ? Vec3{1.0, 0.25, 0.1} : k % 3 == 1 ? Vec3{0.2, 1.0, 0.3} : Vec3{1.0, 0.85, 0.2}) * (on ? 5.0 : 0.2);
		out += emissiveSphere(transformPoint(F, {-1.6 + 0.25 * k, 1.09, D - 0.75}), 0.018, c);
	}
	for (const double x : {-0.8, 0.8}) {   // two seats
		out += slab(F, {x, 0.5, D - 1.4}, {0.6, 0.12, 0.6}, seat) + slab(F, {x, 0.95, D - 1.72}, {0.6, 0.9, 0.12}, seat) + slab(F, {x, 0.25, D - 1.4}, {0.12, 0.5, 0.12}, dark);
	}
	const Vec3 lamp = in.light * 3.0;   // the light over it
	out += slab(F, {0.0, H - 0.01, 0.4 * D}, {1.6, 0.02, 0.4}, diffuseMaterial({0.0, 0.0, 0.0}), &lamp);
	return out;
}

// How far each door of an airlock is open (0 shut .. 1) at nature's time nt, and how strongly its warning lights turn and its mist vents.
inline void airlockState(const Interior &in, double nt, double &inner, double &outer, double &warn, double &vent) {
	inner = 1.0, outer = 0.0, warn = 0.0, vent = 0.0;
	if (in.cycle < 0.0) return;
	const double u = nt - in.cycle;
	inner = 1.0 - smoothstep(0.0, 1.2, u);
	warn = smoothstep(1.0, 1.4, u) * (1.0 - smoothstep(6.0, 6.5, u));
	outer = smoothstep(4.5, 6.0, u);
	vent = u > 4.5 ? std::exp(-(u - 4.5) / 0.9) : 0.0;
}
inline std::string airlockText(const Interior &in, double nt) {
	using namespace detail;
	const Mat4 F = frameOf(in);
	const double W = 2.4, H = 2.5, D = std::clamp(in.length, 2.5, 6.0);
	const std::string wall = diffuseMaterial({0.62, 0.62, 0.6}), dark = diffuseMaterial({0.12, 0.12, 0.13}), door = diffuseMaterial({0.5, 0.52, 0.55});
	const std::string yellow = diffuseMaterial({0.85, 0.65, 0.05}), black = diffuseMaterial({0.05, 0.05, 0.05});
	double inner = 0.0, outer = 0.0, warn = 0.0, vent = 0.0;
	airlockState(in, nt, inner, outer, warn, vent);
	std::string out = "# an airlock (film_interior.h)\n";
	out += slab(F, {0.0, -0.05, 0.5 * D}, {W, 0.1, D}, dark) + slab(F, {0.0, H + 0.05, 0.5 * D}, {W, 0.1, D}, wall);
	for (const double s : {-1.0, 1.0}) {
		out += slab(F, {s * (0.5 * W + 0.05), 0.5 * H, 0.5 * D}, {0.1, H, D}, wall);
		for (int k = 0; k < 8; ++k)   // warning stripes along the walls
			out += attr(k % 2 ? yellow : black, emitMesh(box(0.02, 0.2, 0.3), mul(F, translate(s * (0.5 * W - 0.01), 0.35, 0.3 + 0.32 * k), rotate({1.0, 0.0, 0.0}, 35.0))));
	}
	// the doors: two halves at each end, sliding apart into the walls
	for (int end = 0; end < 2; ++end) {
		const double open = end == 0 ? inner : outer, z = end == 0 ? 0.0 : D;
		for (const double s : {-1.0, 1.0}) out += slab(F, {s * (0.25 * W + 0.5 * W * open), 0.5 * H, z}, {0.5 * W, H, 0.08}, door);
		out += slab(F, {0.0, H - 0.1, z}, {W, 0.2, 0.12}, yellow);
	}
	// the warning lights: two amber beacons, their beams turning
	for (const double s : {-1.0, 1.0}) {
		const Vec3 base{s * (0.5 * W - 0.15), H - 0.25, 0.5 * D};
		out += emissiveSphere(transformPoint(F, base), 0.06, Vec3{1.0, 0.55, 0.05} * (0.3 + 4.0 * warn));
		if (warn > 0.01) {
			const double a = nt * 6.0 + (s > 0 ? kPi : 0.0);
			const Vec3 beam = base + Vec3{0.25 * std::cos(a), 0.0, 0.25 * std::sin(a)};
			out += emissiveSphere(transformPoint(F, beam), 0.035, Vec3{1.0, 0.5, 0.05} * (8.0 * warn));
		}
	}
	const Vec3 lamp = in.light * (2.0 * (1.0 - 0.6 * warn));
	out += slab(F, {0.0, H - 0.01, 0.5 * D}, {0.8, 0.02, D * 0.6}, diffuseMaterial({0.0, 0.0, 0.0}), &lamp);
	if (vent > 0.01) {   // the mist venting out through the outer door
		const double since = nt - (in.cycle + 4.5);
		for (int k = 0; k < 36; ++k) {
			const double u = std::fmod(hash01(in.seed * 13 + k, 1) + since * 0.8, 1.0), spread = 0.2 + 1.6 * u;
			const Vec3 p{(hash01(k, 2) - 0.5) * W * 0.6 * spread, 0.3 + (H - 0.6) * hash01(k, 3), D - 0.5 + 6.0 * u};
			out += attr(diffuseMaterial(Vec3{0.85, 0.87, 0.9} * (0.6 + 0.4 * vent)), emitMesh(uvSphere((0.08 + 0.25 * u) * (0.4 + 0.6 * vent), 10, 5), mul(F, translate(p.x, p.y, p.z))));
		}
	}
	return out;
}

inline std::string ringText(const Interior &in, double nt) {
	using namespace detail;
	(void)nt;
	const Mat4 F = frameOf(in);
	const double R = std::max(20.0, in.length * 4.0), W = 6.0, H = 3.2;   // (the ring's radius - its floor curving up ahead and behind - its width and height)
	const std::string floor = diffuseMaterial({0.55, 0.56, 0.58}), wall = diffuseMaterial({0.7, 0.71, 0.73}), trim = diffuseMaterial({0.25, 0.4, 0.55});
	std::string out = "# a ring station's floor (film_interior.h)\n";
	const double span = 1.25;   // (radians each way: up out of sight)
	const int n = 60;
	const Vec3 strip = in.light * 2.2;
	for (int k = -n; k < n; ++k) {
		const double a0 = span * k / n, a1 = span * (k + 1) / n, a = 0.5 * (a0 + a1), len = R * (a1 - a0) * 1.02;
		// a point of the floor at angle a: the axis of the ring is R above the start, along x
		const Vec3 p{0.0, R - R * std::cos(a), R * std::sin(a)};
		const Mat4 seg = mul(F, translate(p.x, p.y, p.z), rotate({1.0, 0.0, 0.0}, -a * 180.0 / kPi));
		out += attr(floor, emitMesh(box(W, 0.1, len), mul(seg, translate(0.0, -0.05, 0.0))));
		for (const double s : {-1.0, 1.0}) {
			out += attr(wall, emitMesh(box(0.1, H, len), mul(seg, translate(s * 0.5 * W, 0.5 * H, 0.0))));
			out += attr(trim, emitMesh(box(0.12, 0.15, len), mul(seg, translate(s * (0.5 * W - 0.02), 0.08, 0.0))));
			out += attr(diffuseMaterial({0.0, 0.0, 0.0}), emitMesh(box(0.05, 0.06, len * 0.9), mul(seg, translate(s * (0.5 * W - 0.06), H - 0.4, 0.0))), &strip);
		}
		if ((k & 3) != 0) out += attr(wall, emitMesh(box(W, 0.1, len), mul(seg, translate(0.0, H + 0.05, 0.0))));   // (its ceiling, a skylight every fourth)
	}
	return out;
}

inline std::string interiorText(const Interior &in, double nt) {
	if (in.kind == "cockpit") return cockpitText(in, nt);
	if (in.kind == "airlock") return airlockText(in, nt);
	if (in.kind == "ring") return ringText(in, nt);
	return corridorText(in, nt);
}
inline std::string usage() { return "`interior corridor|cockpit|airlock|ring at X Y Z [facing DEGREES] [length L] [light R G B] [cycle T] [seed N]` (length 2 to 200 m; `cycle`: an airlock's)"; }

}  // namespace interior
}  // namespace cartoon
