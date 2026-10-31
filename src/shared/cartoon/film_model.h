#pragma once
// film_model.h -- models written in a film file (modeling round, 2026-10-30): a `model NAME` block, its pieces built from shapes, cages, curves and kit parts, and the
// model placed in the set with `place NAME at X Y Z` or scattered with `scatter NAME N in X0 Z0 X1 Z1`.
//
//     model robot
//       piece body  colour 0.85 0.3 0.2  rough 0.3            a piece and its material: plastic (the default), matte, metal, glass or glow G
//         hard                                                 (meshed with sharp edges: dual contouring, model_sdf.h)
//         box 0 1 0  size 0.6 0.8 0.4  round 0.04              shapes (model_sdf.h): sphere, box, ellipsoid, capsule, cylinder, torus, cone, `turn AX AY AZ`
//         blend 0.06 sphere 0 1.55 0  radius 0.22               ... combined: add (the default), blend K, cut [soft K], intersect
//         cut cylinder 0 1.55 0.2  radius 0.06 height 0.3 turn 90 0 0
//       piece eyes  glow 4  colour 0.4 0.9 1
//         sphere 0.08 1.6 0.2 radius 0.03
//         mirror x                                             modifiers (model_modifiers.h), and `displace PATTERN A [size S]` (model_detail.h)
//       piece head
//         cage box 0.5 0.4 0.45 crease 1   levels 2            a subdivision cage (model_subdiv.h): `cage box|file`, `v`, `f`, `crease`, `corner`, `pull`, `extrude`
//       piece hose  colour 0.2 0.2 0.2
//         sweep circle 0.03 path 0.3 1 0  0.5 0.8 0.1  0.4 0.5 0.3   a sweep or a `loft` with `ring` lines (model_curves.h)
//       piece crate
//         kit crate 0.6 0.5 0.6 at 1 0 0                       kit parts (model_kit.h)
//         paint base 0.6 0.45 0.3 wear 0.9 0.8 0.6 0.8 dirt 0.2 0.15 0.1 0.6    a painted texture (model_texture.h)
//     end
//     model house
//       make building floors 2 width 7 depth 5 roof gable       a whole model from a generator (model_kit.h)
//     end
//     place robot at 0 0 -3 turn 30 scale 1.2
//
// Shapes are in the model's own metres, y up; a model is placed by its origin (pbrt's x y z, as `place` places a tree). Each piece is meshed once and kept in the film's
// cache folder by a hash of what makes it, so an edit to one piece remakes that piece alone.
//
// Levels of detail and instancing (part 9): a model placed more than once in a frame is written once as a pbrt object and instanced (one copy in memory however many there
// are); each placement is drawn at the level of detail its size on the screen asks for - level 1 within 25 of its sizes from the camera, 2 to 100, 4 beyond (fields meshed
// 2 or 4 times coarser, cages a level or two less subdivided).

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_cache.h"
#include "model_doctor.h"
#include "model_kit.h"
#include "model_part.h"
#include "nature_lib.h"

