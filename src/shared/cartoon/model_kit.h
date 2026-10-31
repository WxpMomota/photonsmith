#pragma once
// model_kit.h -- modeling, part 8: ready parts and whole models made from a few numbers.
//
// Kit pieces go into a part (`kit KIND ... [at X Y Z] [turn AX AY AZ]`), meshed hard-edged where they are machined and joined to whatever else the part has:
//   crate W H D        a wooden crate: planks framed by battens
//   barrel R H         a barrel with its bulge and two hoops
//   panel W H          a sheet-metal panel with a lip and a bolt at each corner
//   window W H         a window frame with a cross of glazing bars (the glass is a part of its own: `make building` does it so)
//   door W H           a panelled door with a knob
//   wheel R W [N]      a tyre round a hub with N spokes (5)
//   pipe R X Y Z ...   a pipe of radius R through the points (a sweep), with a flange at each end
//
// Generators (`make KIND [option value]...` in a model) write a whole model's parts, each with its material:
//   building  floors N, width W, depth D, storey H, roof flat|gable, windows N (a floor, a side), colour, seed
//   chair, table, shelf, lamp   size S, colour
//   car       size S, colour: a cartoon car - body, cabin, windows, four wheels, lights
//   rocket    size S, colour: fuselage, nose, fins, porthole, nozzle
//   boat      size S, colour: a hull lofted through its stations, a deck, a cabin, a mast

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "model_part.h"

