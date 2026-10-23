#pragma once
// turtle_escape_set.h -- the living room of "The Great Turtle Escape" (turtle_escape.h), built at real size in metres: a wooden plank floor, walls with
// skirting and a cornice, a window with curtains and herbs on the sill, the front door, a skylight over a ceiling fan with a globe light, a sofa, a coffee
// table with what is on it, a rug, a bookshelf full of books, a floor lamp, pictures, a wall clock, a coat by the door, the robot vacuum and its dock, the
// towels, the storage tub with lettuce and a rock in it, a cardboard box - and the turtle tank on its cabinet, with gravel, river rocks, swaying plants, a
// little waterfall, the basking slate, the heat lamp, the thermometer on its suction cup and the canister filter's ribbed hose.
//
// Layout (y up). The back wall is at z = -2.5, with the window (x -2.4 .. -1.2), the clock and the tank on its cabinet (x 0.25 .. 1.15), the filter's canister and the
// towels to its right. The sofa (x -2 .. 0, z -0.4 .. 0.5) faces the tank across the coffee table; the rug lies in front of the sofa (z 0.62 .. 2.0); the tub stands in
// the front right corner (x 1.7 .. 2.32, z 2.05 .. 2.45) with a cardboard box beside it; the door is in the right wall (z 0.70 .. 1.62), the bookshelf on the left wall.
//
// Everything here is a function of the film's time only through `SetState` (the light of the day, the lamp, the door, the hose, the water, the vacuum):
// the film fills one in for each instant.

#include "nature_lib.h"
#include "human_actor.h"
#include "turtle_actor.h"