namespace cartoon {
namespace model {

struct Model {
	std::string name;
	std::vector<Part> parts;
	int line = 0;
	bool closed = false;   // (its `end` has been read)
};
struct Placed {
	std::string model;
	Vec3 at{0, 0, 0};   // pbrt's
	double scale = 1.0, turn = 0.0;
};

namespace detail {
inline bool num(const std::string &s, double &v) {
	char *end = nullptr;
	v = std::strtod(s.c_str(), &end);
	return end && *end == '\0' && !s.empty() && std::isfinite(v);
}
}  // namespace detail

// ---- reading a model block ----
// One line of a model block into `m` (`t` its words): "" or what is wrong with it. `end` closes the block. (`cage file NAME.obj`: under film_assets, or a path.)
// One line of a piece: shapes with their op, and the piece's settings. `mine` false (and "") when the line is not one of these.
inline std::string shapeLine(Part &p, const std::vector<std::string> &t, bool &mine) {
	using detail::num;
	std::vector<double> v;
	std::size_t i = 1;
	const auto numbers = [&](std::size_t n) {
		v.assign(n, 0.0);
		for (std::size_t k = 0; k < n; ++k)
			if (i + k >= t.size() || !num(t[i + k], v[k])) return false;
		i += n;
		return true;
	};
	const std::string verb = t[0];
	(void)numbers, (void)verb;
	mine = true;
	// shapes, with their op
	Op op = Op::Add;
	double k = 0.0;
	std::size_t s = 0;
	if (t[s] == "add") ++s;
	else if (t[s] == "intersect") op = Op::Intersect, ++s;
	else if (t[s] == "blend") {
		if (t.size() < 3 || !num(t[1], k) || k <= 0) return "`blend K SHAPE ...`: K how wide the join is (metres)";
		op = Op::Blend, s = 2;
	} else if (t[s] == "cut") {
		op = Op::Cut, ++s;
		if (s + 1 < t.size() && t[s] == "soft") {
			if (!num(t[s + 1], k) || k <= 0) return "`cut soft K SHAPE ...`";
			s += 2;
		}
	}
	static const std::vector<std::string> shapes = {"sphere", "box", "ellipsoid", "capsule", "cylinder", "torus", "cone"};
	if (s < t.size() && std::find(shapes.begin(), shapes.end(), t[s]) != shapes.end()) {
		Prim pr;
		pr.kind = t[s];
		const std::string usage = pr.kind == "sphere"	  ? "`sphere X Y Z radius R`"
								  : pr.kind == "box"	  ? "`box X Y Z size W H D [round R]`"
								  : pr.kind == "ellipsoid" ? "`ellipsoid X Y Z size W H D`"
								  : pr.kind == "capsule"  ? "`capsule X Y Z to X Y Z radius R`"
								  : pr.kind == "cylinder" ? "`cylinder X Y Z radius R height H [round R]`"
								  : pr.kind == "torus"	  ? "`torus X Y Z radius R tube r`"
														  : "`cone X Y Z radius R1 R2 height H`";
		i = s + 1;
		if (!numbers(3)) return usage + " (and `turn AX AY AZ`)";
		pr.at = {v[0], v[1], v[2]};
		bool sized = pr.kind == "capsule" ? false : false;
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "radius") {
				if (pr.kind == "cone") {
					if (!numbers(2)) return usage;
					pr.size.x = v[0], pr.size.y = v[1];
				} else {
					if (!numbers(1)) return usage;
					pr.size.x = v[0];
				}
				sized = true;
			} else if (w == "size" && numbers(3)) pr.size = {v[0], v[1], v[2]}, sized = true;
			else if (w == "height" && numbers(1)) (pr.kind == "cone" ? pr.size.z : pr.size.y) = v[0];
			else if (w == "tube" && numbers(1)) pr.size.y = v[0];
			else if (w == "round" && numbers(1)) pr.round = v[0];
			else if (w == "to" && numbers(3)) pr.to = {v[0], v[1], v[2]};
			else if (w == "turn" && numbers(3)) pr.turn = {v[0], v[1], v[2]};
			else return usage + " (and `turn AX AY AZ`): \"" + w + "\"";
		}
		if (!sized) return usage + ": its size";
		if (pr.size.x <= 0.0 || pr.size.y < 0.0 || pr.size.z < 0.0) return usage + ": sizes above 0";
		p.sdf.steps.push_back({op, k, pr});
		return std::string();
	}
	if (s > 0) return "`" + t[0] + "` is followed by a shape: sphere, box, ellipsoid, capsule, cylinder, torus or cone";
	// the piece's settings
	if (verb == "round" || verb == "shell" || verb == "detail") {
		if (!numbers(1) || v[0] < 0) return "`" + verb + " METRES`";
		(verb == "round" ? p.sdf.round : verb == "shell" ? p.sdf.shell : p.cell) = v[0];
		if (verb == "detail" && v[0] > 0 && v[0] < 0.0005) return "`detail` is the meshing cube's size: 0.5 mm at the finest";
		return std::string();
	}
	if (verb == "hard") {
		p.hard = true;
		if (i < t.size()) {
			if (t[i++] != "smooth" || !numbers(1) || v[0] < 0 || v[0] > 180) return "`hard [smooth DEGREES]`";
			p.smoothAngle = v[0];
		}
		return std::string();
	}
	mine = false;
	return std::string();
}
// One line of a piece: a subdivision cage. `mine` false (and "") when the line is not one of these.
inline std::string cageLine(Part &p, const std::vector<std::string> &t, bool &mine) {
	using detail::num;
	std::vector<double> v;
	std::size_t i = 1;
	const auto numbers = [&](std::size_t n) {
		v.assign(n, 0.0);
		for (std::size_t k = 0; k < n; ++k)
			if (i + k >= t.size() || !num(t[i + k], v[k])) return false;
		i += n;
		return true;
	};
	const std::string verb = t[0];
	(void)numbers, (void)verb;
	mine = true;
	// a subdivision cage
	if (verb == "cage") {
		p.hasCage = true;
		if (t.size() >= 2 && t[1] == "box") {
			i = 2;
			if (!numbers(3)) return "`cage box W H D [crease S]`";
			const Vec3 size{v[0], v[1], v[2]};
			double crease = 0.0;
			if (i < t.size()) {
				if (t[i++] != "crease" || !numbers(1)) return "`cage box W H D [crease S]`";
				crease = v[0];
			}
			p.cage = cageBox(size.x, size.y, size.z, crease);
		} else if (t.size() == 3 && t[1] == "file") {
			std::string error;
			const std::string found = film_cache::asset(t[2]);
			p.cage = readObjCage(found.empty() ? t[2] : found, error);
			if (!error.empty()) return "`cage file " + t[2] + "`: " + error;
		} else if (t.size() != 1) {
			return "`cage` (then v and f lines), `cage box W H D [crease S]` or `cage file NAME.obj`";
		}
		return std::string();
	}
	if (verb == "v" || verb == "f" || verb == "crease" || verb == "corner" || verb == "levels" || verb == "pull" || verb == "extrude") {
		if (!p.hasCage) return "`" + verb + "` belongs to a cage: a `cage` line first";
		Cage &c = p.cage;
		const int nv = static_cast<int>(c.verts.size());
		if (verb == "v") {
			if (!numbers(3) || t.size() != 4) return "`v X Y Z`";
			c.verts.push_back({v[0], v[1], v[2]});
		} else if (verb == "f") {
			std::vector<int> face;
			for (std::size_t q = 1; q < t.size(); ++q) {
				double x = 0.0;
				if (!num(t[q], x) || x < 0 || x >= nv || x != std::floor(x)) return "`f A B C [D ...]`: vertices from 0 to " + std::to_string(nv - 1) + " (the `v` lines in order)";
				face.push_back(static_cast<int>(x));
			}
			if (face.size() < 3) return "`f A B C [D ...]`: three corners or more";
			c.faces.push_back(face);
		} else if (verb == "crease") {
			if (!numbers(3) || v[0] < 0 || v[1] < 0 || v[0] >= nv || v[1] >= nv) return "`crease A B SHARPNESS`";
			c.crease(static_cast<int>(v[0]), static_cast<int>(v[1]), v[2]);
		} else if (verb == "corner") {
			if (!numbers(1) || v[0] < 0 || v[0] >= nv) return "`corner V`";
			c.corners.push_back(static_cast<int>(v[0]));
		} else if (verb == "levels") {
			if (!numbers(1) || v[0] < 0 || v[0] > 5) return "`levels N` (0 to 5)";
			p.levels = static_cast<int>(v[0]);
		} else if (verb == "pull") {
			if (!numbers(4) || v[0] < 0 || v[0] >= nv) return "`pull V DX DY DZ`";
			c.verts[static_cast<std::size_t>(v[0])] = c.verts[static_cast<std::size_t>(v[0])] + Vec3{v[1], v[2], v[3]};
		} else {   // extrude F D [scale S]: the face pushed out along its normal on new sides
			if (!numbers(2) || v[0] < 0 || v[0] >= c.faces.size()) return "`extrude FACE DISTANCE [scale S]` (faces numbered from 0 in order)";
			const int f = static_cast<int>(v[0]);
			const double dist = v[1];
			double sc = 1.0;
			if (i < t.size()) {
				if (t[i++] != "scale" || !numbers(1)) return "`extrude FACE DISTANCE [scale S]`";
				sc = v[0];
			}
			const std::vector<int> face = c.faces[f];
			Vec3 n{0, 0, 0}, mid{0, 0, 0};
			for (std::size_t q = 0; q < face.size(); ++q) n = n + cross(c.verts[face[q]], c.verts[face[(q + 1) % face.size()]]), mid = mid + c.verts[face[q]];
			n = normalize(n), mid = mid * (1.0 / face.size());
			std::vector<int> top;
			for (int q : face) top.push_back(static_cast<int>(c.verts.size())), c.verts.push_back(mid + n * dist + (c.verts[q] - mid) * sc);
			c.faces[f] = top;
			for (std::size_t q = 0; q < face.size(); ++q) {
				const std::size_t r = (q + 1) % face.size();
				c.faces.push_back({face[q], face[r], top[r], top[q]});
			}
		}
		return std::string();
	}
	mine = false;
	return std::string();
}
// One line of a piece: sweeps and lofts. `mine` false (and "") when the line is not one of these.
inline std::string curveLine(Part &p, const std::vector<std::string> &t, bool &mine) {
	using detail::num;
	std::vector<double> v;
	std::size_t i = 1;
	const auto numbers = [&](std::size_t n) {
		v.assign(n, 0.0);
		for (std::size_t k = 0; k < n; ++k)
			if (i + k >= t.size() || !num(t[i + k], v[k])) return false;
		i += n;
		return true;
	};
	const std::string verb = t[0];
	(void)numbers, (void)verb;
	mine = true;
	// curves
	if (verb == "sweep") {
		Sweep sw;
		i = 1;
		if (i < t.size() && t[i] == "circle") {
			++i;
			if (!numbers(1)) return "`sweep circle R path ...`";
			sw.profile = circleProfile(v[0]);
		} else if (i < t.size() && t[i] == "rect") {
			++i;
			if (!numbers(2)) return "`sweep rect W H path ...`";
			sw.profile = rectProfile(v[0], v[1]);
		} else if (i < t.size() && t[i] == "star") {
			++i;
			if (!numbers(3) || v[0] < 3) return "`sweep star N R1 R2 path ...`";
			sw.profile = starProfile(static_cast<int>(v[0]), v[1], v[2]);
		} else if (i < t.size() && t[i] == "shape") {
			++i;
			double a = 0.0, b = 0.0;
			while (i + 1 < t.size() && num(t[i], a) && num(t[i + 1], b)) sw.profile.push_back({a, b}), i += 2;
			if (sw.profile.size() < 3) return "`sweep shape X Y X Y X Y ... path ...`: three points or more";
		} else {
			return "`sweep circle R|rect W H|star N R1 R2|shape X Y ... path X Y Z X Y Z ... [twist A] [taper S] [loop]`";
		}
		if (i >= t.size() || t[i++] != "path") return "a sweep's `path X Y Z X Y Z ...` (two points or more)";
		double x = 0.0;
		while (i + 2 < t.size() && num(t[i], x)) {
			if (!numbers(3)) return "a sweep's path: X Y Z for each point";
			sw.path.push_back({v[0], v[1], v[2]});
		}
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "loop") sw.loop = true;
			else if (w == "twist" && numbers(1)) sw.twist = v[0];
			else if (w == "taper" && numbers(1)) sw.taper = v[0];
			else return "\"" + w + "\": a sweep takes twist A, taper S and loop";
		}
		if (sw.path.size() < 2) return "a sweep's path needs two points or more";
		p.sweeps.push_back(sw);
		return std::string();
	}
	if (verb == "loft") {
		Loft l;
		for (i = 1; i < t.size();) {
			const std::string w = t[i++];
			if (w == "square") l.square = true;
			else if (w == "open") l.capTop = l.capBottom = false;
			else if (w == "sides" && numbers(1) && v[0] >= 3 && v[0] <= 128) l.sides = static_cast<int>(v[0]);
			else return "`loft [sides N] [square] [open]` (then `ring Y RX RZ [at X Z]` lines)";
		}
		p.lofts.push_back(l);
		return std::string();
	}
	if (verb == "ring") {
		if (p.lofts.empty()) return "`ring` belongs to a loft: a `loft` line first";
		Ring r;
		if (!numbers(3)) return "`ring Y RX RZ [at X Z]`";
		r.y = v[0], r.rx = v[1], r.rz = v[2];
		if (i < t.size()) {
			if (t[i++] != "at" || !numbers(2)) return "`ring Y RX RZ [at X Z]`";
			r.x = v[0], r.z = v[1];
		}
		p.lofts.back().rings.push_back(r);
		return std::string();
	}
	mine = false;
	return std::string();
}
// One line of a piece: paint and kit parts. `mine` false (and "") when the line is not one of these.
inline std::string paintKitLine(Part &p, const std::vector<std::string> &t, bool &mine) {
	using detail::num;
	std::vector<double> v;
	std::size_t i = 1;
	const auto numbers = [&](std::size_t n) {
		v.assign(n, 0.0);
		for (std::size_t k = 0; k < n; ++k)
			if (i + k >= t.size() || !num(t[i + k], v[k])) return false;
		i += n;
		return true;
	};
	const std::string verb = t[0];
	(void)numbers, (void)verb;
	mine = true;
	// paint
	if (verb == "paint") {
		Paint &q = p.paint;
		q.on = true;
		for (i = 1; i < t.size();) {
			const std::string w = t[i++];
			if (w == "base" && numbers(3)) q.base = {v[0], v[1], v[2]};
			else if ((w == "wear" || w == "dirt" || w == "spots") && numbers(4)) (w == "wear" ? q.wear : w == "dirt" ? q.dirt : q.spot) = {v[0], v[1], v[2]}, (w == "wear" ? q.wearAmount : w == "dirt" ? q.dirtAmount : q.spotAmount) = v[3];
			else if (w == "stripes" && numbers(4)) {
				q.stripe = {v[0], v[1], v[2]}, q.stripeWidth = v[3];
				if (i < t.size() && (t[i] == "x" || t[i] == "y" || t[i] == "z")) q.stripeAxis = t[i++][0];
			} else if (w == "mottle" && numbers(1)) q.mottle = v[0];
			else if (w == "res" && numbers(1) && v[0] >= 64 && v[0] <= 4096) q.res = static_cast<int>(v[0]);
			else if (w == "seed" && numbers(1)) q.seed = static_cast<int>(v[0]);
			else return "\"" + w + "\": `paint base R G B [wear R G B A] [dirt R G B A] [mottle A] [stripes R G B W [x|y|z]] [spots R G B A] [res N] [seed K]`";
		}
		return std::string();
	}
	// kit parts
	if (verb == "kit") {
		if (t.size() < 2 || std::find(kit::pieceKinds().begin(), kit::pieceKinds().end(), t[1]) == kit::pieceKinds().end())
			return "`kit KIND ...`: crate W H D, barrel R H, panel W H, window W H, door W H, wheel R W [N], pipe R X Y Z ...";
		const std::string kind = t[1];
		i = 2;
		Mesh piece;
		if (kind == "pipe") {
			if (!numbers(1)) return "`kit pipe R X Y Z X Y Z ...`";
			const double r = v[0];
			std::vector<Vec3> path;
			double x = 0.0;
			while (i + 2 < t.size() && num(t[i], x) && numbers(3)) path.push_back({v[0], v[1], v[2]});
			if (path.size() < 2) return "`kit pipe R X Y Z X Y Z ...`: two points or more";
			piece = kit::pipe(r, path);
		} else {
			const std::size_t need = kind == "crate" ? 3 : kind == "wheel" ? 2 : 2;
			if (!numbers(need)) return "`kit " + kind + "` sizes";
			if (kind == "crate") piece = kit::crate(v[0], v[1], v[2]);
			else if (kind == "barrel") piece = kit::barrel(v[0], v[1]);
			else if (kind == "panel") piece = kit::panel(v[0], v[1]);
			else if (kind == "window") piece = kit::window(v[0], v[1]);
			else if (kind == "door") piece = kit::door(v[0], v[1]);
			else {
				const double r = v[0], w = v[1];
				double spokes = 5.0;
				if (i < t.size() && num(t[i], spokes)) ++i;
				piece = kit::wheel(r, w, static_cast<int>(spokes));
			}
		}
		Vec3 at{0, 0, 0}, turn{0, 0, 0};
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "at" && numbers(3)) at = {v[0], v[1], v[2]};
			else if (w == "turn" && numbers(3)) turn = {v[0], v[1], v[2]};
			else return "\"" + w + "\": a kit part takes at X Y Z and turn AX AY AZ";
		}
		p.pieces.push_back(transformed(piece, mul(translate(at), rotate({0, 0, 1}, turn.z), rotate({0, 1, 0}, turn.y), rotate({1, 0, 0}, turn.x))));
		return std::string();
	}
	mine = false;
	return std::string();
}
// One line of a piece: displacement and the modifiers. `mine` false (and "") when the line is not one of these.
inline std::string modifierLine(Part &p, const std::vector<std::string> &t, bool &mine) {
	using detail::num;
	std::vector<double> v;
	std::size_t i = 1;
	const auto numbers = [&](std::size_t n) {
		v.assign(n, 0.0);
		for (std::size_t k = 0; k < n; ++k)
			if (i + k >= t.size() || !num(t[i + k], v[k])) return false;
		i += n;
		return true;
	};
	const std::string verb = t[0];
	(void)numbers, (void)verb;
	mine = true;
	// modifiers
	if (verb == "displace") {
		Displace d;
		if (t.size() < 3 || std::find(patternKinds().begin(), patternKinds().end(), t[1]) == patternKinds().end())
			return "`displace noise|rock|bumps|scales|wood|ribs AMOUNT [size S] [seed K]`";
		i = 2;
		if (!numbers(1)) return "`displace PATTERN AMOUNT [size S] [seed K]`";
		d.kind = t[1], d.amount = v[0];
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "size" && numbers(1) && v[0] > 0.0005) d.size = v[0];
			else if (w == "seed" && numbers(1)) d.seed = static_cast<int>(v[0]);
			else return "\"" + w + "\": `displace PATTERN AMOUNT [size S] [seed K]` (size above 0.5 mm)";
		}
		p.displaces.push_back(d);
		p.mods.push_back({"displace", {static_cast<double>(p.displaces.size() - 1)}});
		return std::string();
	}
	if (std::find(modifierKinds().begin(), modifierKinds().end(), verb) != modifierKinds().end()) {
		Modifier mod;
		mod.kind = verb;
		for (i = 1; i < t.size(); ++i) {
			double x = 0.0;
			if (num(t[i], x)) mod.v.push_back(x);
			else if ((verb == "mirror" || verb == "bend") && (t[i] == "x" || t[i] == "y" || t[i] == "z")) mod.axis = t[i];
			else if (verb == "bend" && t[i] == "toward") continue;
			else return "\"" + t[i] + "\": `" + verb + "` takes numbers" + (verb == "mirror" ? " - `mirror x|y|z`" : verb == "bend" ? " - `bend A [toward x|z]`" : "");
		}
		if (verb == "mirror" && mod.v.empty() && t.size() != 2) return "`mirror x|y|z`";
		if ((verb == "array" || verb == "radial") && (mod.v.empty() || mod.v[0] < 1 || mod.v[0] > 500)) return "`" + verb + " N ...` (1 to 500 copies)";
		p.mods.push_back(mod);
		return std::string();
	}
	mine = false;
	return std::string();
}
inline std::string modelLine(Model &m, const std::vector<std::string> &t) {
	using detail::num;
	std::vector<double> v;
	std::size_t i = 1;
	const auto numbers = [&](std::size_t n) {
		v.assign(n, 0.0);
		for (std::size_t k = 0; k < n; ++k)
			if (i + k >= t.size() || !num(t[i + k], v[k])) return false;
		i += n;
		return true;
	};
	const std::string verb = t[0];
	if (verb == "end") {
		m.closed = true;
		for (const Part &p : m.parts)
			if (p.empty()) return "the piece \"" + p.name + "\" of model " + m.name + " has nothing in it (shapes, a cage, a sweep, a loft or kit parts)";
		if (m.parts.empty()) return "the model " + m.name + " has no pieces";
		return std::string();
	}
	if (verb == "make") {
		if (t.size() < 2) return "`make KIND [size S] [colour R G B] ...`: building, chair, table, shelf, lamp, car, rocket or boat";
		std::map<std::string, double> opt;
		Vec3 colour;
		bool coloured = false;
		for (i = 2; i < t.size();) {
			if (t[i] == "colour" && i + 3 < t.size()) {
				++i;
				if (!numbers(3)) return "`colour R G B` (0 to 1)";
				colour = {v[0], v[1], v[2]}, coloured = true;
			} else if (t[i] == "roof" && i + 1 < t.size()) {
				if (t[i + 1] == "gable") opt["roofgable"] = 1.0;
				i += 2;
			} else {
				double x = 0.0;
				if (i + 1 >= t.size() || !num(t[i + 1], x)) return "\"" + t[i] + "\": `make` takes size, floors, width, depth, storey, windows, roof gable|flat, colour R G B, seed";
				opt[t[i]] = x;
				i += 2;
			}
		}
		std::string error;
		const std::vector<Part> made = kit::make(t[1], opt, coloured ? &colour : nullptr, error);
		if (!error.empty()) return error;
		m.parts.insert(m.parts.end(), made.begin(), made.end());
		return std::string();
	}
	if (verb == "piece") {
		Part p;
		p.name = t.size() > 1 ? t[1] : "piece" + std::to_string(m.parts.size() + 1);
		for (i = 2; i < t.size();) {
			const std::string w = t[i++];
			if (w == "colour") {
				if (!numbers(3)) return "`colour R G B` (0 to 1)";
				p.colour = {v[0], v[1], v[2]};
			} else if (w == "rough") {
				if (!numbers(1) || v[0] < 0 || v[0] > 1) return "`rough R` (0 to 1)";
				p.rough = v[0];
			} else if (w == "glow") {
				if (!numbers(1) || v[0] < 0) return "`glow G` (how bright)";
				p.finish = "glow", p.glow = v[0];
			} else if (w == "plastic" || w == "matte" || w == "metal" || w == "glass") {
				p.finish = w;
			} else {
				return "\"" + w + "\": a piece takes colour R G B, rough R, plastic, matte, metal, glass or glow G";
			}
		}
		m.parts.push_back(p);
		return std::string();
	}
	if (m.parts.empty()) {   // (shapes before any `piece`: the model's first piece)
		Part p;
		m.parts.push_back(p);
	}
	Part &p = m.parts.back();
	for (const auto handler : {shapeLine, cageLine, curveLine, paintKitLine, modifierLine}) {
		bool mine = false;
		const std::string e = handler(p, t, mine);
		if (mine) return e;
	}
	return "\"" + verb + "\" is not a line of a model block (piece, make, shapes, round, shell, hard, detail, cage, v, f, crease, corner, levels, pull, extrude, sweep, loft, ring, "
						 "paint, kit, displace, the modifiers, end)";
}