namespace cartoon {
namespace model {
namespace kit {

inline Prim prim(const std::string &kind, Vec3 at, Vec3 size, double round = 0.0, Vec3 turn = {0, 0, 0}) {
	Prim p;
	p.kind = kind, p.at = at, p.size = size, p.round = round, p.turn = turn;
	return p;
}
inline Step add(Prim p) { return {Op::Add, 0.0, p}; }
inline Step blend(Prim p, double k) { return {Op::Blend, k, p}; }
inline Step cut(Prim p, double k = 0.0) { return {Op::Cut, k, p}; }
// A field meshed (hard: dual contouring with split normals), `cell` metres a cube.
inline Mesh meshOf(const std::vector<Step> &steps, double cell, bool hard = true) {
	Part p;
	p.sdf.steps = steps, p.hard = hard, p.cell = cell;
	return partMesh(p);
}

inline Mesh crate(double w, double h, double d) {
	std::vector<Step> s{add(prim("box", {0, 0.5 * h, 0}, {w * 0.94, h * 0.94, d * 0.94}, 0.004))};
	const double b = 0.08 * std::min({w, h, d});
	for (const double sx : {-1.0, 1.0})
		for (const double sz : {-1.0, 1.0}) s.push_back(add(prim("box", {sx * 0.5 * (w - b), 0.5 * h, sz * 0.5 * (d - b)}, {b, h, b}, 0.003)));   // (the corner posts)
	for (const double y : {0.5 * b, h - 0.5 * b}) s.push_back(add(prim("box", {0, y, 0}, {w, b, d}, 0.003)));   // (the battens round top and bottom)
	for (int k = 1; k < 4; ++k) s.push_back(cut(prim("box", {0, h * k / 4.0, 0}, {w * 1.2, 0.006, d * 1.2})));    // (the gaps between the planks)
	return meshOf(s, std::min({w, h, d}) / 40.0);
}
inline Mesh barrel(double r, double h) {
	Loft l;
	for (int k = 0; k <= 8; ++k) {
		const double u = k / 8.0, bulge = 1.0 + 0.16 * std::sin(kPi * u);
		l.rings.push_back({h * u, r * bulge, r * bulge});
	}
	Mesh m = loftMesh(l);
	for (const double u : {0.18, 0.82}) {
		const double rr = r * (1.0 + 0.16 * std::sin(kPi * u)) + 0.006;
		Sweep hoop;
		hoop.profile = rectProfile(0.012, 0.04), hoop.loop = true;
		for (int k = 0; k < 12; ++k) hoop.path.push_back({rr * std::cos(2 * kPi * k / 12), h * u, rr * std::sin(2 * kPi * k / 12)});
		append(m, sweepMesh(hoop));
	}
	return m;
}
inline Mesh panel(double w, double h) {
	std::vector<Step> s{add(prim("box", {0, 0, 0}, {w, h, 0.02}, 0.004)), cut(prim("box", {0, 0, 0.012}, {w - 0.04, h - 0.04, 0.012}))};
	for (const double sx : {-1.0, 1.0})
		for (const double sy : {-1.0, 1.0}) s.push_back(add(prim("cylinder", {sx * (0.5 * w - 0.03), sy * (0.5 * h - 0.03), 0.012}, {0.008, 0.01, 0}, 0.002, {90, 0, 0})));
	return meshOf(s, std::min(w, h) / 45.0);
}
inline Mesh window(double w, double h) {
	const double f = 0.05;
	std::vector<Step> s{add(prim("box", {0, 0, 0}, {w, h, 0.08}, 0.004)), cut(prim("box", {0, 0, 0}, {w - 2 * f, h - 2 * f, 0.2}))};
	s.push_back(add(prim("box", {0, 0, 0}, {w, 0.03, 0.04})));
	s.push_back(add(prim("box", {0, 0, 0}, {0.03, h, 0.04})));
	s.push_back(add(prim("box", {0, -0.5 * h - 0.02, 0.03}, {w + 0.06, 0.04, 0.14}, 0.004)));   // (the sill)
	return meshOf(s, std::min(w, h) / 30.0);
}
inline Mesh door(double w, double h) {
	std::vector<Step> s{add(prim("box", {0, 0.5 * h, 0}, {w, h, 0.05}, 0.004))};
	for (int r = 0; r < 2; ++r) s.push_back(cut(prim("box", {0, h * (0.3 + 0.42 * r), 0.03}, {w * 0.7, h * 0.32, 0.02}, 0.004)));
	s.push_back(add(prim("sphere", {0.38 * w, 0.48 * h, 0.05}, {0.03, 0, 0})));
	return meshOf(s, std::min(w, h) / 40.0);
}
inline Mesh wheel(double r, double width, int spokes) {
	std::vector<Step> s{add(prim("torus", {0, 0, 0}, {r - 0.25 * width, 0.5 * width, 0}, 0.0, {0, 0, 90}))};   // (the tyre, its axle along x)
	s.push_back(add(prim("cylinder", {0, 0, 0}, {0.25 * r, width * 0.9, 0}, 0.01, {0, 0, 90})));
	for (int k = 0; k < std::clamp(spokes, 0, 24); ++k) {
		const double a = 2 * kPi * k / spokes;
		Prim c = prim("capsule", {0, 0, 0}, {0.05 * r, 0, 0});
		c.to = {0, (r - 0.4 * width) * std::cos(a), (r - 0.4 * width) * std::sin(a)};
		s.push_back(add(c));
	}
	return meshOf(s, r / 22.0);
}
inline Mesh pipe(double r, const std::vector<Vec3> &path) {
	Sweep sw;
	sw.profile = circleProfile(r, 14), sw.path = path;
	Mesh m = sweepMesh(sw);
	if (path.size() >= 2)
		for (const auto &[a, b] : {std::pair<Vec3, Vec3>{path[0], path[1]}, {path.back(), path[path.size() - 2]}}) {   // (a flange at each end)
			Sweep f;
			f.profile = circleProfile(r * 1.6, 14), f.path = {a, a + normalize(b - a) * (0.8 * r)};
			append(m, sweepMesh(f));
		}
	return m;
}
inline const std::vector<std::string> &pieceKinds() {
	static const std::vector<std::string> k = {"crate", "barrel", "panel", "window", "door", "wheel", "pipe"};
	return k;
}

// ---- generators ----
inline Part part(const std::string &name, const std::string &finish, Vec3 colour, double rough = 0.4) {
	Part p;
	p.name = name, p.finish = finish, p.colour = colour, p.rough = rough;
	return p;
}
// A generator's parts, or "" in `error` and none when the kind is unknown. `opt` its options (absent: the defaults); `colour` the main colour when it was given.
inline std::vector<Part> make(const std::string &kind, const std::map<std::string, double> &opt, const Vec3 *colour, std::string &error) {
	const auto o = [&](const std::string &k, double d) { return opt.count(k) ? opt.at(k) : d; };
	const double S = o("size", 1.0);
	std::vector<Part> parts;
	if (kind == "building") {
		const int floors = std::clamp(static_cast<int>(o("floors", 3)), 1, 40), windows = std::clamp(static_cast<int>(o("windows", 3)), 0, 20);
		const double W = o("width", 8.0), D = o("depth", 6.0), H = o("storey", 3.0), top = floors * H;
		const int seed = static_cast<int>(o("seed", 1));
		const Vec3 wall = colour ? *colour : Vec3{0.85, 0.72, 0.58};
		Part walls = part("walls", "matte", wall);
		walls.hard = true, walls.cell = std::min(W, D) / 60.0;
		walls.sdf.steps.push_back(add(prim("box", {0, 0.5 * top, 0}, {W, top, D})));
		walls.sdf.steps.push_back(add(prim("box", {0, top + 0.15, 0}, {W + 0.3, 0.3, D + 0.3})));   // (the cornice)
		Part glass = part("glass", "glass", {1, 1, 1}, 0.0), frames = part("frames", "plastic", {0.95, 0.95, 0.92}, 0.3);
		const double ww = std::min(1.2, 0.6 * W / std::max(1, windows)), wh = 0.5 * H;
		for (int f = 0; f < floors; ++f)
			for (int k = 0; k < windows; ++k)
				for (const double side : {-1.0, 1.0}) {
					if (f == 0 && side > 0 && k == windows / 2) continue;   // (the door's place)
					const double x = (k + 0.5) / windows * W - 0.5 * W, y = (f + 0.55) * H, z = side * 0.5 * D;
					walls.sdf.steps.push_back(cut(prim("box", {x, y, z}, {ww, wh, 0.5})));
					glass.pieces.push_back(transformed(box(ww, wh, 0.01), translate({x, y, z - side * 0.12})));
					frames.pieces.push_back(transformed(window(ww, wh), mul(translate({x, y, z - side * 0.06}), rotate({0, 1, 0}, side > 0 ? 0.0 : 180.0))));
				}
		Part door = part("door", "plastic", lerp(Vec3{0.45, 0.2, 0.12}, Vec3{0.15, 0.3, 0.5}, hash01(seed, 3)), 0.4);
		door.pieces.push_back(transformed(kit::door(1.0, 2.1), translate({(windows / 2 + 0.5) / std::max(1, windows) * W - 0.5 * W, 0.0, 0.5 * D + 0.01})));
		Part roof = part("roof", "matte", {0.55, 0.22, 0.16});
		if (o("gable", 0.0) > 0.5 || opt.count("roofgable")) {
			Loft l;
			l.square = true, l.sides = 4;
			for (int k = 0; k <= 6; ++k) l.rings.push_back({top + 0.3 + 0.35 * D * k / 6.0, 0.5 * W + 0.4, (0.5 * D + 0.4) * (1.0 - k / 6.0) + 0.01});
			roof.lofts.push_back(l);
		} else {
			roof.sdf.steps.push_back(add(prim("box", {0, top + 0.35, 0}, {W - 0.4, 0.1, D - 0.4})));
			roof.hard = true, roof.cell = std::min(W, D) / 80.0;
		}
		parts = {walls, glass, frames, door, roof};
	} else if (kind == "chair" || kind == "table" || kind == "shelf") {
		Part p = part(kind, "plastic", colour ? *colour : Vec3{0.62, 0.42, 0.25}, 0.45);
		p.hard = true, p.cell = 0.006 * S;
		const double r = 0.01 * S;
		if (kind == "chair") {
			p.sdf.steps = {add(prim("box", {0, 0.45 * S, 0}, {0.45 * S, 0.04 * S, 0.45 * S}, r)), add(prim("box", {0, 0.72 * S, -0.205 * S}, {0.45 * S, 0.5 * S, 0.04 * S}, r))};
			for (const double sx : {-1.0, 1.0})
				for (const double sz : {-1.0, 1.0}) p.sdf.steps.push_back(add(prim("box", {sx * 0.2 * S, 0.215 * S, sz * 0.2 * S}, {0.04 * S, 0.43 * S, 0.04 * S}, r)));
		} else if (kind == "table") {
			p.sdf.steps = {add(prim("box", {0, 0.73 * S, 0}, {1.2 * S, 0.05 * S, 0.75 * S}, r))};
			for (const double sx : {-1.0, 1.0})
				for (const double sz : {-1.0, 1.0}) p.sdf.steps.push_back(add(prim("box", {sx * 0.54 * S, 0.355 * S, sz * 0.32 * S}, {0.05 * S, 0.71 * S, 0.05 * S}, r)));
		} else {
			for (const double sx : {-1.0, 1.0}) p.sdf.steps.push_back(add(prim("box", {sx * 0.4 * S, 0.9 * S, 0}, {0.03 * S, 1.8 * S, 0.3 * S}, r)));
			for (int k = 0; k < 5; ++k) p.sdf.steps.push_back(add(prim("box", {0, (0.05 + 0.42 * k) * S, 0}, {0.8 * S, 0.03 * S, 0.3 * S}, r)));
		}
		parts = {p};
	} else if (kind == "lamp") {
		Part base = part("base", "metal", colour ? *colour : Vec3{0.75, 0.6, 0.3}, 0.25), shade = part("shade", "matte", {0.95, 0.9, 0.75}), bulb = part("bulb", "glow", {1.0, 0.85, 0.6});
		base.sdf.steps = {add(prim("cylinder", {0, 0.02 * S, 0}, {0.12 * S, 0.04 * S, 0}, 0.01 * S)), blend(prim("capsule", {0, 0.02 * S, 0}, {0.012 * S, 0, 0}), 0.02 * S)};
		base.sdf.steps[1].prim.to = {0, 0.45 * S, 0};
		Loft l;
		l.capTop = l.capBottom = false;
		l.rings = {{0.38 * S, 0.17 * S, 0.17 * S}, {0.56 * S, 0.09 * S, 0.09 * S}};
		shade.lofts.push_back(l);
		shade.mods.push_back({"solidify", {0.004 * S}});
		bulb.glow = 6.0, bulb.sdf.steps = {add(prim("sphere", {0, 0.46 * S, 0}, {0.04 * S, 0, 0}))};
		parts = {base, shade, bulb};
	} else if (kind == "car") {
		const Vec3 c = colour ? *colour : Vec3{0.85, 0.15, 0.1};
		Part body = part("body", "plastic", c, 0.2), glass = part("windows", "plastic", {0.12, 0.18, 0.25}, 0.05), tyres = part("wheels", "plastic", {0.06, 0.06, 0.06}, 0.6),
			 lights = part("lights", "glow", {1.0, 0.95, 0.8});
		body.sdf.steps = {add(prim("box", {0, 0.55 * S, 0}, {1.7 * S, 0.5 * S, 3.6 * S}, 0.18 * S)), blend(prim("box", {0, 0.95 * S, -0.2 * S}, {1.45 * S, 0.5 * S, 1.9 * S}, 0.2 * S), 0.15 * S)};
		for (const double sz : {-1.0, 1.0}) body.sdf.steps.push_back(cut(prim("cylinder", {0, 0.38 * S, sz * 1.15 * S}, {0.42 * S, 1.9 * S, 0}, 0.0, {0, 0, 90}), 0.03 * S));   // (the wheel arches)
		glass.sdf.steps = {add(prim("box", {0, 1.0 * S, -0.2 * S}, {1.47 * S, 0.34 * S, 1.6 * S}, 0.12 * S))};
		for (const double sx : {-1.0, 1.0})
			for (const double sz : {-1.0, 1.0}) tyres.pieces.push_back(transformed(wheel(0.36 * S, 0.24 * S, 5), translate({sx * 0.78 * S, 0.36 * S, sz * 1.15 * S})));
		for (const double sx : {-1.0, 1.0}) lights.sdf.steps.push_back(add(prim("sphere", {sx * 0.55 * S, 0.62 * S, 1.78 * S}, {0.1 * S, 0, 0})));
		lights.glow = 3.0;
		parts = {body, glass, tyres, lights};
	} else if (kind == "rocket") {
		const Vec3 c = colour ? *colour : Vec3{0.92, 0.92, 0.9};
		Part hull = part("hull", "plastic", c, 0.25), fins = part("fins", "plastic", {0.85, 0.15, 0.1}, 0.3), port = part("porthole", "plastic", {0.2, 0.5, 0.8}, 0.05),
			 nozzle = part("nozzle", "metal", {0.6, 0.6, 0.62}, 0.3);
		Loft l;
		for (int k = 0; k <= 12; ++k) {
			const double u = k / 12.0, r = 0.5 * S * (u < 0.7 ? 1.0 : std::sqrt(std::max(0.0, 1.0 - std::pow((u - 0.7) / 0.3, 2.0))));
			l.rings.push_back({4.0 * S * u, std::max(0.005 * S, r), std::max(0.005 * S, r)});
		}
		hull.lofts.push_back(l);
		fins.sdf.steps.push_back(add(prim("box", {0, 0.5 * S, 0.75 * S}, {0.06 * S, 1.0 * S, 0.6 * S}, 0.02 * S, {-20, 0, 0})));
		fins.mods.push_back({"radial", {3}});   // (one fin, three round)
		port.sdf.steps = {add(prim("cylinder", {0, 2.6 * S, 0.48 * S}, {0.18 * S, 0.08 * S, 0}, 0.02 * S, {90, 0, 0}))};
		nozzle.lofts.push_back(Loft{{{-0.4 * S, 0.38 * S, 0.38 * S}, {0.0, 0.25 * S, 0.25 * S}}, 24, false, false, true});
		nozzle.mods.push_back({"solidify", {0.02 * S}});
		parts = {hull, fins, port, nozzle};
	} else if (kind == "boat") {
		const Vec3 c = colour ? *colour : Vec3{0.2, 0.35, 0.7};
		Part hull = part("hull", "plastic", c, 0.3), deck = part("deck", "matte", {0.7, 0.55, 0.35}), cabin = part("cabin", "plastic", {0.95, 0.95, 0.92}, 0.3),
			 mast = part("mast", "matte", {0.55, 0.4, 0.25});
		// the hull: stations along its length (x across, y up, z along), narrow at the bow, a transom at the stern, lofted with its length upright then laid down
		Loft l;
		l.capTop = false;
		for (int k = 0; k <= 10; ++k) {
			const double u = k / 10.0, beam = 0.9 * S * std::sqrt(std::max(0.02, 1.0 - std::pow(std::max(0.0, u - 0.35) / 0.65, 2.0)));
			l.rings.push_back({4.0 * S * u, beam, 0.55 * S * (1.0 - 0.3 * u), 0.0, 0.3 * S * u * u});
		}
		hull.lofts.push_back(l);
		hull.mods.push_back({"rotate", {90, 0, 0}});
		hull.mods.push_back({"move", {0, 0.55 * S, -2.0 * S}});
		deck.sdf.steps = {add(prim("box", {0, 0.5 * S, -0.3 * S}, {1.5 * S, 0.06 * S, 2.6 * S}, 0.02 * S))};
		cabin.sdf.steps = {add(prim("box", {0, 0.85 * S, -0.6 * S}, {1.0 * S, 0.65 * S, 1.2 * S}, 0.08 * S)), cut(prim("box", {0, 0.95 * S, -0.6 * S}, {1.2 * S, 0.22 * S, 0.8 * S}, 0.03 * S))};
		Prim m = prim("capsule", {0, 0.5 * S, 0.6 * S}, {0.04 * S, 0, 0});
		m.to = {0, 3.2 * S, 0.6 * S};
		mast.sdf.steps = {add(m)};
		parts = {hull, deck, cabin, mast};
	} else {
		error = "`make " + kind + "`: building, chair, table, shelf, lamp, car, rocket or boat";
	}
	return parts;
}

}  // namespace kit
}  // namespace model
}  // namespace cartoon