namespace cartoon {
namespace turtle_escape {

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Where the important things are
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
constexpr double kRoomX0 = -3.0, kRoomX1 = 3.0, kRoomZ0 = -2.5, kRoomZ1 = 2.5, kCeiling = 2.6;
// the tank: inside dimensions; its glass bottom stands on the cabinet
constexpr double kTankX0 = 0.25, kTankX1 = 1.15, kTankZ0 = -2.42, kTankZ1 = -2.03;
constexpr double kTankBottom = 0.76, kTankRim = 1.21, kWater = 1.00;
constexpr double kGravel = 0.79;                         // the top of the gravel
inline Vec3 basking() { return {1.01, 1.035, -2.30}; }    // the top of the basking slate, near the back right corner, just above the water
inline Vec3 thermometerSpot() { return {0.42, 0.93, -2.045}; }   // the thermometer's middle, on the inside of the front glass
inline Vec3 heatLampBulb() { return {0.98, 1.43, -2.27}; }
inline Vec3 canister() { return {1.46, 0.0, -2.28}; }     // the filter's canister on the floor right of the cabinet
inline Vec3 towels() { return {1.48, 0.0, -1.86}; }       // the towel pile, where the hose's outside end comes down
inline Vec3 vacuumDock() { return {2.70, 0.0, 0.10}; }
constexpr double kSofaX0 = -2.0, kSofaX1 = 0.0, kSofaZ0 = -0.40, kSofaZ1 = 0.50, kSofaLegs = 0.105;
constexpr double kRugX0 = -2.4, kRugX1 = 0.6, kRugZ0 = 0.62, kRugZ1 = 2.0, kRugThick = 0.014;
constexpr double kTubX0 = 1.70, kTubX1 = 2.32, kTubZ0 = 2.05, kTubZ1 = 2.45, kTubH = 0.17, kTubWater = 0.125;
inline Vec3 cardboardBox() { return {1.55, 0.0, 2.24}; }  // beside the tub: the turtle's last step
constexpr double kBoxH = 0.10;
inline Vec3 fanCentre() { return {-0.9, kCeiling - 0.32, 1.1}; }
inline Vec3 doorHinge() { return {kRoomX1, 0.0, 0.70}; }  // the door is 0.92 wide, z 0.70 .. 1.62, opening inwards
inline Vec3 clockFace() { return {-0.55, 1.95, kRoomZ0 + 0.035}; }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// What changes during the film
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
enum class Light { Morning, Afternoon, Dusk, Night, RoomLamp };
struct Ripple {
	double x, z, t0, amp;
};
struct SetState {
	double t = 0.0;
	Light light = Light::Morning;
	double heatLamp = 1.0;          // 0 off .. 1 on
	double door = 0.0;              // 0 shut .. 1 wide open (inwards, about the hinge)
	double fanTurn = 0.0;           // the fan's angle, degrees
	double roomLamp = 0.0;          // the globe under the fan, 0 off .. 1 on
	double clockMinutes = 0.0;      // the clock's time, minutes after 8:00
	// the hose: 0 in its place; `hoseSag` the dip at `hoseGrip` (the turtle hanging on it, metres); `hoseOut` 0 .. 1 how far its inside end has
	// slipped out over the rim (1: hanging down outside the tank, as the owner finds it)
	double hoseSag = 0.0;
	Vec3 hoseGrip = {0.0, 0.0, 0.0};
	double hoseOut = 0.0;
	// the rocks the turtle moves: each one's offset from where it starts and its turn (degrees about the vertical, then about z when it topples)
	std::vector<Vec3> rockOffset;
	std::vector<double> rockTilt;
	// the thermometer: on the glass (0), pulled off and floating (1), pushed against the rocks as a ramp (2)
	double thermoState = 0.0;
	Vec3 thermoPos = {0.0, 0.0, 0.0};
	double thermoTilt = 0.0;        // degrees from upright
	std::vector<Ripple> tankRipples, tubRipples;
	std::vector<double> pellets;    // the times pellets were dropped
	double drip = 0.0;              // water dripping from the hose onto the floor (scene 7)
	Vec3 vacuumPos = {2.70, 0.0, 0.10};
	double vacuumYaw = 180.0;
	double vacuumOn = 0.0;
	bool bagsOnFloor = false;
	bool blueprint = false;         // the turtle's escape plan: the room drawn as a plan
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Little builders
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline std::string boxAt(Vec3 centre, Vec3 size, const std::string &material, double yaw = 0.0) {
	return attr(material, emitMesh(box(size.x, size.y, size.z), mul(translate(centre), rotate({0, 1, 0}, -yaw))));
}
inline std::string roundedAt(Vec3 centre, Vec3 size, double r, const std::string &material, double yaw = 0.0) {
	return attr(material, emitMesh(roundedBox(size.x, size.y, size.z, std::min(r, 0.49 * std::min(size.x, std::min(size.y, size.z))), 2), mul(translate(centre), rotate({0, 1, 0}, -yaw))));
}
// A tube along a path of points (a hose, a cord), with corrugations every `rib` metres when rib > 0.
inline Mesh tube(const std::vector<Vec3> &pts, double radius, int sides = 12, double rib = 0.0) {
	Mesh m;
	// a smooth path: Catmull-Rom between the points
	std::vector<Vec3> path;
	for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
		const Vec3 p0 = pts[i == 0 ? 0 : i - 1], p1 = pts[i], p2 = pts[i + 1], p3 = pts[std::min(pts.size() - 1, i + 2)];
		const int steps = std::max(2, static_cast<int>(length(p2 - p1) / 0.015));
		for (int k = 0; k < steps; ++k) {
			const double u = static_cast<double>(k) / steps, u2 = u * u, u3 = u2 * u;
			path.push_back((p1 * 2.0 + (p2 - p0) * u + (p0 * 2.0 - p1 * 5.0 + p2 * 4.0 - p3) * u2 + (p1 * 3.0 - p0 - p2 * 3.0 + p3) * u3) * 0.5);
		}
	}
	path.push_back(pts.back());
	double along = 0.0;
	Vec3 prevN = {0.0, 1.0, 0.0};
	for (std::size_t i = 0; i < path.size(); ++i) {
		if (i > 0) along += length(path[i] - path[i - 1]);
		const Vec3 tang = normalize(path[std::min(path.size() - 1, i + 1)] - path[i == 0 ? 0 : i - 1]);
		Vec3 nrm = normalize(prevN - tang * dot(prevN, tang));
		if (length(nrm) < 1e-6) nrm = normalize(cross(tang, Vec3{1.0, 0.0, 0.0}));
		prevN = nrm;
		const Vec3 bin = cross(tang, nrm);
		const double r = rib > 0.0 ? radius * (1.0 + 0.10 * std::pow(std::abs(std::sin(kPi * along / rib)), 4.0)) : radius;
		for (int s = 0; s <= sides; ++s) {
			const double a = 2.0 * kPi * s / sides;
			const Vec3 d = nrm * std::cos(a) + bin * std::sin(a);
			m.verts.push_back(path[i] + d * r);
			m.normals.push_back(d);
			m.uvs.push_back({static_cast<double>(s) / sides, along});
		}
	}
	const int ring = sides + 1;
	for (std::size_t i = 0; i + 1 < path.size(); ++i)
		for (int s = 0; s < sides; ++s) {
			const int a = static_cast<int>(i) * ring + s, b = a + ring;
			m.tris.push_back({a, b, a + 1});
			m.tris.push_back({a + 1, b, b + 1});
		}
	return m;
}

// A flat rectangle in the plane through `c` with axes `u` and `v` (half sizes), facing `u x v`.
inline Mesh panel(Vec3 c, Vec3 u, Vec3 v) {
	Mesh m;
	const Vec3 n = normalize(cross(u, v));
	for (const auto &k : {std::pair<double, double>{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}) {
		m.verts.push_back(c + u * k.first + v * k.second);
		m.normals.push_back(n);
		m.uvs.push_back({0.5 + 0.5 * k.first, 0.5 + 0.5 * k.second});
	}
	m.tris = {{0, 1, 2}, {0, 2, 3}};
	return m;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The room
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline std::string buildFloor() {
	std::string out;
	// planks along x, 15 cm wide, in lengths of 0.9 to 2.1 m with their joints staggered, in four shades of oak
	// (each plank a placed unit plank, objectDefinitions: four shades)
	int k = 0;
	for (double z = kRoomZ0; z < kRoomZ1 - 1e-6; z += 0.15) {
		double x = kRoomX0 - 0.9 * hash01(k, 3);
		for (int j = 0; x < kRoomX1; ++j) {
			const double len = 0.9 + 1.2 * hash01(k * 31 + j, 4);
			const double a = std::max(x, kRoomX0), b = std::min(x + len, kRoomX1);
			if (b - a > 0.05) out += nature::placeObject("plank" + std::to_string((k + j * 3) % 4), mul(translate((a + b) / 2, -0.01, z + 0.075), scale(b - a - 0.004, 1.0, 1.0)));
			x += len;
		}
		++k;
	}
	out += boxAt({0.0, -0.035, 0.0}, {kRoomX1 - kRoomX0, 0.03, kRoomZ1 - kRoomZ0}, diffuseMaterial({0.12, 0.08, 0.05}));   // the dark gaps between planks
	return out;
}

// A wall from (x0, z0) to (x1, z1) (on the floor), with rectangular holes: each {u0, u1, y0, y1} along the wall from its start.
struct Hole {
	double u0, u1, y0, y1;
};
inline std::string wall(Vec3 a, Vec3 b, const std::vector<Hole> &holes, const std::string &paint) {
	const Vec3 along = normalize(b - a);
	const double len = length(b - a);
	std::vector<double> cuts = {0.0, len};
	for (const Hole &h : holes) {
		cuts.push_back(h.u0);
		cuts.push_back(h.u1);
	}
	std::sort(cuts.begin(), cuts.end());
	std::string shapes;
	const double thick = 0.10;
	const Vec3 out = normalize(cross(Vec3{0.0, 1.0, 0.0}, along));   // away from the room (the walls are given going round it anticlockwise from above)
	for (std::size_t i = 0; i + 1 < cuts.size(); ++i) {
		const double u0 = cuts[i], u1 = cuts[i + 1];
		if (u1 - u0 < 1e-6) continue;
		// the column of wall over [u0, u1]: full height unless a hole covers it
		std::vector<std::pair<double, double>> solid = {{0.0, kCeiling}};
		for (const Hole &h : holes)
			if (h.u0 <= u0 + 1e-6 && h.u1 >= u1 - 1e-6) solid = {{0.0, h.y0}, {h.y1, kCeiling}};
		for (const auto &s : solid) {
			if (s.second - s.first < 1e-6) continue;
			const Vec3 c = a + along * ((u0 + u1) / 2) + Vec3{0.0, (s.first + s.second) / 2, 0.0} + out * (thick / 2);
			const double yaw = std::atan2(along.z, along.x) * 180.0 / kPi;
			shapes += emitMesh(box(u1 - u0, s.second - s.first, thick), mul(translate(c), rotate({0, 1, 0}, -yaw)));
		}
	}
	return attr(paint, shapes);
}

inline std::string buildWalls(const SetState &s) {
	std::string out;
	const std::string paint = diffuseMaterial({0.80, 0.74, 0.64}), back = diffuseMaterial({0.56, 0.66, 0.62});   // the back wall is a sage feature wall
	out += wall({kRoomX0, 0.0, kRoomZ0}, {kRoomX1, 0.0, kRoomZ0}, {{0.6, 1.8, 0.92, 2.18}}, back);       // the window: x -2.4 .. -1.2
	out += wall({kRoomX1, 0.0, kRoomZ1}, {kRoomX0, 0.0, kRoomZ1}, {}, paint);
	out += wall({kRoomX0, 0.0, kRoomZ1}, {kRoomX0, 0.0, kRoomZ0}, {{1.0, 2.6, 0.55, 2.25}}, paint);   // the big window: z 1.5 .. -0.1
	out += wall({kRoomX1, 0.0, kRoomZ0}, {kRoomX1, 0.0, kRoomZ1}, {{3.20, 4.12, 0.0, 2.06}}, paint);     // the door opening: z 0.70 .. 1.62
	// the ceiling, with the skylight's opening over the fan (none in the plan view: the blueprint is lit from above)
	if (!s.blueprint) {
		std::string c;
		const double sx0 = fanCentre().x - 0.45, sx1 = fanCentre().x + 0.45, sz0 = fanCentre().z - 0.45, sz1 = fanCentre().z + 0.45;
		const auto slab = [&](double x0, double x1, double z0, double z1) { if (x1 > x0 && z1 > z0) c += emitMesh(box(x1 - x0, 0.1, z1 - z0), translate((x0 + x1) / 2, kCeiling + 0.05, (z0 + z1) / 2)); };
		slab(kRoomX0, sx0, kRoomZ0, kRoomZ1);
		slab(sx1, kRoomX1, kRoomZ0, kRoomZ1);
		slab(sx0, sx1, kRoomZ0, sz0);
		slab(sx0, sx1, sz1, kRoomZ1);
		out += attr(diffuseMaterial({0.86, 0.84, 0.80}), c);
		// the skylight's well and its glass
		std::string well;
		for (int k = 0; k < 4; ++k) {
			const double x = k < 2 ? (k == 0 ? sx0 : sx1) : (sx0 + sx1) / 2, z = k >= 2 ? (k == 2 ? sz0 : sz1) : (sz0 + sz1) / 2;
			well += emitMesh(box(k < 2 ? 0.04 : 0.94, 0.30, k < 2 ? 0.94 : 0.04), translate(x, kCeiling + 0.15, z));
		}
		out += attr(diffuseMaterial({0.90, 0.89, 0.86}), well);
		out += attr("Material \"thindielectric\" \"float eta\" [ 1.5 ]", emitMesh(panel({(sx0 + sx1) / 2, kCeiling + 0.30, (sz0 + sz1) / 2}, {0.45, 0.0, 0.0}, {0.0, 0.0, -0.45}), identity()));
	}
	// skirting boards and the cornice
	const std::string trim = coated({0.92, 0.91, 0.88}, 0.4);
	std::string trims;
	for (int w = 0; w < 4; ++w) {
		const bool alongX = w < 2;
		const double fixed = w == 0 ? kRoomZ0 + 0.01 : (w == 1 ? kRoomZ1 - 0.01 : (w == 2 ? kRoomX0 + 0.01 : kRoomX1 - 0.01));
		const double len = alongX ? kRoomX1 - kRoomX0 : kRoomZ1 - kRoomZ0;
		const Vec3 sizeSkirt = alongX ? Vec3{len, 0.10, 0.02} : Vec3{0.02, 0.10, len}, sizeCornice = alongX ? Vec3{len, 0.06, 0.05} : Vec3{0.05, 0.06, len};
		const Vec3 c = alongX ? Vec3{0.0, 0.05, fixed} : Vec3{fixed, 0.05, 0.0};
		if (w == 3) {   // the door's wall: skirting either side of the opening
			trims += emitMesh(box(0.02, 0.10, 3.20), translate(fixed, 0.05, kRoomZ0 + 1.60));
			trims += emitMesh(box(0.02, 0.10, 0.88), translate(fixed, 0.05, kRoomZ1 - 0.44));
		} else {
			trims += emitMesh(box(sizeSkirt.x, sizeSkirt.y, sizeSkirt.z), translate(c));
		}
		trims += emitMesh(box(sizeCornice.x, sizeCornice.y, sizeCornice.z), translate(alongX ? Vec3{0.0, kCeiling - 0.03, fixed} : Vec3{fixed, kCeiling - 0.03, 0.0}));
	}
	out += attr(trim, trims);
	return out;
}

inline std::string buildWindow(const SetState &s) {
	std::string out;
	const double x0 = -2.4, x1 = -1.2, y0 = 0.92, y1 = 2.18, z = kRoomZ0 - 0.05;
	const std::string frame = coated({0.94, 0.94, 0.92}, 0.3);
	std::string f;
	f += emitMesh(box(x1 - x0 + 0.10, 0.06, 0.12), translate((x0 + x1) / 2, y0 - 0.03, z + 0.03));
	f += emitMesh(box(x1 - x0 + 0.24, 0.035, 0.22), translate((x0 + x1) / 2, y0 + 0.005, z + 0.10));   // the sill
	f += emitMesh(box(x1 - x0 + 0.10, 0.06, 0.12), translate((x0 + x1) / 2, y1 + 0.03, z));
	for (double x : {x0 - 0.03, x1 + 0.03}) f += emitMesh(box(0.06, y1 - y0 + 0.12, 0.12), translate(x, (y0 + y1) / 2, z));
	f += emitMesh(box(0.04, y1 - y0, 0.06), translate((x0 + x1) / 2, (y0 + y1) / 2, z));   // the mullions
	f += emitMesh(box(x1 - x0, 0.04, 0.06), translate((x0 + x1) / 2, y0 + 0.62 * (y1 - y0), z));
	out += attr(frame, f);
	out += attr("Material \"thindielectric\" \"float eta\" [ 1.5 ]", emitMesh(panel({(x0 + x1) / 2, (y0 + y1) / 2, z - 0.01}, {(x1 - x0) / 2, 0.0, 0.0}, {0.0, (y1 - y0) / 2, 0.0}), identity()));
	// curtains: soft folds hanging either side, a little open; a rail above
	std::string rail = bar({x0 - 0.45, y1 + 0.16, z + 0.16}, {x1 + 0.45, y1 + 0.16, z + 0.16}, 0.012, 10);
	out += attr(metalMaterial({0.60, 0.55, 0.45}, 0.3), rail);
	for (double side : {-1.0, 1.0}) {
		Mesh c;
		const double cx = side < 0 ? x0 - 0.18 : x1 + 0.18;
		const int cols = 18, rows = 10;
		for (int j = 0; j <= rows; ++j)
			for (int i = 0; i <= cols; ++i) {
				const double u = static_cast<double>(i) / cols, v = static_cast<double>(j) / rows;
				const double x = cx - 0.24 + 0.48 * u, y = y1 + 0.14 - (y1 - y0 + 0.55) * v;
				const double fold = 0.035 * std::sin(u * kPi * 7.0) * (0.7 + 0.3 * v) + 0.004 * std::sin(s.t * 0.8 + u * 9.0) * v;
				c.verts.push_back({x, y, z + 0.17 + fold});
				c.normals.push_back(normalize({-0.035 * 7.0 * kPi * std::cos(u * kPi * 7.0) / 0.48, 0.0, 1.0}));
				c.uvs.push_back({u, v});
			}
		for (int j = 0; j < rows; ++j)
			for (int i = 0; i < cols; ++i) {
				const int a = j * (cols + 1) + i;
				c.tris.push_back({a, a + cols + 1, a + 1});
				c.tris.push_back({a + 1, a + cols + 1, a + cols + 2});
			}
		out += attr(translucentMaterial({0.72, 0.62, 0.48}, {0.30, 0.24, 0.16}), emitMesh(c, identity()));
	}
	// pots of herbs on the sill
	for (int k = 0; k < 3; ++k) {
		const Vec3 base = {x0 + 0.25 + 0.35 * k, y0 + 0.02, z + 0.12};
		out += attr(coated({0.70, 0.36, 0.22}, 0.6), emitMesh(lathe({{0.0, 0.0}, {0.045, 0.0}, {0.06, 0.10}, {0.065, 0.11}, {0.0, 0.11}}, 20), translate(base)));
		Mesh leaves;
		for (int l = 0; l < 22; ++l) {
			const double a = 2.0 * kPi * hash01(l + 30 * k, 5), h = 0.10 + 0.10 * hash01(l + 30 * k, 6);
			nature::addLeaf(leaves, base + Vec3{0.02 * std::cos(a), h, 0.02 * std::sin(a)}, {std::cos(a), 0.6, std::sin(a)}, {0.0, 1.0, 0.0}, 0.04, 0.022);
		}
		out += attr(translucentMaterial({0.16, 0.36, 0.10}, {0.20, 0.40, 0.08}), emitMesh(leaves, identity()));
	}
	return out;
}

// The big window in the left wall (z -0.1 .. 1.5): four tall panes, a sill, and sheer curtains drawn back.
inline std::string buildSideWindow(const SetState &s) {
	std::string out;
	const double z0 = -0.1, z1 = 1.5, y0 = 0.55, y1 = 2.25, x = kRoomX0 - 0.05;
	const std::string frame = coated({0.94, 0.94, 0.92}, 0.3);
	std::string f;
	f += emitMesh(box(0.12, 0.06, z1 - z0 + 0.10), translate(x, y0 - 0.03, (z0 + z1) / 2));
	f += emitMesh(box(0.22, 0.035, z1 - z0 + 0.24), translate(x + 0.10, y0 + 0.005, (z0 + z1) / 2));
	f += emitMesh(box(0.12, 0.06, z1 - z0 + 0.10), translate(x, y1 + 0.03, (z0 + z1) / 2));
	for (double z : {z0 - 0.03, z1 + 0.03}) f += emitMesh(box(0.12, y1 - y0 + 0.12, 0.06), translate(x, (y0 + y1) / 2, z));
	for (int k = 1; k < 4; ++k) f += emitMesh(box(0.06, y1 - y0, 0.04), translate(x, (y0 + y1) / 2, z0 + (z1 - z0) * k / 4.0));
	out += attr(frame, f);
	out += attr("Material \"thindielectric\" \"float eta\" [ 1.5 ]", emitMesh(panel({x - 0.01, (y0 + y1) / 2, (z0 + z1) / 2}, {0.0, 0.0, (z1 - z0) / 2}, {0.0, (y1 - y0) / 2, 0.0}), identity()));
	for (double side : {-1.0, 1.0}) {
		Mesh c;
		const double cz = side < 0 ? z0 - 0.22 : z1 + 0.22;
		const int cols = 16, rows = 10;
		for (int j = 0; j <= rows; ++j)
			for (int i = 0; i <= cols; ++i) {
				const double u = static_cast<double>(i) / cols, v = static_cast<double>(j) / rows;
				const double z = cz - 0.25 + 0.5 * u, y = y1 + 0.2 - (y1 - y0 + 0.7) * v;
				const double fold = 0.03 * std::sin(u * kPi * 6.0) + 0.003 * std::sin(s.t * 0.7 + u * 7.0) * v;
				c.verts.push_back({x + 0.17 + fold, y, z});
				c.normals.push_back({1.0, 0.0, 0.0});
				c.uvs.push_back({u, v});
			}
		for (int j = 0; j < rows; ++j)
			for (int i = 0; i < cols; ++i) {
				const int a = j * (cols + 1) + i;
				c.tris.push_back({a, a + 1, a + cols + 1});
				c.tris.push_back({a + 1, a + cols + 2, a + cols + 1});
			}
		out += attr(translucentMaterial({0.80, 0.78, 0.72}, {0.55, 0.52, 0.45}), emitMesh(c, identity()));
	}
	return out;
}

inline std::string buildDoor(const SetState &s) {
	std::string out;
	const Vec3 h = doorHinge();
	const double w = 0.92, ht = 2.04;
	const Mat4 m = mul(translate(h.x - 0.03, 0.0, h.z), rotate({0, 1, 0}, -90.0 * s.door), translate(0.0, 0.0, w / 2));   // swings into the room about its hinge
	const std::string wood = coated({0.42, 0.24, 0.13}, 0.3);
	std::string d = emitMesh(box(0.045, ht, w), mul(m, translate(0.0, ht / 2, 0.0)));
	std::string panels;
	for (int k = 0; k < 4; ++k) {
		const double y = k < 2 ? 0.55 : 1.45, zc = (k % 2 == 0 ? -0.2 : 0.2);
		panels += emitMesh(roundedBox(0.012, k < 2 ? 0.70 : 0.85, 0.30, 0.004, 1), mul(m, translate(-0.026, y, zc)));
	}
	out += attr(wood, d) + attr(coated({0.36, 0.20, 0.11}, 0.3), panels);
	out += attr(metalMaterial({0.82, 0.70, 0.40}, 0.12), emitMesh(uvSphere(0.03, 16, 8), mul(m, translate(-0.07, 1.0, 0.36))) +
														  bar(transformPoint(m, {-0.025, 1.0, 0.36}), transformPoint(m, {-0.07, 1.0, 0.36}), 0.008, 8));
	// the frame round the opening, and three hinges
	const std::string trim = coated({0.92, 0.91, 0.88}, 0.4);
	std::string fr = emitMesh(box(0.06, 0.08, w + 0.18), translate(kRoomX1 - 0.02, ht + 0.04, h.z + w / 2));
	for (double z : {h.z - 0.045, h.z + w + 0.045}) fr += emitMesh(box(0.06, ht, 0.07), translate(kRoomX1 - 0.02, ht / 2, z));
	out += attr(trim, fr);
	std::string hinges;
	for (double y : {0.25, 1.0, 1.8}) hinges += emitMesh(box(0.012, 0.09, 0.03), translate(h.x - 0.06, y, h.z + 0.01));
	out += attr(metalMaterial({0.80, 0.68, 0.40}, 0.15), hinges);
	// the landing outside: bright when the door opens (the hall light)
	if (s.door > 0.01) {
		const Vec3 glow = Vec3{4.5, 4.2, 3.6};
		out += attr(diffuseMaterial({0.6, 0.6, 0.6}), emitMesh(box(0.1, 3.0, 3.0), translate(kRoomX1 + 1.8, 1.3, h.z + 0.46)));
		out += emissiveSphere({kRoomX1 + 1.0, 2.3, h.z + 0.46}, 0.10, glow * 6.0);
	}
	// a coat on hooks by the door, a bowl for keys on a small shelf
	out += attr(coated({0.85, 0.85, 0.82}, 0.3), emitMesh(box(0.04, 0.05, 0.6), translate(kRoomX1 - 0.03, 1.72, 0.25)));
	std::string hooks;
	for (double z : {0.05, 0.25, 0.45}) hooks += bar({kRoomX1 - 0.04, 1.70, z}, {kRoomX1 - 0.12, 1.75, z}, 0.009, 6);
	out += attr(metalMaterial({0.2, 0.2, 0.2}, 0.3), hooks);
	out += attr(diffuseMaterial({0.24, 0.34, 0.30}),
				emitMesh(lathe({{0.0, 0.0}, {0.05, 0.0}, {0.16, 0.25}, {0.20, 0.55}, {0.18, 0.80}, {0.08, 0.92}, {0.0, 0.93}}, 24), mul(translate(kRoomX1 - 0.17, 0.80, 0.05), scale(1.0, 1.0, 0.55))));
	out += attr(coated({0.30, 0.20, 0.12}, 0.4), emitMesh(box(0.20, 0.03, 0.42), translate(kRoomX1 - 0.12, 1.05, 2.05)));
	out += attr(coated({0.85, 0.82, 0.74}, 0.25), emitMesh(lathe({{0.0, 0.0}, {0.05, 0.0}, {0.09, 0.04}, {0.10, 0.05}, {0.0, 0.01}}, 20), translate(kRoomX1 - 0.12, 1.065, 2.05)));
	return out;
}

inline std::string buildFan(const SetState &s) {
	std::string out;
	const Vec3 c = fanCentre();
	const std::string metal = metalMaterial({0.30, 0.28, 0.25}, 0.25);
	out += attr(metal, bar({c.x, kCeiling, c.z}, {c.x, c.y + 0.10, c.z}, 0.015, 10));
	out += attr(metal, emitMesh(lathe({{0.0, 0.0}, {0.10, 0.0}, {0.12, 0.05}, {0.10, 0.12}, {0.0, 0.13}}, 28), translate(c.x, c.y - 0.03, c.z)));
	std::string blades;
	for (int k = 0; k < 5; ++k) {
		const double a = s.fanTurn + 72.0 * k;
		blades += emitMesh(roundedBox(0.52, 0.010, 0.13, 0.004, 2), mul(translate(c), rotate({0, 1, 0}, -a), translate(0.38, 0.01, 0.0), rotate({1, 0, 0}, 9.0)));
	}
	out += attr(coated({0.38, 0.26, 0.16}, 0.4), blades);
	// the globe light under it: a white glass ball when off, a light when on
	const Vec3 globe = c + Vec3{0.0, -0.12, 0.0};
	if (s.roomLamp > 0.01) out += emissiveSphere(globe, 0.10, Vec3{120.0, 104.0, 80.0} * s.roomLamp, "Material \"diffuse\" \"rgb reflectance\" [ 0.9 0.9 0.88 ]");
	else out += attr(coated({0.88, 0.88, 0.85}, 0.2), emitMesh(uvSphere(0.10, 24, 12), translate(globe)));
	return out;
}

inline std::string buildSofaAndTable() {
	std::string out;
	const std::string fabric = diffuseMaterial({0.30, 0.38, 0.50}), cushion = diffuseMaterial({0.34, 0.42, 0.55}), wood = coated({0.30, 0.19, 0.10}, 0.35);
	const double cx = (kSofaX0 + kSofaX1) / 2, cz = (kSofaZ0 + kSofaZ1) / 2, w = kSofaX1 - kSofaX0, d = kSofaZ1 - kSofaZ0;
	// the sofa faces the back wall (-z): its back is along +z
	out += roundedAt({cx, kSofaLegs + 0.14, cz}, {w, 0.20, d}, 0.05, fabric);                                // the base
	out += roundedAt({cx, kSofaLegs + 0.48, kSofaZ1 - 0.11}, {w - 0.24, 0.52, 0.20}, 0.07, fabric);         // the back
	for (double side : {-1.0, 1.0}) out += roundedAt({cx + side * (w / 2 - 0.09), kSofaLegs + 0.33, cz}, {0.18, 0.44, d}, 0.07, fabric);   // the arms
	for (int k = 0; k < 3; ++k) {
		const double x = kSofaX0 + 0.18 + (w - 0.36) * (k + 0.5) / 3.0;
		out += roundedAt({x, kSofaLegs + 0.30, cz - 0.08}, {(w - 0.36) / 3.0 - 0.02, 0.14, d - 0.24}, 0.06, cushion);   // seat cushions
		out += roundedAt({x, kSofaLegs + 0.55, kSofaZ1 - 0.24}, {(w - 0.36) / 3.0 - 0.03, 0.40, 0.14}, 0.06, cushion);   // back cushions
	}
	out += roundedAt({kSofaX0 + 0.38, kSofaLegs + 0.52, kSofaZ1 - 0.36}, {0.34, 0.32, 0.10}, 0.05, diffuseMaterial({0.85, 0.66, 0.28}), 18.0);   // throw pillows
	out += roundedAt({kSofaX1 - 0.40, kSofaLegs + 0.52, kSofaZ1 - 0.36}, {0.32, 0.30, 0.10}, 0.05, diffuseMaterial({0.72, 0.30, 0.25}), -14.0);
	std::string legs;
	for (double x : {kSofaX0 + 0.08, kSofaX1 - 0.08})
		for (double z : {kSofaZ0 + 0.08, kSofaZ1 - 0.08}) legs += emitMesh(lathe({{0.0, 0.0}, {0.020, 0.0}, {0.028, kSofaLegs}, {0.0, kSofaLegs}}, 12), translate(x, 0.0, z));
	out += attr(wood, legs);
	// the coffee table, between the sofa and the tank, with a mug, magazines and the remote
	const double tx = -1.0, tz = -1.15;
	out += roundedAt({tx, 0.42, tz}, {1.10, 0.04, 0.55}, 0.01, wood);
	out += roundedAt({tx, 0.16, tz}, {1.00, 0.02, 0.45}, 0.008, wood);   // the lower shelf
	std::string tl;
	for (double x : {tx - 0.50, tx + 0.50})
		for (double z : {tz - 0.22, tz + 0.22}) tl += emitMesh(box(0.045, 0.40, 0.045), translate(x, 0.20, z));
	out += attr(wood, tl);
	out += attr(coated({0.92, 0.90, 0.85}, 0.15), emitMesh(lathe({{0.0, 0.0}, {0.04, 0.0}, {0.045, 0.10}, {0.040, 0.10}, {0.035, 0.012}, {0.0, 0.012}}, 24), translate(tx + 0.30, 0.44, tz + 0.05)));
	out += attr(coated({0.92, 0.90, 0.85}, 0.15), emitMesh(torus(0.025, 0.007, 16, 8), mul(translate(tx + 0.35, 0.49, tz + 0.05), rotate({1, 0, 0}, 90.0))));   // its handle
	const Vec3 mags[3] = {{0.80, 0.25, 0.20}, {0.20, 0.45, 0.70}, {0.95, 0.85, 0.40}};
	for (int k = 0; k < 3; ++k) out += boxAt({tx - 0.25 + 0.01 * k, 0.445 + 0.007 * k, tz - 0.05}, {0.28, 0.006, 0.21}, coated(mags[k], 0.4), 6.0 * k - 6.0);
	out += roundedAt({tx + 0.05, 0.452, tz - 0.15}, {0.16, 0.018, 0.045}, 0.008, coated({0.10, 0.10, 0.11}, 0.3), 25.0);   // the remote
	for (int k = 0; k < 4; ++k) out += boxAt({tx - 0.2 + 0.25 * k, 0.21, tz}, {0.18, 0.06 + 0.02 * (k % 2), 0.24}, coated(mags[k % 3] * 0.8, 0.5), 3.0 * k);   // books on the shelf
	return out;
}

inline std::string buildRug(bool fibres, Vec3 fibreCentre, double fibreRadius) {
	std::string out;
	const double cx = (kRugX0 + kRugX1) / 2, cz = (kRugZ0 + kRugZ1) / 2;
	out += roundedAt({cx, kRugThick / 2, cz}, {kRugX1 - kRugX0, kRugThick, kRugZ1 - kRugZ0}, 0.006, diffuseMaterial({0.62, 0.30, 0.22}));
	// a pattern woven in: a cream border and a diamond lattice (thin raised strips, part of the pile)
	const std::string cream = diffuseMaterial({0.86, 0.78, 0.62}), navy = diffuseMaterial({0.16, 0.20, 0.34});
	std::string border;
	border += emitMesh(box(kRugX1 - kRugX0 - 0.10, 0.002, 0.05), translate(cx, kRugThick + 0.0005, kRugZ0 + 0.08));
	border += emitMesh(box(kRugX1 - kRugX0 - 0.10, 0.002, 0.05), translate(cx, kRugThick + 0.0005, kRugZ1 - 0.08));
	border += emitMesh(box(0.05, 0.002, kRugZ1 - kRugZ0 - 0.10), translate(kRugX0 + 0.08, kRugThick + 0.0005, cz));
	border += emitMesh(box(0.05, 0.002, kRugZ1 - kRugZ0 - 0.10), translate(kRugX1 - 0.08, kRugThick + 0.0005, cz));
	out += attr(cream, border);
	std::string lattice;
	for (int k = -6; k <= 6; ++k)
		for (double sgn : {-1.0, 1.0}) {
			const Vec3 a = {cx + 0.25 * k - 0.55, kRugThick + 0.0006, kRugZ0 + 0.15}, b = {cx + 0.25 * k + 0.55, kRugThick + 0.0006, kRugZ1 - 0.15};
			Vec3 p0 = a, p1 = b;
			if (sgn < 0) std::swap(p0.x, p1.x);
			if (std::max(p0.x, p1.x) > kRugX1 - 0.15 || std::min(p0.x, p1.x) < kRugX0 + 0.15) continue;
			lattice += bar(p0, p1, 0.006, 4);
		}
	out += attr(navy, lattice);
	// tassels at the short ends
	std::string tassels;
	for (double z = kRugZ0 + 0.03; z < kRugZ1 - 0.02; z += 0.035)
		for (double x : {kRugX0, kRugX1}) tassels += bar({x, 0.004, z}, {x + (x < cx ? -0.07 : 0.07), 0.003, z + 0.005 * std::sin(z * 40.0)}, 0.0025, 4);
	out += attr(cream, tassels);
	// the pile as the turtle sees it: tufts of yarn round where it crosses, written only for the shots that are down among them
	if (fibres) {
		std::string tufts;
		int k = 0;
		for (double x = fibreCentre.x - fibreRadius; x < fibreCentre.x + fibreRadius; x += 0.012)
			for (double z = fibreCentre.z - fibreRadius; z < fibreCentre.z + fibreRadius; z += 0.012, ++k) {
				const double jx = x + 0.008 * (hash01(k, 41) - 0.5), jz = z + 0.008 * (hash01(k, 42) - 0.5);
				if (jx < kRugX0 + 0.01 || jx > kRugX1 - 0.01 || jz < kRugZ0 + 0.01 || jz > kRugZ1 - 0.01) continue;
				if ((jx - fibreCentre.x) * (jx - fibreCentre.x) + (jz - fibreCentre.z) * (jz - fibreCentre.z) > fibreRadius * fibreRadius) continue;
				tufts += nature::placeObject("yarn" + std::to_string(k % 3), mul(translate(jx, kRugThick - 0.002, jz), rotate({0, 1, 0}, 360.0 * hash01(k, 43)), scale(1.0 + 0.4 * hash01(k, 44))));
			}
		out += tufts;
	}
	return out;
}

inline std::string buildShelfLampPictures() {
	std::string out;
	// the bookshelf against the left wall, five shelves of books
	const double sx = kRoomX0 + 0.18, sz0 = -1.9, sz1 = -0.9;
	const std::string wood = coated({0.36, 0.23, 0.13}, 0.35);
	std::string frame;
	for (double z : {sz0, sz1}) frame += emitMesh(box(0.34, 1.90, 0.03), translate(sx, 0.95, z));
	for (int k = 0; k <= 5; ++k) frame += emitMesh(box(0.34, 0.025, sz1 - sz0), translate(sx, 0.02 + 0.37 * k, (sz0 + sz1) / 2));
	frame += emitMesh(box(0.02, 1.90, sz1 - sz0), translate(kRoomX0 + 0.02, 0.95, (sz0 + sz1) / 2));
	out += attr(wood, frame);
	std::string books;   // (each book a placed unit block in one of seven colours, objectDefinitions)
	for (int shelf = 0; shelf < 5; ++shelf) {
		double z = sz0 + 0.04;
		for (int b = 0; z < sz1 - 0.06; ++b) {
			const int i = shelf * 40 + b;
			const double th = 0.02 + 0.03 * hash01(i, 51), h = 0.20 + 0.10 * hash01(i, 52), depth = 0.20 + 0.06 * hash01(i, 53);
			const double lean = (hash01(i, 54) < 0.08) ? 12.0 : 0.0;
			if (hash01(i, 55) < 0.06) {   // a gap, or a small plant on the next shelf
				z += 0.08;
				continue;
			}
			books += nature::placeObject("book" + std::to_string(i % 7), mul(translate(sx + 0.02, 0.035 + 0.37 * shelf + h / 2, z + th / 2), rotate({1, 0, 0}, lean), scale(depth, h, th)));
			z += th + 0.002;
		}
	}
	out += books;
	// the floor lamp beside the sofa: a brass stand and a linen shade (on in the evening, lit from an open bulb under the shade)
	const Vec3 lb = {kSofaX0 - 0.30, 0.0, kSofaZ1 - 0.05};
	out += attr(metalMaterial({0.78, 0.62, 0.34}, 0.2), emitMesh(lathe({{0.0, 0.0}, {0.16, 0.0}, {0.16, 0.02}, {0.03, 0.04}, {0.0, 0.04}}, 24), translate(lb)) + bar(lb, lb + Vec3{0.0, 1.45, 0.0}, 0.012, 10));
	out += attr(translucentMaterial({0.80, 0.74, 0.60}, {0.45, 0.38, 0.24}), emitMesh(lathe({{0.20, 0.0}, {0.14, 0.30}}, 32), translate(lb + Vec3{0.0, 1.32, 0.0})));
	// pictures on the walls: frames with a mountain, a sea, and a turtle's own portrait over the sofa
	const auto picture = [&](Vec3 c, double w, double h, double yaw, Vec3 sky, Vec3 land) {
		std::string p = attr(coated({0.15, 0.12, 0.10}, 0.3), emitMesh(box(w + 0.06, h + 0.06, 0.03), mul(translate(c), rotate({0, 1, 0}, -yaw))));
		p += attr(diffuseMaterial(sky), emitMesh(box(w, h * 0.55, 0.004), mul(translate(c), rotate({0, 1, 0}, -yaw), translate(0.0, h * 0.225, 0.017))));
		p += attr(diffuseMaterial(land), emitMesh(box(w, h * 0.45, 0.004), mul(translate(c), rotate({0, 1, 0}, -yaw), translate(0.0, -h * 0.275, 0.017))));
		p += attr(diffuseMaterial(land * 0.6), emitMesh(lathe({{w * 0.3, 0.0}, {0.0, h * 0.35}}, 3), mul(translate(c), rotate({0, 1, 0}, -yaw), translate(-w * 0.1, -h * 0.05, 0.02), rotate({1, 0, 0}, 90.0), scale(1.0, 1.0, 0.02))));
		return p;
	};
	out += picture({-2.0, 1.55, kRoomZ1 - 0.02}, 0.60, 0.42, 180.0, {0.55, 0.70, 0.85}, {0.30, 0.50, 0.30});
	out += picture({kRoomX0 + 0.02, 1.70, 0.40}, 0.50, 0.38, 90.0, {0.85, 0.65, 0.45}, {0.20, 0.35, 0.55});
	out += picture({1.9, 1.62, kRoomZ0 + 0.02}, 0.44, 0.56, 0.0, {0.75, 0.85, 0.80}, {0.25, 0.42, 0.18});
	// a big potted plant in the corner by the window
	const Vec3 pot = {-2.75, 0.0, -2.2};
	out += attr(coated({0.88, 0.86, 0.80}, 0.3), emitMesh(lathe({{0.0, 0.0}, {0.14, 0.0}, {0.18, 0.36}, {0.19, 0.38}, {0.0, 0.36}}, 28), translate(pot)));
	Mesh big;
	for (int l = 0; l < 26; ++l) {
		const double a = 2.0 * kPi * hash01(l, 61), h = 0.55 + 0.75 * hash01(l, 62), reach = 0.15 + 0.25 * hash01(l, 63);
		const Vec3 tip = pot + Vec3{reach * std::cos(a), h, reach * std::sin(a)};
		nature::addStrip(big, [&](double u) { return pot + Vec3{0.0, 0.36, 0.0} + (tip - pot - Vec3{0.0, 0.36, 0.0}) * u + Vec3{0.0, 0.08 * std::sin(kPi * u), 0.0}; },
						 [](double) { return 0.006; }, normalize(Vec3{-std::sin(a), 0.0, std::cos(a)}), 4);
		nature::addLeaf(big, tip, {std::cos(a), 0.15, std::sin(a)}, {0.0, 1.0, 0.0}, 0.22 + 0.08 * hash01(l, 64), 0.16);
	}
	out += attr(translucentMaterial({0.08, 0.26, 0.10}, {0.10, 0.30, 0.08}), emitMesh(big, identity()));
	return out;
}

inline std::string buildClock(const SetState &s) {
	std::string out;
	const Vec3 c = clockFace();
	const Mat4 m = mul(translate(c), rotate({1, 0, 0}, 90.0));   // the face looks along +z
	out += attr(coated({0.12, 0.10, 0.09}, 0.25), emitMesh(torus(0.16, 0.018, 48, 10), m));
	out += attr(coated({0.95, 0.93, 0.88}, 0.4), emitMesh(lathe({{0.0, 0.0}, {0.16, 0.0}, {0.16, 0.012}, {0.0, 0.012}}, 48), mul(m, translate(0.0, -0.01, 0.0))));
	std::string ticks;
	for (int k = 0; k < 12; ++k) {
		const double a = 2.0 * kPi * k / 12.0, r0 = k % 3 == 0 ? 0.115 : 0.128;
		ticks += bar(c + Vec3{r0 * std::sin(a), r0 * std::cos(a), 0.006}, c + Vec3{0.142 * std::sin(a), 0.142 * std::cos(a), 0.006}, k % 3 == 0 ? 0.004 : 0.0025, 4);
	}
	out += attr(diffuseMaterial({0.08, 0.08, 0.08}), ticks);
	const double minutes = 480.0 + s.clockMinutes;   // 8:00 in the morning onwards
	const double am = 2.0 * kPi * std::fmod(minutes, 60.0) / 60.0, ah = 2.0 * kPi * std::fmod(minutes / 60.0, 12.0) / 12.0;
	out += attr(diffuseMaterial({0.06, 0.06, 0.06}), bar(c + Vec3{0.0, 0.0, 0.012}, c + Vec3{0.125 * std::sin(am), 0.125 * std::cos(am), 0.012}, 0.0045, 6) +
													 bar(c + Vec3{0.0, 0.0, 0.016}, c + Vec3{0.08 * std::sin(ah), 0.08 * std::cos(ah), 0.016}, 0.0065, 6));
	out += attr(coated({0.75, 0.12, 0.08}, 0.3), emitMesh(uvSphere(0.008, 12, 6), translate(c + Vec3{0.0, 0.0, 0.02})));
	return out;
}

inline std::string buildVacuum(const SetState &s) {
	std::string out;
	// the dock against the right wall
	const Vec3 d = vacuumDock();
	out += roundedAt({kRoomX1 - 0.06, 0.08, d.z}, {0.10, 0.16, 0.24}, 0.03, coated({0.92, 0.92, 0.92}, 0.25));
	const Mat4 m = mul(translate(s.vacuumPos), rotate({0, 1, 0}, -s.vacuumYaw));
	out += attr(coated({0.14, 0.14, 0.15}, 0.18), emitMesh(lathe({{0.0, 0.012}, {0.165, 0.012}, {0.172, 0.03}, {0.170, 0.075}, {0.150, 0.088}, {0.0, 0.090}}, 48), m));
	out += attr(coated({0.55, 0.55, 0.58}, 0.25), emitMesh(torus(0.172, 0.012, 48, 8), mul(m, translate(0.0, 0.045, 0.0))));   // the bumper
	out += attr(coated({0.30, 0.30, 0.32}, 0.2), emitMesh(lathe({{0.0, 0.0}, {0.04, 0.0}, {0.035, 0.022}, {0.0, 0.024}}, 24), mul(m, translate(0.06, 0.088, 0.0))));   // the sensor turret
	std::string brushes;
	for (int k = 0; k < 3; ++k) {
		const double a = s.t * 30.0 * s.vacuumOn + 120.0 * k;
		brushes += bar(transformPoint(m, {0.11, 0.008, 0.10}), transformPoint(m, Vec3{0.11, 0.004, 0.10} + Vec3{0.07 * std::cos(a * kPi / 180.0), 0.0, 0.07 * std::sin(a * kPi / 180.0)}), 0.0025, 4);
	}
	out += attr(diffuseMaterial({0.12, 0.12, 0.12}), brushes);
	if (s.vacuumOn > 0.01) {
		// three blue lights on the top: its "eyes" in the dark (glowing balls are one light each)
		for (int k = -1; k <= 1; ++k) out += emissiveSphere(transformPoint(m, {0.125, 0.082, 0.04 * k}), 0.007, Vec3{1.5, 6.0, 14.0} * s.vacuumOn);
	}
	return out;
}

inline std::string buildTowelsTubBox(const SetState &s) {
	std::string out;
	// the towels: three folded, one tumbled on top, where the turtle lands
	const Vec3 t = towels();
	const Vec3 towelColours[4] = {{0.92, 0.88, 0.80}, {0.55, 0.72, 0.80}, {0.92, 0.88, 0.80}, {0.85, 0.55, 0.45}};
	for (int k = 0; k < 3; ++k) out += roundedAt(t + Vec3{0.01 * k, 0.035 + 0.06 * k, 0.005 * k}, {0.42, 0.06, 0.30}, 0.025, diffuseMaterial(towelColours[k]), 4.0 * k - 3.0);
	{
		Mesh tumble = uvSphere(1.0, 20, 10);
		for (Vec3 &v : tumble.verts) v = Vec3{v.x * 0.22, v.y * 0.05 + 0.02 * std::sin(v.x * 9.0) * std::cos(v.z * 7.0), v.z * 0.17};
		out += attr(diffuseMaterial(towelColours[3]), emitMesh(tumble, translate(t + Vec3{0.02, 0.22, 0.0})));
	}
	// the storage tub against the front wall: pale blue plastic with a lip, water, lettuce and a basking rock in it (opaque: under the water it reads as a pool)
	const double cx = (kTubX0 + kTubX1) / 2, cz = (kTubZ0 + kTubZ1) / 2, w = kTubX1 - kTubX0, d = kTubZ1 - kTubZ0;
	const std::string plastic = coated({0.38, 0.62, 0.82}, 0.25);
	std::string walls;
	walls += emitMesh(box(w, 0.006, d), translate(cx, 0.003, cz));
	for (double side : {-1.0, 1.0}) {
		walls += emitMesh(box(w, kTubH, 0.006), translate(cx, kTubH / 2, cz + side * d / 2));
		walls += emitMesh(box(0.006, kTubH, d), translate(cx + side * w / 2, kTubH / 2, cz));
	}
	out += attr(plastic, walls);
	std::string lip;   // the rim round its top
	for (double side : {-1.0, 1.0}) {
		lip += emitMesh(roundedBox(w + 0.03, 0.012, 0.022, 0.005, 1), translate(cx, kTubH, cz + side * (d / 2 + 0.004)));
		lip += emitMesh(roundedBox(0.022, 0.012, d + 0.03, 0.005, 1), translate(cx + side * (w / 2 + 0.004), kTubH, cz));
	}
	out += attr(coated({0.55, 0.70, 0.85}, 0.3), lip);
	(void)s;
	// lettuce leaves floating, and a flat rock to bask on in one corner
	Mesh lettuce;
	for (int k = 0; k < 7; ++k) {
		const double a = 2.0 * kPi * hash01(k, 71);
		Mesh leaf;
		nature::addLeaf(leaf, {cx + 0.15 * (hash01(k, 72) - 0.5), kTubWater + 0.003, cz + 0.12 * (hash01(k, 73) - 0.5)}, {std::cos(a), 0.05, std::sin(a)}, {0.0, 1.0, 0.0}, 0.09, 0.08);
		for (std::size_t i = 0; i < leaf.verts.size(); ++i) leaf.verts[i].y += 0.004 * std::sin(leaf.verts[i].x * 80.0);   // a ruffled edge
		const int base = static_cast<int>(lettuce.verts.size());
		lettuce.verts.insert(lettuce.verts.end(), leaf.verts.begin(), leaf.verts.end());
		lettuce.normals.insert(lettuce.normals.end(), leaf.normals.begin(), leaf.normals.end());
		lettuce.uvs.insert(lettuce.uvs.end(), leaf.uvs.begin(), leaf.uvs.end());
		for (auto tr : leaf.tris) lettuce.tris.push_back({tr[0] + base, tr[1] + base, tr[2] + base});
	}
	out += attr(translucentMaterial({0.45, 0.70, 0.25}, {0.40, 0.65, 0.18}), emitMesh(lettuce, identity()));
	out += attr(coated({0.42, 0.40, 0.36}, 0.6), emitMesh(nature::rock(0.12, 9), translate(kTubX0 + 0.10, kTubWater - 0.025, kTubZ1 - 0.10)));
	// the cardboard box beside it, its flaps open
	const Vec3 b = cardboardBox();
	const std::string card = diffuseMaterial({0.66, 0.48, 0.30});
	out += roundedAt(b + Vec3{0.0, kBoxH / 2, 0.0}, {0.22, kBoxH, 0.16}, 0.004, card);
	out += attr(card, emitMesh(box(0.22, 0.003, 0.08), mul(translate(b + Vec3{0.0, kBoxH, -0.08}), rotate({1, 0, 0}, -60.0), translate(0.0, 0.0, -0.04))) +
						  emitMesh(box(0.22, 0.003, 0.08), mul(translate(b + Vec3{0.0, kBoxH, 0.08}), rotate({1, 0, 0}, 70.0), translate(0.0, 0.0, 0.04))));
	out += attr(diffuseMaterial({0.30, 0.22, 0.14}), emitMesh(box(0.10, 0.0015, 0.05), translate(b + Vec3{0.04, kBoxH + 0.002, 0.0})));   // a label
	return out;
}

// A water surface over a rectangle, rippled by `ripples` and gently moving. `y0` its rest height.
inline Mesh waterSurface(double x0, double x1, double z0, double z1, double y0, double t, const std::vector<Ripple> &ripples, double cell = 0.015) {
	Mesh m;
	const int nx = std::max(2, static_cast<int>((x1 - x0) / cell)), nz = std::max(2, static_cast<int>((z1 - z0) / cell));
	const auto height = [&](double x, double z) {
		double h = 0.0006 * std::sin(9.0 * x + 1.3 * t) * std::cos(11.0 * z - 0.9 * t) + 0.0004 * std::sin(17.0 * (x + z) + 2.1 * t);
		for (const Ripple &r : ripples) {
			const double a = t - r.t0;
			if (a < 0.0 || a > 4.0) continue;
			const double d = std::hypot(x - r.x, z - r.z), front = 0.25 * a;
			h += r.amp * 0.004 * std::exp(-2.0 * a) * std::exp(-std::pow((d - front) / 0.03, 2.0)) * std::cos(60.0 * (d - front));
		}
		return h;
	};
	for (int j = 0; j <= nz; ++j)
		for (int i = 0; i <= nx; ++i) {
			const double x = x0 + (x1 - x0) * i / nx, z = z0 + (z1 - z0) * j / nz, e = 0.002;
			m.verts.push_back({x, y0 + height(x, z), z});
			m.normals.push_back(normalize({height(x - e, z) - height(x + e, z), 2.0 * e, height(x, z - e) - height(x, z + e)}));
			m.uvs.push_back({static_cast<double>(i) / nx, static_cast<double>(j) / nz});
		}
	for (int j = 0; j < nz; ++j)
		for (int i = 0; i < nx; ++i) {
			const int a = j * (nx + 1) + i;
			m.tris.push_back({a, a + nx + 1, a + 1});
			m.tris.push_back({a + 1, a + nx + 1, a + nx + 2});
		}
	return m;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The tank
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The river rocks the turtle pushes: where each one starts (on the gravel), and its size.
struct TankRock {
	Vec3 home;
	double size;
	int salt;
};
inline const std::vector<TankRock> &tankRocks() {
	static const std::vector<TankRock> r = {
		{{0.62, kGravel, -2.14}, 0.070, 11},   // 0: the stubborn one, pushed and bonked in scene 2
		{{0.70, kGravel, -2.30}, 0.060, 12}, {{0.55, kGravel, -2.33}, 0.055, 13}, {{0.80, kGravel, -2.18}, 0.050, 14}, {{0.48, kGravel, -2.24}, 0.050, 15},   // 1-4: become the pile
		{{0.88, kGravel, -2.32}, 0.065, 16}, {{0.36, kGravel, -2.33}, 0.045, 17}, {{0.75, kGravel, -2.08}, 0.040, 18},
	};
	return r;
}
// The hose's inside part: from the canister up the back, over the rim near the basking slate, and down into the water. `out` slides the inside end out
// over the rim (scene 4's slip and how the owner finds it); `sag` pulls it down at `grip`.
// The hose comes up the right side of the cabinet from the canister and over the tank's right rim, beside the basking slate.
inline Vec3 hoseIn1() { return {1.115, kTankRim + 0.022, -2.33}; }   // just inside the rim
inline Vec3 hoseIn2() { return {1.085, 1.10, -2.34}; }               // where the turtle grabs it from the slate
inline Vec3 hoseIn3() { return {1.065, 0.92, -2.34}; }               // its end, in the water
inline Vec3 hoseOut1() { return {1.24, kTankRim + 0.005, -2.20}; }   // slipped out: over the rim...
inline Vec3 hoseOut2() { return {1.40, 0.88, -2.00}; }
inline Vec3 hoseOut3() { return towels() + Vec3{-0.04, 0.30, -0.03}; }   // ...and down to the towels
inline std::vector<Vec3> hosePath(const SetState &s) {
	const Vec3 c = canister();
	std::vector<Vec3> pts = {c + Vec3{0.0, 0.35, 0.0}, {1.40, 0.80, -2.30}, {1.27, 1.12, -2.31}, {1.18, kTankRim + 0.035, -2.315}};
	// over the rim and down inside, or (slipped out) hanging down the outside to the towels
	const double o = clamp01(s.hoseOut);
	const Vec3 in1 = hoseIn1(), in2 = hoseIn2(), in3 = hoseIn3();
	const Vec3 out1 = hoseOut1(), out2 = hoseOut2(), out3 = hoseOut3();
	pts.push_back(lerp(in1, out1, o));
	pts.push_back(lerp(in2, out2, o));
	pts.push_back(lerp(in3, out3, o));
	if (s.hoseSag > 0.0) {
		for (std::size_t i = 4; i < pts.size(); ++i) {
			const double d = length(pts[i] - s.hoseGrip);
			pts[i].y -= s.hoseSag * std::exp(-d * d / 0.01);
		}
	}
	return pts;
}

inline std::string buildTank(const SetState &s) {
	std::string out;
	const double cx = (kTankX0 + kTankX1) / 2, cz = (kTankZ0 + kTankZ1) / 2, w = kTankX1 - kTankX0, d = kTankZ1 - kTankZ0, h = kTankRim - kTankBottom;
	// the cabinet: oak, two doors with brass handles, a plinth
	const std::string oak = coated({0.48, 0.31, 0.17}, 0.3);
	out += roundedAt({cx, 0.38, cz}, {w + 0.10, 0.70, d + 0.08}, 0.01, oak);
	out += roundedAt({cx, 0.745, cz}, {w + 0.14, 0.03, d + 0.12}, 0.008, oak);
	out += boxAt({cx, 0.025, cz + 0.01}, {w + 0.06, 0.05, d + 0.04}, coated({0.28, 0.18, 0.10}, 0.4));
	for (double side : {-1.0, 1.0}) {
		out += roundedAt({cx + side * w * 0.25, 0.39, kTankZ1 + 0.045}, {w * 0.47, 0.58, 0.012}, 0.004, coated({0.44, 0.28, 0.15}, 0.3));
		out += attr(metalMaterial({0.82, 0.68, 0.38}, 0.15), bar({cx + side * 0.04, 0.48, kTankZ1 + 0.06}, {cx + side * 0.04, 0.58, kTankZ1 + 0.06}, 0.007, 8));
	}
	// the glass: thin panes (they let the light through to everything inside), black trim top and bottom
	const std::string glass = "Material \"thindielectric\" \"float eta\" [ 1.5 ]";
	std::string panes;
	panes += emitMesh(panel({cx, kTankBottom + h / 2, kTankZ1}, {w / 2, 0.0, 0.0}, {0.0, h / 2, 0.0}), identity());
	panes += emitMesh(panel({cx, kTankBottom + h / 2, kTankZ0}, {-w / 2, 0.0, 0.0}, {0.0, h / 2, 0.0}), identity());
	for (double side : {-1.0, 1.0}) panes += emitMesh(panel({side < 0 ? kTankX0 : kTankX1, kTankBottom + h / 2, cz}, {0.0, 0.0, side * d / 2}, {0.0, h / 2, 0.0}), identity());
	out += attr(glass, panes);
	std::string trim;
	for (double y : {kTankBottom + 0.012, kTankRim - 0.008}) {
		const double th = y < 1.0 ? 0.024 : 0.016;
		trim += emitMesh(box(w + 0.02, th, 0.012), translate(cx, y, kTankZ1 + 0.002));
		trim += emitMesh(box(w + 0.02, th, 0.012), translate(cx, y, kTankZ0 - 0.002));
		for (double side : {-1.0, 1.0}) trim += emitMesh(box(0.012, th, d + 0.02), translate(side < 0 ? kTankX0 - 0.002 : kTankX1 + 0.002, y, cz));
	}
	out += attr(coated({0.04, 0.04, 0.045}, 0.2), trim);
	// a backdrop on the back glass: a painted river bank, blue-green fading up
	out += attr(diffuseMaterial({0.12, 0.30, 0.30}), emitMesh(panel({cx, kTankBottom + h * 0.30, kTankZ0 - 0.004}, {-w / 2, 0.0, 0.0}, {0.0, h * 0.30, 0.0}), identity()));
	out += attr(diffuseMaterial({0.30, 0.52, 0.55}), emitMesh(panel({cx, kTankBottom + h * 0.80, kTankZ0 - 0.004}, {-w / 2, 0.0, 0.0}, {0.0, h * 0.20, 0.0}), identity()));
	// gravel: a bed of small pebbles (placed objects), over a sand-coloured base
	out += boxAt({cx, kTankBottom + 0.012, cz}, {w - 0.004, 0.024, d - 0.004}, diffuseMaterial({0.58, 0.50, 0.38}));
	{
		std::string pebbles;
		int k = 0;
		for (double x = kTankX0 + 0.01; x < kTankX1 - 0.01; x += 0.022)
			for (double z = kTankZ0 + 0.01; z < kTankZ1 - 0.01; z += 0.022, ++k)
				pebbles += nature::placeObject("pebble" + std::to_string(k % 4), mul(translate(x + 0.01 * (hash01(k, 81) - 0.5), kGravel - 0.006, z + 0.01 * (hash01(k, 82) - 0.5)), rotate({0, 1, 0}, 360.0 * hash01(k, 83)),
																						 scale(0.7 + 0.6 * hash01(k, 84))));
		out += pebbles;
	}
	// the river rocks (some of them moved by the turtle)
	{
		std::string rocks;
		for (std::size_t i = 0; i < tankRocks().size(); ++i) {
			const TankRock &r = tankRocks()[i];
			const Vec3 off = i < s.rockOffset.size() ? s.rockOffset[i] : Vec3{0.0, 0.0, 0.0};
			const double tilt = i < s.rockTilt.size() ? s.rockTilt[i] : 0.0;
			rocks += emitMesh(nature::rock(r.size, r.salt), mul(translate(r.home + off), rotate({0, 0, 1}, tilt), rotate({0, 1, 0}, 40.0 * r.salt)));
		}
		out += attr(coated({0.46, 0.43, 0.38}, 0.45), rocks);
	}
	// plants: ribbons of eelgrass swaying in the back corners, and a round-leaved plant at the front left
	{
		Mesh grass;
		for (int k = 0; k < 26; ++k) {
			const double x = (k < 13 ? kTankX0 + 0.04 : kTankX1 - 0.18) + 0.12 * hash01(k, 91), z = kTankZ0 + 0.03 + 0.08 * hash01(k, 92);
			const double hgt = 0.14 + 0.08 * hash01(k, 93), ph = 6.0 * hash01(k, 94);
			const Vec3 root = {x, kGravel, z};
			nature::addStrip(grass, [&](double u) { return root + Vec3{0.03 * std::sin(1.1 * s.t + ph + 3.0 * u) * u * u, hgt * u, 0.02 * std::cos(0.9 * s.t + ph) * u * u}; },
							 [](double u) { return 0.008 * (1.0 - 0.6 * u); }, normalize(Vec3{std::cos(ph), 0.0, std::sin(ph)}), 6);
		}
		out += attr(translucentMaterial({0.15, 0.38, 0.12}, {0.20, 0.45, 0.10}), emitMesh(grass, identity()));
		Mesh round;
		const Vec3 r0 = {kTankX0 + 0.10, kGravel, kTankZ1 - 0.10};
		for (int k = 0; k < 12; ++k) {
			const double a = 2.0 * kPi * hash01(k, 95), hgt = 0.04 + 0.08 * hash01(k, 96);
			const Vec3 tip = r0 + Vec3{0.04 * std::cos(a), hgt, 0.04 * std::sin(a)};
			nature::addStrip(round, [&](double u) { return lerp(r0, tip, u); }, [](double) { return 0.003; }, {1.0, 0.0, 0.0}, 2);
			nature::addLeaf(round, tip, {std::cos(a), 0.4, std::sin(a)}, {0.0, 1.0, 0.0}, 0.035, 0.030);
		}
		out += attr(translucentMaterial({0.25, 0.50, 0.15}, {0.25, 0.50, 0.10}), emitMesh(round, identity()));
	}
	// the waterfall: a resin cliff at the back left with a thin falling sheet of water and bubbles where it lands
	{
		const Vec3 cl = {kTankX0 + 0.13, kGravel, kTankZ0 + 0.06};
		out += attr(coated({0.40, 0.33, 0.26}, 0.6), emitMesh(nature::rock(0.20, 77), mul(translate(cl), scale(1.0, 1.9, 0.8))));
		Mesh sheet;
		nature::addStrip(sheet, [&](double u) { return cl + Vec3{0.06, 0.20 - 0.21 * u, 0.06 + 0.015 * u * u}; }, [](double u) { return 0.04 + 0.01 * u; }, {1.0, 0.0, 0.0}, 6);
		out += attr("Material \"thindielectric\" \"float eta\" [ 1.33 ]", emitMesh(sheet, identity()));
		std::string bubbles;
		for (int k = 0; k < 14; ++k) {
			const double ph = std::fmod(s.t * 0.6 + hash01(k, 97), 1.0);
			bubbles += "AttributeBegin\n  Translate " + fmt3({-(cl.x + 0.06 + 0.04 * (hash01(k, 98) - 0.5)), kWater - 0.06 + 0.055 * ph, cl.z + 0.07 + 0.03 * (hash01(k, 99) - 0.5)}) +
					   "\n  Shape \"sphere\" \"float radius\" [ " + std::to_string(0.002 + 0.002 * hash01(k, 100)) + " ]\nAttributeEnd\n";
		}
		out += attr("Material \"dielectric\" \"float eta\" [ 1.0 ]", bubbles);
	}
	// the basking slate on its stack of stones, the heat lamp clamped over it
	{
		const Vec3 b = basking();
		out += attr(coated({0.36, 0.34, 0.32}, 0.5), emitMesh(nature::rock(0.09, 61), mul(translate(b.x, kGravel, b.z), scale(1.0, 2.2, 1.0))) +
														 emitMesh(nature::rock(0.08, 62), mul(translate(b.x - 0.02, kGravel + 0.10, b.z + 0.01), scale(1.0, 1.8, 1.0))));
		out += roundedAt(b - Vec3{0.0, 0.009, 0.0}, {0.15, 0.018, 0.11}, 0.006, coated({0.28, 0.28, 0.30}, 0.55), 12.0);
		const Vec3 bulb = heatLampBulb();
		out += attr(metalMaterial({0.82, 0.82, 0.84}, 0.18), emitMesh(lathe({{0.0, 0.10}, {0.03, 0.10}, {0.075, 0.06}, {0.10, 0.0}, {0.098, -0.002}, {0.0, 0.08}}, 32), translate(bulb + Vec3{0.0, -0.02, 0.0})));
		out += attr(metalMaterial({0.25, 0.25, 0.26}, 0.3), bar(bulb + Vec3{0.0, 0.08, 0.0}, {bulb.x, kTankRim + 0.02, kTankZ0 - 0.01}, 0.008, 8) +
															 emitMesh(box(0.05, 0.04, 0.03), translate(bulb.x, kTankRim, kTankZ0 - 0.012)));
		if (s.heatLamp > 0.01) out += emissiveSphere(bulb + Vec3{0.0, 0.005, 0.0}, 0.03, Vec3{60.0, 42.0, 20.0} * s.heatLamp, "Material \"diffuse\" \"rgb reflectance\" [ 0.9 0.85 0.8 ]");
		else out += attr(coated({0.75, 0.70, 0.62}, 0.1), emitMesh(uvSphere(0.03, 16, 8), translate(bulb + Vec3{0.0, 0.005, 0.0})));
	}
	// the thermometer: a glass tube with red spirit, on a suction cup on the front glass - then off, floating, then a ramp
	{
		const Vec3 p = s.thermoState > 0.0 ? s.thermoPos : thermometerSpot();
		const double tilt = s.thermoState > 0.0 ? s.thermoTilt : 0.0;
		const Mat4 m = mul(translate(p), rotate({0, 0, 1}, tilt), rotate({0, 1, 0}, 90.0));
		out += attr("Material \"dielectric\" \"float eta\" [ 1.5 ]", emitMesh(capsule(0.007, 0.15, 12, 3), mul(m, translate(0.0, -0.075, 0.0))));
		out += attr(coated({0.80, 0.08, 0.06}, 0.1), emitMesh(capsule(0.0025, 0.10, 8, 2), mul(m, translate(0.0, -0.07, 0.0))));
		out += attr(coated({0.85, 0.85, 0.82}, 0.4), emitMesh(box(0.012, 0.13, 0.002), mul(m, translate(0.0, 0.0, -0.009))));   // the scale behind the spirit
		std::string marks;
		for (int k = 0; k < 9; ++k) marks += emitMesh(box(0.008, 0.0008, 0.0006), mul(m, translate(0.0, -0.055 + 0.014 * k, -0.0076)));
		out += attr(diffuseMaterial({0.1, 0.1, 0.1}), marks);
		out += attr(translucentMaterial({0.70, 0.75, 0.75}, {0.25, 0.28, 0.28}), emitMesh(lathe({{0.0, 0.0}, {0.014, 0.0}, {0.010, 0.006}, {0.004, 0.010}, {0.0, 0.010}}, 20),
																							 mul(m, translate(0.0, -0.06, 0.008), rotate({1, 0, 0}, 90.0))));   // the suction cup
	}
	// the filter's canister on the floor, and the ribbed hose
	{
		const Vec3 c = canister();
		out += attr(coated({0.15, 0.17, 0.18}, 0.3), emitMesh(roundedBox(0.20, 0.32, 0.20, 0.03, 3), translate(c + Vec3{0.0, 0.16, 0.0})));
		out += attr(coated({0.35, 0.60, 0.70}, 0.3), emitMesh(roundedBox(0.21, 0.04, 0.21, 0.015, 2), translate(c + Vec3{0.0, 0.33, 0.0})));
		out += attr("Material \"dielectric\" \"float eta\" [ 1.45 ] \"float roughness\" [ 0.35 ]", emitMesh(tube(hosePath(s), 0.0105, 12, 0.012), identity()));
		// the intake: a straight tube with a strainer, standing in the water when the hose is in its place
		if (s.hoseOut < 0.5) {
			const Vec3 top = hosePath(s).back();
			out += attr(coated({0.22, 0.22, 0.24}, 0.3), emitMesh(lathe({{0.0, 0.0}, {0.016, 0.0}, {0.016, 0.06}, {0.0, 0.06}}, 16), translate(top + Vec3{0.0, -0.08, 0.0})));
		}
		// drips from the hose's end onto the floor (scene 7), and a little puddle
		if (s.drip > 0.0) {
			const Vec3 end = hosePath(s).back();
			out += attr("Material \"dielectric\" \"float eta\" [ 1.33 ]", "AttributeBegin\n  Translate " + fmt3({-end.x, end.y - 0.03 - 0.2 * std::fmod(s.t * 1.7, 1.0), end.z}) + "\n  Shape \"sphere\" \"float radius\" [ 0.005 ]\nAttributeEnd\n");
			out += attr("Material \"dielectric\" \"float eta\" [ 1.33 ]", emitMesh(lathe({{0.0, 0.0}, {0.08 * s.drip, 0.0}, {0.0, 0.002}}, 24), translate(end.x, 0.001, end.z)));
		}
	}
	// the water surface: thin (the light gets through it to the rocks and the turtle), rippled where things go in
	out += attr("Material \"thindielectric\" \"float eta\" [ 1.33 ]", emitMesh(waterSurface(kTankX0 + 0.002, kTankX1 - 0.002, kTankZ0 + 0.002, kTankZ1 - 0.002, kWater, s.t, s.tankRipples), identity()));
	// pellets dropped in: float a moment, then sink
	{
		std::string pel;
		for (double t0 : s.pellets)
			for (int k = 0; k < 5; ++k) {
				const double a = s.t - t0 - 0.12 * k;
				if (a < 0.0 || a > 30.0) continue;
				const double y = a < 0.4 ? kWater + 0.25 * (1.0 - a / 0.4) * (1.0 - a / 0.4) : std::max(kGravel + 0.004, kWater + 0.002 - 0.012 * std::max(0.0, a - 2.0));
				pel += "AttributeBegin\n  Translate " + fmt3({-(0.55 + 0.03 * k), y, -2.20 + 0.02 * k}) + "\n  Shape \"sphere\" \"float radius\" [ 0.004 ]\nAttributeEnd\n";
			}
		if (!pel.empty()) out += attr(coated({0.55, 0.38, 0.18}, 0.5), pel);
	}
	return out;
}

// The pieces placed many times: pebbles in the tank, yarn tufts in the rug.
inline const std::string &objectDefinitions() {
	static const std::string s = [] {
		std::string out;
		const Vec3 pebble[4] = {{0.55, 0.50, 0.42}, {0.42, 0.40, 0.36}, {0.70, 0.62, 0.50}, {0.35, 0.30, 0.26}};
		for (int k = 0; k < 4; ++k) out += nature::objectBegin("pebble" + std::to_string(k)) + attr(coated(pebble[k], 0.4), emitMesh(nature::rock(0.014, 120 + k), identity())) + nature::objectEnd();
		const Vec3 plank[4] = {{0.52, 0.34, 0.19}, {0.47, 0.30, 0.17}, {0.56, 0.37, 0.21}, {0.44, 0.28, 0.15}};
		for (int k = 0; k < 4; ++k) out += nature::objectBegin("plank" + std::to_string(k)) + attr(coated(plank[k], 0.35), emitMesh(roundedBox(1.0, 0.02, 0.146, 0.003, 1), identity())) + nature::objectEnd();
		const Vec3 spines[7] = {{0.62, 0.15, 0.12}, {0.12, 0.30, 0.52}, {0.85, 0.75, 0.40}, {0.20, 0.45, 0.25}, {0.50, 0.30, 0.55}, {0.88, 0.86, 0.80}, {0.25, 0.22, 0.20}};
		for (int k = 0; k < 7; ++k) out += nature::objectBegin("book" + std::to_string(k)) + attr(coated(spines[k], 0.5), emitMesh(box(1.0, 1.0, 1.0), identity())) + nature::objectEnd();
		const Vec3 yarn[3] = {{0.64, 0.31, 0.23}, {0.58, 0.28, 0.20}, {0.68, 0.36, 0.26}};
		for (int k = 0; k < 3; ++k)
			out += nature::objectBegin("yarn" + std::to_string(k)) + attr(diffuseMaterial(yarn[k]), emitMesh(nature::grassTuft(9, 0.010, 0.003, 0.0012, 300 + 11 * k), identity())) + nature::objectEnd();
		return out;
	}();
	return s;
}

}  // namespace turtle_escape
}  // namespace cartoon