// ---- drawing ----
// The name a piece is kept under in the film's cache.
constexpr int kMeshingVersion = 3;   // (raise it when the way a piece is meshed changes: the cache's old meshes are then made again)
inline std::string pieceName(const Model &m, const Part &p, int lod) {
	return "model_" + m.name + "_" + p.name + "_" + hashOf(keyOf(p) + "#v" + std::to_string(kMeshingVersion)).substr(0, 12) + "_lod" + std::to_string(lod);
}
// The model's pieces at level of detail `lod`, as scene text (in the model's own space: placed by whatever transform is round it).
inline std::string modelText(const std::string &film, const Model &m, int lod) {
	std::string out;
	for (const Part &p : m.parts) {
		const std::string name = pieceName(m, p, lod);
		// the mesh, unwrapped when it is painted, and its picture: each kept in the cache, both made from the same mesh (partMesh is deterministic)
		const std::string file = film_cache::ply(film, name, [&]() { return p.paint.on ? boxUnwrapped(partMesh(p, lod)) : partMesh(p, lod); });
		if (file.empty()) continue;
		std::string picture;
		if (p.paint.on) {
			const int N = std::clamp(p.paint.res, 64, 4096);
			picture = film_sky::picture(film, name + "_paint", N, N, [&](std::vector<float> &rgb) {
				const Mesh mesh = partMesh(p, lod);
				const std::vector<double> k = curvatureOf(mesh);
				std::vector<int> from;
				const Mesh uv = boxUnwrapped(mesh, &from);
				std::vector<double> kk(from.size());
				for (std::size_t q = 0; q < from.size(); ++q) kk[q] = k[static_cast<std::size_t>(from[q])];
				paintInto(rgb, uv, kk, p.paint);
			});
		}
		out += attr(materialOf(p, name + "_tex", picture), film_cache::plyShape(file));
	}
	return out;
}
// Which level of detail a placement `size` metres across, `distance` from the camera, is drawn at.
inline int lodFor(double size, double distance) {
	const double ratio = distance / std::max(0.05, size);
	return ratio < 25.0 ? 1 : ratio < 100.0 ? 2 : 4;
}
// A model's size across (its pieces' boxes at level 1; measured once per model and kept).
inline double sizeOf(const std::string &film, const Model &m) {
	static std::map<std::string, double> known;
	static std::mutex lock;
	std::string key = film + "|" + m.name;
	for (const Part &p : m.parts) key += "|" + hashOf(keyOf(p));
	{
		std::lock_guard<std::mutex> hold(lock);
		if (known.count(key)) return known[key];
	}
	Vec3 lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};
	for (const Part &p : m.parts) {
		if (!p.sdf.steps.empty() && !p.hasCage && p.sweeps.empty() && p.lofts.empty() && p.pieces.empty() && p.mods.empty()) {
			Vec3 a, b;
			p.sdf.bounds(a, b);
			lo = {std::min(lo.x, a.x), std::min(lo.y, a.y), std::min(lo.z, a.z)}, hi = {std::max(hi.x, b.x), std::max(hi.y, b.y), std::max(hi.z, b.z)};
			continue;
		}
		Vec3 a, b;
		boxOf(partMesh(p, 4), a, b);
		lo = {std::min(lo.x, a.x), std::min(lo.y, a.y), std::min(lo.z, a.z)}, hi = {std::max(hi.x, b.x), std::max(hi.y, b.y), std::max(hi.z, b.z)};
	}
	const double size = lo.x > hi.x ? 1.0 : std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
	std::lock_guard<std::mutex> hold(lock);
	return known[key] = size;
}
// Every placed model in a frame seen from pbrt's `eye`: a model placed once is written where it stands; one placed more than once is written once (per level of detail
// used) as an object and instanced.
inline std::string placedText(const std::string &film, const std::vector<Model> &models, const std::vector<Placed> &placed, const Vec3 &eye) {
	if (placed.empty()) return std::string();
	std::map<std::string, const Model *> byName;
	for (const Model &m : models) byName[m.name] = &m;
	std::map<std::pair<std::string, int>, std::vector<Mat4>> groups;
	for (const Placed &pl : placed) {
		const auto it = byName.find(pl.model);
		if (it == byName.end()) continue;
		const double size = sizeOf(film, *it->second) * pl.scale;
		const int lod = lodFor(size, length(pl.at - eye));
		groups[{pl.model, lod}].push_back(mul(translate({-pl.at.x, pl.at.y, pl.at.z}), rotate({0, 1, 0}, pl.turn), scale(pl.scale)));
	}
	std::string out = "# models (film_model.h)\n", defs;
	for (const auto &[key, frames] : groups) {
		const Model &m = *byName[key.first];
		if (frames.size() == 1) {
			out += "AttributeBegin\n" + film_cache::placement(frames[0]) + modelText(film, m, key.second) + "AttributeEnd\n";
			continue;
		}
		const std::string object = "model_" + m.name + "_lod" + std::to_string(key.second);
		defs += nature::objectBegin(object) + modelText(film, m, key.second) + nature::objectEnd();
		for (const Mat4 &f : frames) out += nature::placeObject(object, f);
	}
	return defs + out;
}

// ---- a turntable, for looking at a model by itself ----
// A scene of the model alone on a grey floor under a studio's three lights, seen from `angle` degrees round it (and a little above), framed to its size.
inline std::string turntableScene(const std::string &film, const Model &m, double angle, int width, int height, int spp) {
	const double size = sizeOf(film, m), d = 2.2 * size + 0.3, a = angle * kPi / 180.0;
	char head[700];
	std::snprintf(head, sizeof head,
				  "LookAt %.4f %.4f %.4f  0 %.4f 0  0 1 0\nCamera \"perspective\" \"float fov\" [ 35 ]\nFilm \"rgb\" \"integer xresolution\" [ %d ] \"integer yresolution\" [ %d ]\n"
				  "Sampler \"halton\" \"integer pixelsamples\" [ %d ]\nWorldBegin\nLightSource \"infinite\" \"rgb L\" [ 0.35 0.37 0.4 ]\n"
				  "LightSource \"distant\" \"point3 from\" [ 1 2 1.5 ] \"point3 to\" [ 0 0 0 ] \"rgb L\" [ 2.2 2.1 2 ]\n"
				  "LightSource \"distant\" \"point3 from\" [ -1.5 0.8 -1 ] \"point3 to\" [ 0 0 0 ] \"rgb L\" [ 0.6 0.65 0.75 ]\n"
				  "AttributeBegin\n  Material \"diffuse\" \"rgb reflectance\" [ 0.45 0.45 0.45 ]\n  Shape \"trianglemesh\" \"integer indices\" [ 0 1 2 0 2 3 ] \"point3 P\" [ -%g 0 -%g  %g 0 -%g  %g 0 %g  -%g 0 %g ]\nAttributeEnd\n",
				  d * std::sin(a), 0.45 * size + 0.35 * d, d * std::cos(a), 0.4 * size, width, height, spp, 20 * size, 20 * size, 20 * size, 20 * size, 20 * size, 20 * size, 20 * size, 20 * size);
	return std::string(head) + modelText(film, m, 1);
}

}  // namespace model
}  // namespace cartoon
