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
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_cache.h"
#include "model_doctor.h"
#include "model_anim.h"
#include "model_boolean.h"
#include "model_decimate.h"
#include "model_fracture.h"
#include "model_grow.h"
#include "model_import.h"
#include "model_kit.h"
#include "model_part.h"
#include "model_text.h"
#include "nature_lib.h"

namespace cartoon {
namespace model {

struct Model {
	std::string name;
	std::vector<Part> parts;
	int line = 0;
	bool closed = false;   // (its `end` has been read)
	// (modeling round 2) its lines as written and its parameters (`model NAME params A 1 B 2`, `$A` in its lines): another model can be made from it with other values
	// (`model NAME2 from NAME [A v]...`); a model from the library (`library/NAME.model`) waits for the film's load to be read (`library` its name there)
	std::vector<std::vector<std::string>> raw;
	std::map<std::string, double> params;
	std::string library;
	std::vector<std::string> variants;   // (`variants N`: the models it stands for, one picked for each placement)
	// (modeling round 3) `cover PIECE with MODEL N [up|slope D|all] [scale A B] [seed K]`: copies of another model strewn over a piece's surface (rivets, moss, leaves,
	// barnacles), made into pieces of this model when the film is loaded (DataFilm::load: coveredParts)
	struct Cover {
		std::string piece, with, rule = "all";
		int count = 100, seed = 1;
		double slope = 0.0, s0 = 1.0, s1 = 1.0;
	};
	std::vector<Cover> covers;
};
struct Placed {
	std::string model;
	Vec3 at{0, 0, 0};   // pbrt's
	double scale = 1.0, turn = 0.0;
	std::string id;     // (`as ID`: what `move`, `turn`, `grow`, `show` and `shatter` name it by - and ID.PIECE a piece of it)
};
// A line's words with each `$NAME` (or `$NAME*K`, `$NAME/K`, `$NAME+K`, `$NAME-K`, chained) put in from the model's parameters. `error` names a parameter it has not.
inline std::vector<std::string> substituted(const std::vector<std::string> &t, const std::map<std::string, double> &params, std::string &error) {
	std::vector<std::string> out;
	for (const std::string &w : t) {
		if (w.size() < 2 || w[0] != '$') {
			out.push_back(w);
			continue;
		}
		std::size_t i = 1;
		while (i < w.size() && (std::isalnum(static_cast<unsigned char>(w[i])) || w[i] == '_')) ++i;
		const std::string name = w.substr(1, i - 1);
		const auto it = params.find(name);
		if (it == params.end()) {
			error = "\"" + w + "\": the model has no parameter " + name + " (a `params` on its `model` line)";
			return t;
		}
		double v = it->second;
		while (i < w.size()) {
			const char op = w[i++];
			std::size_t j = i;
			while (j < w.size() && (std::isdigit(static_cast<unsigned char>(w[j])) || w[j] == '.')) ++j;
			const double k = std::atof(w.substr(i, j - i).c_str());
			if (j == i || (op != '*' && op != '/' && op != '+' && op != '-') || (op == '/' && k == 0.0)) {
				error = "\"" + w + "\": a parameter is followed by *K, /K, +K or -K";
				return t;
			}
			v = op == '*' ? v * k : op == '/' ? v / k : op == '+' ? v + k : v - k;
			i = j;
		}
		char buf[40];
		std::snprintf(buf, sizeof buf, "%.9g", v);
		out.push_back(buf);
	}
	return out;
}

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
			else if (w == "bake") d.bake = true;
			else return "\"" + w + "\": `displace PATTERN AMOUNT [size S] [seed K] [bake]` (size above 0.5 mm)";
		}
		if (p.openKey >= 0) return "`displace` cannot go in a shape key (it splits the mesh: the key could not blend)";
		if (d.bake && !p.paint.on) p.paint.on = true, p.paint.base = p.colour;   // (a baked pattern needs the piece's uvs and picture)
		if (d.bake) p.paint.normals = true;
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
		if (p.openKey >= 0) {   // (a shape key's modifiers: they must keep its vertices)
			if (verb == "mirror" || verb == "array" || verb == "radial" || verb == "solidify") return "`" + verb + "` cannot go in a shape key (it adds vertices: the key could not blend)";
			p.keys[static_cast<std::size_t>(p.openKey)].mods.push_back(mod);
		} else {
			p.mods.push_back(mod);
		}
		return std::string();
	}
	mine = false;
	return std::string();
}
// One line of a piece (modeling round 2): `tool`, `boolean`, `budget`, `key`, `mesh`, `texture`, `decal`. `mine` false (and "") when the line is not one of these.
inline std::string round2Line(Part &p, const std::vector<std::string> &t, bool &mine) {
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
	mine = true;
	if (verb == "tool") {
		if (t.size() != 1) return "`tool` (alone: the piece is not drawn, other pieces use it in `boolean`)";
		p.tool = true;
		return std::string();
	}
	if (verb == "boolean") {
		const char *usage = "`boolean cut|add|intersect|blend K PIECE [soft K]` (the PIECE a `tool` piece of the model, written before this one)";
		Part::Boolean b;
		if (t.size() < 3) return usage;
		i = 2;
		if (t[1] == "cut") b.op = Op::Cut;
		else if (t[1] == "add") b.op = Op::Add;
		else if (t[1] == "intersect") b.op = Op::Intersect;
		else if (t[1] == "blend") {
			b.op = Op::Blend;
			if (!numbers(1) || v[0] <= 0) return usage;
			b.k = v[0];
		} else return usage;
		if (i >= t.size()) return usage;
		b.with = t[i++];
		if (i < t.size()) {
			if (t[i++] != "soft" || !numbers(1) || v[0] <= 0) return usage;
			b.k = v[0];
		}
		p.booleans.push_back(b);
		return std::string();
	}
	if (verb == "budget") {
		if (!numbers(1) || v[0] < 12 || t.size() != 2) return "`budget N`: at most N triangles (12 or more)";
		p.budget = static_cast<int>(v[0]);
		return std::string();
	}
	if (verb == "key") {
		if (t.size() != 2) return "`key NAME` (then the modifiers that make it: smile, sad, open, surprised, angry and blink are blended in by feelings and speech)";
		Part::ShapeKey k;
		k.name = t[1];
		p.keys.push_back(k);
		p.openKey = static_cast<int>(p.keys.size()) - 1;
		return std::string();
	}
	if (verb == "mesh") {
		const char *usage = "`mesh FILE [fit H] [scale S] [at X Y Z] [turn AX AY AZ]` (a .ply, .obj, .gltf or .glb under film_assets, or a path)";
		if (t.size() < 2) return usage;
		const std::string found = film_cache::asset(t[1]), path = found.empty() ? t[1] : found;
		std::string error, ext;
		std::vector<unsigned char> image;
		Mesh m = importMesh(path, error, &image, &ext);
		if (!error.empty()) return "`mesh " + t[1] + "`: " + error;
		double fit = 0.0, sc = 1.0;
		Vec3 at{0, 0, 0}, turn{0, 0, 0};
		for (i = 2; i < t.size();) {
			const std::string w = t[i++];
			if (w == "fit" && numbers(1) && v[0] > 0) fit = v[0];
			else if (w == "scale" && numbers(1) && v[0] > 0) sc = v[0];
			else if (w == "at" && numbers(3)) at = {v[0], v[1], v[2]};
			else if (w == "turn" && numbers(3)) turn = {v[0], v[1], v[2]};
			else return usage;
		}
		if (fit > 0.0) m = fitted(m, fit);
		m = transformed(m, mul(translate(at), rotate({0, 0, 1}, turn.z), rotate({0, 1, 0}, turn.y), rotate({1, 0, 0}, turn.x), scale(sc)));
		std::error_code ec;
		p.sources.push_back(path + "#" + std::to_string(std::filesystem::file_size(path, ec)) + "#" + std::to_string(m.verts.size()));
		if (!image.empty() && p.texture.empty()) {   // (a glTF's own picture: kept in the temp folder by its contents, for `texture` to find)
			const std::filesystem::path f = std::filesystem::temp_directory_path() / ("photonsmith_model_" + hashOf(std::string(image.begin(), image.end())).substr(0, 12) + "." + ext);
			if (!std::filesystem::exists(f, ec)) std::ofstream(f, std::ios::binary).write(reinterpret_cast<const char *>(image.data()), static_cast<std::streamsize>(image.size()));
			p.texture = f.string();
		}
		p.pieces.push_back(m);
		return std::string();
	}
	if (verb == "texture") {
		const char *usage = "`texture FILE [scale S]`: a picture (under film_assets, or a path) on the piece - on an imported mesh's own uvs, or projected on a box S metres across";
		if (t.size() < 2) return usage;
		double sc = 1.0;
		for (i = 2; i < t.size();) {
			if (t[i++] != "scale" || !numbers(1) || v[0] <= 0) return usage;
			sc = v[0];
		}
		const bool ownUvs = !p.pieces.empty() && std::all_of(p.pieces.begin(), p.pieces.end(), [](const Mesh &m) { return !m.uvs.empty(); }) && p.sdf.steps.empty() && !p.hasCage;
		if (ownUvs) p.texture = t[1];
		else p.paint.on = true, p.paint.base = p.colour, p.paint.image = t[1], p.paint.imageScale = sc;
		return std::string();
	}
	if (verb == "decal") {
		const char *usage = "`decal FILE at X Y Z toward X Y Z size W H`: a picture stuck on where it is projected (its alpha kept)";
		Decal d;
		if (t.size() < 2) return usage;
		d.file = t[1];
		for (i = 2; i < t.size();) {
			const std::string w = t[i++];
			if (w == "at" && numbers(3)) d.at = {v[0], v[1], v[2]};
			else if (w == "toward" && numbers(3)) d.toward = {v[0], v[1], v[2]};
			else if (w == "size" && numbers(2) && v[0] > 0 && v[1] > 0) d.w = v[0], d.h = v[1];
			else return usage;
		}
		if (length(d.toward) < 1e-9) return usage;
		if (!p.paint.on) p.paint.on = true, p.paint.base = p.colour;
		p.paint.decals.push_back(d);
		return std::string();
	}
	mine = false;
	return std::string();
}

// One line of a piece (modeling round 3): skinned, bone, fur, cloth, stroke, remesh, close, region, unwrap, text, pbr. `mine` false (and "") when not one of these.
inline std::string round3Line(Part &p, const std::vector<std::string> &t, bool &mine) {
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
	const auto colourAt = [&](Vec3 &c) {
		if (!numbers(3)) return false;
		c = {v[0], v[1], v[2]};
		return true;
	};
	const std::string verb = t[0];
	mine = true;
	if (verb == "skinned") {
		if (t.size() != 1) return "`skinned` (alone: the piece bends with a character's bones, model_skin.h)";
		p.skin = true;
		return std::string();
	}
	if (verb == "bone") {
		const char *usage = "`bone NAME from X Y Z to X Y Z [parent NAME] [radius R]` (NAME: spine, head, leg.1 .., arm.l, arm.r, tail, or a name of your own for `pose`)";
		Bone b;
		if (t.size() < 10 || t[2] != "from" || t[6] != "to") return usage;
		b.name = t[1];
		i = 3;
		if (!numbers(3)) return usage;
		b.a = {v[0], v[1], v[2]};
		i = 7;
		if (!numbers(3)) return usage;
		b.b = {v[0], v[1], v[2]};
		for (i = 10; i < t.size();) {
			const std::string w = t[i++];
			if (w == "parent" && i < t.size()) b.parent = t[i++];
			else if (w == "radius" && numbers(1) && v[0] > 0.0) b.radius = v[0];
			else return usage;
		}
		p.bones.push_back(b), p.skin = true;
		return std::string();
	}
	if (verb == "fur") {
		const char *usage = "`fur LENGTH [density N] [width W] [colour R G B] [curl C] [droop D] [seed K]`";
		if (!numbers(1) || v[0] <= 0 || v[0] > 2) return usage;
		p.fur.on = true, p.fur.length = v[0], p.fur.colour = p.colour;
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "density" && numbers(1) && v[0] > 0) p.fur.density = v[0];
			else if (w == "width" && numbers(1) && v[0] > 0) p.fur.width = v[0];
			else if (w == "colour" && colourAt(p.fur.colour)) continue;
			else if (w == "curl" && numbers(1)) p.fur.curl = v[0];
			else if (w == "droop" && numbers(1)) p.fur.droop = v[0];
			else if (w == "seed" && numbers(1)) p.fur.seed = static_cast<int>(v[0]);
			else return usage;
		}
		return std::string();
	}
	if (verb == "cloth") {
		const char *usage = "`cloth W D [at X Y Z] [res N] [wave A]`: a sheet dropped onto the model";
		Cloth c;
		if (!numbers(2) || v[0] <= 0 || v[1] <= 0) return usage;
		c.w = v[0], c.d = v[1];
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "at" && numbers(3)) c.at = {v[0], v[1], v[2]};
			else if (w == "res" && numbers(1) && v[0] >= 6 && v[0] <= 120) c.res = static_cast<int>(v[0]);
			else if (w == "wave" && numbers(1) && v[0] >= 0) c.wave = v[0];
			else return usage;
		}
		p.cloths.push_back(c);
		return std::string();
	}
	if (verb == "stroke") {
		const char *usage = "`stroke inflate|crease|grab|smooth|pinch|flatten RADIUS STRENGTH path X Y Z ... [toward X Y Z]`";
		static const std::vector<std::string> kinds = {"inflate", "crease", "grab", "smooth", "pinch", "flatten"};
		Stroke k;
		if (t.size() < 4 || std::find(kinds.begin(), kinds.end(), t[1]) == kinds.end()) return usage;
		k.kind = t[1], i = 2;
		if (!numbers(2) || v[0] <= 0) return usage;
		k.radius = v[0], k.strength = v[1];
		if (i >= t.size() || t[i++] != "path") return usage;
		double x = 0.0;
		while (i + 2 < t.size() && num(t[i], x) && numbers(3)) k.path.push_back({v[0], v[1], v[2]});
		if (i < t.size()) {
			if (t[i++] != "toward" || !numbers(3)) return usage;
			k.toward = {v[0], v[1], v[2]};
		}
		if (k.path.empty() || i != t.size()) return usage;
		if (p.openKey >= 0) return "a `stroke` cannot go in a shape key";
		p.strokes.push_back(k);
		p.mods.push_back({"stroke", {static_cast<double>(p.strokes.size() - 1)}});
		return std::string();
	}
	if (verb == "remesh" || verb == "close") {
		Modifier mod;
		mod.kind = verb;
		if (verb == "remesh" && t.size() == 2) {
			if (!numbers(1) || v[0] < 0.0005) return "`remesh [CELL]` (the cube's size, 0.5 mm or more)";
			mod.v = v;
		} else if (t.size() != 1) {
			return verb == "remesh" ? "`remesh [CELL]`" : "`close` (alone)";
		}
		p.mods.push_back(mod);
		return std::string();
	}
	if (verb == "region") {
		const char *usage = "`region NAME above Y|below Y|facing up|down|front|back|left|right|near X Y Z R [colour R G B] [rough R] [plastic|matte|metal|glass|skin|glow G]`";
		Region r;
		if (t.size() < 3) return usage;
		r.name = t[1], r.rule = t[2], r.colour = p.colour, r.finish = p.finish, r.rough = p.rough;
		i = 3;
		if (r.rule == "above" || r.rule == "below") {
			if (!numbers(1)) return usage;
			r.value = v[0];
		} else if (r.rule == "facing") {
			static const std::vector<std::string> ways = {"up", "down", "front", "back", "left", "right"};
			if (i >= t.size() || std::find(ways.begin(), ways.end(), t[i]) == ways.end()) return usage;
			r.facing = t[i++];
		} else if (r.rule == "near") {
			if (!numbers(4) || v[3] <= 0) return usage;
			r.at = {v[0], v[1], v[2]}, r.radius = v[3];
		} else return usage;
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "colour" && colourAt(r.colour)) continue;
			else if (w == "rough" && numbers(1)) r.rough = v[0];
			else if (w == "glow" && numbers(1)) r.finish = "glow", r.glow = v[0];
			else if (w == "plastic" || w == "matte" || w == "metal" || w == "glass" || w == "skin") r.finish = w;
			else return usage;
		}
		p.regions.push_back(r);
		return std::string();
	}
	if (verb == "unwrap") {
		if (t.size() != 1) return "`unwrap` (alone: chart uvs for its paint and its export)";
		p.unwrap = true;
		return std::string();
	}
	if (verb == "text") {
		const char *usage = "`text \"WORDS\" [size H] [depth D] [font FILE] [at X Y Z] [turn AX AY AZ]`";
		if (t.size() < 2) return usage;
		double h = 0.3, d = 0.06;
		std::string font;
		Vec3 at{0, 0, 0}, turn{0, 0, 0};
		for (i = 2; i < t.size();) {
			const std::string w = t[i++];
			if (w == "size" && numbers(1) && v[0] > 0) h = v[0];
			else if (w == "depth" && numbers(1) && v[0] > 0) d = v[0];
			else if (w == "font" && i < t.size()) font = t[i++];
			else if (w == "at" && numbers(3)) at = {v[0], v[1], v[2]};
			else if (w == "turn" && numbers(3)) turn = {v[0], v[1], v[2]};
			else return usage;
		}
		std::string error;
		const Mesh m = textSolid(t[1], h, d, font, error);
		if (!error.empty()) return "`text`: " + error;
		p.pieces.push_back(transformed(m, mul(translate(at), rotate({0, 0, 1}, turn.z), rotate({0, 1, 0}, turn.y), rotate({1, 0, 0}, turn.x))));
		p.sources.push_back("text:" + t[1] + "#" + font + "#" + std::to_string(h) + "#" + std::to_string(d));
		return std::string();
	}
	if (verb == "pbr") {
		const char *usage = "`pbr base FILE [rough FILE] [normal FILE]`: an imported mesh's pictures on its own uvs";
		for (i = 1; i + 1 < t.size(); i += 2) {
			if (t[i] == "base") p.texture = t[i + 1];
			else if (t[i] == "rough") p.roughMap = t[i + 1];
			else if (t[i] == "normal") p.normalMap = t[i + 1];
			else return usage;
		}
		if (i != t.size() || p.texture.empty()) return usage;
		return std::string();
	}
	mine = false;
	return std::string();
}

inline std::string modelLine(Model &m, const std::vector<std::string> &raw) {
	using detail::num;
	if (raw.empty()) return std::string();
	m.raw.push_back(raw);
	std::string bad;
	const std::vector<std::string> t = substituted(raw, m.params, bad);
	if (!bad.empty()) return bad;
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
			} else if (t[i] == "kind" && i + 1 < t.size()) {   // (a tree's: oak, pine, palm, birch)
				static const std::vector<std::string> kinds = {"oak", "pine", "palm", "birch"};
				const auto it = std::find(kinds.begin(), kinds.end(), t[i + 1]);
				if (it == kinds.end()) return "`make tree kind oak|pine|palm|birch`";
				opt["kind"] = static_cast<double>(it - kinds.begin());
				i += 2;
			} else {
				double x = 0.0;
				if (i + 1 >= t.size() || !num(t[i + 1], x))
					return "\"" + t[i] + "\": `make` takes size, floors, width, depth, storey, windows, roof gable|flat, kind, height, count, legs, arms, tail, eyes, houses, length, "
										 "furnished, colour R G B, seed";
				opt[t[i]] = x;
				i += 2;
			}
		}
		std::string error;
		const std::vector<Part> made = grow::makeAny(t[1], opt, coloured ? &colour : nullptr, error);
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
			} else if (w == "plastic" || w == "matte" || w == "metal" || w == "glass" || w == "skin") {
				p.finish = w;
			} else {
				return "\"" + w + "\": a piece takes colour R G B, rough R, plastic, matte, metal, glass, skin or glow G";
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
	if (verb == "cover") {   // (the model's: copies of another model over one of its pieces)
		const char *usage = "`cover PIECE with MODEL N [up|slope D|all] [scale A B] [seed K]` (MODEL a model of the film; N 1 to 5000)";
		Model::Cover c;
		if (t.size() < 5 || t[2] != "with") return usage;
		c.piece = t[1], c.with = t[3];
		i = 4;
		if (!numbers(1) || v[0] < 1 || v[0] > 5000) return usage;
		c.count = static_cast<int>(v[0]);
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "up" || w == "all") c.rule = w;
			else if (w == "slope" && numbers(1)) c.rule = "slope", c.slope = v[0];
			else if (w == "scale" && numbers(2) && v[0] > 0 && v[1] > 0) c.s0 = v[0], c.s1 = v[1];
			else if (w == "seed" && numbers(1)) c.seed = static_cast<int>(v[0]);
			else return usage;
		}
		m.covers.push_back(c);
		return std::string();
	}
	for (const auto handler : {round3Line, round2Line, shapeLine, cageLine, curveLine, paintKitLine, modifierLine}) {
		bool mine = false;
		const std::string e = handler(p, t, mine);
		if (mine) return e;
	}
	return "\"" + verb + "\" is not a line of a model block (piece, make, shapes, round, shell, hard, detail, cage, v, f, crease, corner, levels, pull, extrude, sweep, loft, ring, "
						 "paint, kit, displace, the modifiers, end)";
}

// Model `out` made again from `from`'s lines with these parameters: "" or what is wrong.
inline std::string remade(const Model &from, Model &out, const std::map<std::string, double> &params) {
	out.params = from.params;
	for (const auto &[k, v] : params) {
		if (!from.params.count(k)) return "the model " + from.name + " has no parameter " + k + " (its `model` line's `params`)";
		out.params[k] = v;
	}
	out.parts.clear(), out.raw.clear(), out.closed = false;
	for (const std::vector<std::string> &line : from.raw)
		if (const std::string e = modelLine(out, line); !e.empty()) return "the model " + from.name + ", made as " + out.name + ": " + e;
	return out.closed ? std::string() : "the model " + from.name + " has no `end`";
}

// ---- drawing ----
constexpr int kMeshingVersion = 5;   // (raise it when the way a piece is meshed changes: the cache's old meshes are then made again)
// A piece's key with its tools' (a piece cut by a tool changes when the tool does).
inline std::string fullKeyOf(const Model &m, const Part &p, int depth = 0) {
	std::string key = keyOf(p);
	for (const Part::Boolean &b : p.booleans)
		for (const Part &q : m.parts)
			if (q.name == b.with && depth < 4) key += "|tool:" + fullKeyOf(m, q, depth + 1);
	return key;
}
inline std::string pieceName(const Model &m, const Part &p, int lod) {
	return "model_" + m.name + "_" + p.name + "_" + hashOf(fullKeyOf(m, p) + "#v" + std::to_string(kMeshingVersion)).substr(0, 12) + "_lod" + std::to_string(lod);
}
// The piece's mesh at level of detail `lod`: its own (partMesh), then its booleans with the model's tool pieces (model_boolean.h), then its budget - and at the far levels
// a quarter or a sixteenth of its triangles (model_decimate.h).
inline Mesh pieceMesh(const Model &m, const Part &p, int lod, int depth = 0) {
	Mesh mesh = partMesh(p, lod);
	if (!p.cloths.empty() && depth == 0) {   // (round 3) its sheets dropped onto the rest of the model
		std::vector<Mesh> others;
		std::vector<MeshField> fields;
		for (const Part &q : m.parts)
			if (&q != &p && !q.tool && q.cloths.empty()) others.push_back(pieceMesh(m, q, 2, depth + 1));
		for (const Mesh &o : others) {
			Vec3 lo, hi;
			boxOf(o, lo, hi);
			if (!o.tris.empty()) fields.push_back(meshField(o, std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 0.05}) / 60.0));
		}
		std::vector<const MeshField *> colliders;
		for (const MeshField &f : fields) colliders.push_back(&f);
		for (const Cloth &c : p.cloths) append(mesh, draped(c, colliders));
	}
	if (!p.booleans.empty() && depth < 4 && !mesh.tris.empty()) {
		Vec3 lo, hi;
		boxOf(mesh, lo, hi);
		const double cell = (p.cell > 0.0 ? p.cell : std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z}) / 64.0) * lod;
		for (const Part::Boolean &b : p.booleans)
			for (const Part &q : m.parts)
				if (q.name == b.with) mesh = combined(mesh, pieceMesh(m, q, lod, depth + 1), b.op, b.k, cell);
		mesh = p.hard ? autoSmooth(mesh, p.smoothAngle) : mesh;
	}
	std::size_t target = p.budget > 0 ? static_cast<std::size_t>(p.budget) : mesh.tris.size();
	if (lod > 1 && mesh.tris.size() > 2000) target = std::min(target, std::max<std::size_t>(500, mesh.tris.size() / static_cast<std::size_t>(lod * lod)));
	if (target < mesh.tris.size() && mesh.uvs.empty()) {
		mesh = decimated(mesh, target);
		if (p.hard) mesh = autoSmooth(mesh, p.smoothAngle);
	}
	return mesh;
}
// The piece's mesh with uvs for its paint (charts with `unwrap`, else box projection) and, when asked, each copied vertex's curvature.
inline Mesh uvMeshOf(const Part &p, const Mesh &mesh, std::vector<double> *curvature) {
	std::vector<int> from;
	if (p.unwrap) {
		Mesh w = welded(mesh, 1e-7);
		sdf::computeNormals(w);
		const Mesh uv = chartUnwrapped(mesh, &from);
		if (curvature) {
			const std::vector<double> k = curvatureOf(w);
			curvature->resize(from.size());
			for (std::size_t q = 0; q < from.size(); ++q) (*curvature)[q] = k[static_cast<std::size_t>(from[q])];
		}
		return uv;
	}
	const Mesh uv = boxUnwrapped(mesh, &from);
	if (curvature) {
		const std::vector<double> k = curvatureOf(mesh);
		curvature->resize(from.size());
		for (std::size_t q = 0; q < from.size(); ++q) (*curvature)[q] = k[static_cast<std::size_t>(from[q])];
	}
	return uv;
}
// The piece's regions (model_surface.h), each its own mesh in the cache and its own material: "" when it has none.
inline std::string regionsText(const std::string &film, const Model &m, const Part &p, int lod, const std::string &name) {
	if (p.regions.empty()) return std::string();
	std::string out;
	for (std::size_t k = 0; k <= p.regions.size(); ++k) {
		const std::string file = film_cache::ply(film, name + "_r" + std::to_string(k), [&]() { return splitByRegions(pieceMesh(m, p, lod), p.regions)[k]; });
		if (file.empty()) continue;
		Part look = p;
		look.paint.on = false;
		if (k > 0) {
			const Region &r = p.regions[k - 1];
			look.finish = r.finish, look.colour = r.colour, look.rough = r.rough, look.glow = r.glow;
		}
		out += attr(materialOf(look), film_cache::plyShape(file));
	}
	return out;
}
// An imported mesh's pictures on its own uvs: its colour, and (`pbr`) its roughness and its normal map.
inline std::string picturedMaterial(const Part &p, const std::string &name) {
	const auto found = [](const std::string &f) {
		const std::string a = film_cache::asset(f);
		return a.empty() ? f : a;
	};
	std::string mat = "Texture \"" + name + "_tex\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + found(p.texture) + "\" ]\n  ";
	if (!p.roughMap.empty()) mat += "Texture \"" + name + "_rough\" \"float\" \"imagemap\" \"string filename\" [ \"" + found(p.roughMap) + "\" ] \"string encoding\" \"linear\"\n  ";
	char buf[200];
	std::snprintf(buf, sizeof buf, " \"float roughness\" [ %.4f ]", p.rough);
	mat += "Material \"coateddiffuse\" \"texture reflectance\" \"" + name + "_tex\"" + (p.roughMap.empty() ? std::string(buf) : " \"texture roughness\" \"" + name + "_rough\"");
	if (!p.normalMap.empty()) mat += " \"string normalmap\" [ \"" + found(p.normalMap) + "\" ]";
	return mat;
}
// One piece as scene text at level `lod`, in the model's own space: its mesh from the cache, its painted picture and baked normal map (model_texture.h) or its own
// picture (`texture`), its material.
inline std::string pieceText(const std::string &film, const Model &m, const Part &p, int lod) {
	const std::string name = pieceName(m, p, lod);
	std::string extra;   // (round 3: its fur)
	if (p.fur.on) {
		Fur f = p.fur;
		f.density /= lod * lod;   // (fewer strands far off)
		extra = furText(pieceMesh(m, p, lod), f);
	}
	if (const std::string regions = regionsText(film, m, p, lod, name); !regions.empty()) return regions + extra;
	const std::string file = film_cache::ply(film, name, [&]() { return p.paint.on ? uvMeshOf(p, pieceMesh(m, p, lod), nullptr) : pieceMesh(m, p, lod); });
	if (file.empty()) return extra;
	if (!p.texture.empty() && !p.paint.on) return attr(picturedMaterial(p, name), film_cache::plyShape(file)) + extra;
	std::string picture, normals;
	if (p.paint.on) {
		const int N = std::clamp(p.paint.res, 64, 4096);
		const auto unwrapped = [&](std::vector<double> *curvature) { return uvMeshOf(p, pieceMesh(m, p, lod), curvature); };
		picture = film_sky::picture(film, name + "_paint", N, N, [&](std::vector<float> &rgb) {
			std::vector<double> kk;
			const Mesh uv = unwrapped(&kk);
			paintInto(rgb, uv, kk, p.paint);
		});
		if (p.paint.normals) {
			std::vector<Displace> bakes;
			for (const Displace &d : p.displaces)
				if (d.bake) bakes.push_back(d);
			normals = film_sky::picture(film, name + "_normals", N, N, [&](std::vector<float> &rgb) { normalsInto(rgb, N, unwrapped(nullptr), bakes); });
		}
	}
	std::string material = materialOf(p, name + "_tex", picture);
	if (!normals.empty()) material += " \"string normalmap\" [ \"" + normals + "\" ]";
	return attr(material, film_cache::plyShape(file)) + extra;
}
// The model's drawn pieces at level `lod`, as scene text (in the model's own space: placed by whatever transform is round it).
inline std::string modelText(const std::string &film, const Model &m, int lod) {
	std::string out;
	for (const Part &p : m.parts)
		if (!p.tool) out += pieceText(film, m, p, lod);
	return out;
}
// Which level of detail a placement `size` metres across, `distance` from the camera, is drawn at.
inline int lodFor(double size, double distance) {
	const double ratio = distance / std::max(0.05, size);
	return ratio < 25.0 ? 1 : ratio < 100.0 ? 2 : 4;
}
// A model's size across (its pieces' boxes at the coarsest level; measured once per model and kept).
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
		if (p.tool) continue;
		Vec3 a, b;
		if (!p.sdf.steps.empty() && !p.hasCage && p.sweeps.empty() && p.lofts.empty() && p.pieces.empty() && p.mods.empty()) p.sdf.bounds(a, b);
		else boxOf(partMesh(p, 4), a, b);
		lo = {std::min(lo.x, a.x), std::min(lo.y, a.y), std::min(lo.z, a.z)}, hi = {std::max(hi.x, b.x), std::max(hi.y, b.y), std::max(hi.z, b.z)};
	}
	const double size = lo.x > hi.x ? 1.0 : std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
	std::lock_guard<std::mutex> hold(lock);
	return known[key] = size;
}
// What moves a placed model now (the film's `move`, `turn`, `grow`, `show` and `shatter` lines, film_world.cpp DataFilm::modelsAt): by a placement's id, and by
// "ID.PIECE" for a piece of it.
struct Motion {
	std::function<bool(const std::string &)> moves;        // whether anything moves or hides the name (an id, or ID.PIECE)
	std::function<Mat4(const std::string &)> transform;    // a placement's (story space, the world's) or a piece's (the model's own space)
	std::function<bool(const std::string &)> shown;
	std::function<const Shatter *(const std::string &)> shatter;   // a placement's break, if it has one
	double t = 0.0;
	// (modeling round 3) shapes changing over time (model_anim.h), the models of the film (a morph's other), and what puts a placement somewhere else - a route, a
	// character carrying it, the rigid-body world (`frame` given the placement and its own frame: false when nothing does)
	const Animated *anim = nullptr;
	std::function<const Model *(const std::string &)> modelNamed;
	std::function<bool(const Placed &, double, Mat4 &)> frame;
	std::function<Mat4(const std::string &, double)> transformAt;   // (a placement's whole frame, story space, at another time: what lags behind it)
};
// Whether anything changes the placed model's shape or place now or later (it is written by itself then).
inline bool movesAtAll(const Model &m, const Placed &pl, const Motion &mo) {
	if (pl.id.empty() || !mo.moves) return false;
	bool moving = mo.moves(pl.id) || (mo.shatter && mo.shatter(pl.id)) || (mo.anim && mo.anim->names(pl.id));
	Mat4 unused;
	moving = moving || (mo.frame && mo.frame(pl, mo.t, unused));
	for (const Part &p : m.parts) {
		moving = moving || mo.moves(pl.id + "." + p.name) || (mo.anim && mo.anim->names(pl.id + "." + p.name));
		for (const Cloth &c : p.cloths) moving = moving || c.wave > 0.0;
	}
	return moving;
}
// A placed model that moves, hides a piece or breaks: written by itself, piece by piece (no instancing), at level `lod`.
inline std::string movingText(const std::string &film, const Model &m, const Placed &pl, const Mat4 &frame, int lod, const Motion &mo) {
	Mat4 placed = frame;
	if (mo.frame) mo.frame(pl, mo.t, placed);   // (a route, a character carrying it, the rigid-body world)
	const Mat4 base = mo.moves(pl.id) ? mul(mo.transform(pl.id), placed) : placed;
	std::string out;
	if (mo.anim)   // (round 3) melting into another model
		for (const Morph &mph : mo.anim->morphs) {
			if (mph.id != pl.id || mo.t <= mph.t0) continue;
			const Model *to = mo.modelNamed ? mo.modelNamed(mph.to) : nullptr;
			if (!to) break;
			const double u = std::clamp((mo.t - mph.t0) / std::max(1e-6, mph.t1 - mph.t0), 0.0, 1.0);
			if (u >= 1.0) return "AttributeBegin\n" + film_cache::placement(base) + modelText(film, *to, lod) + "AttributeEnd\n";
			const int step = static_cast<int>(std::lround(u * 24.0));   // (24 steps from one to the other, each kept in the cache)
			const auto whole = [&](const Model &x) {
				Mesh all;
				for (const Part &q : x.parts)
					if (!q.tool) append(all, pieceMesh(x, q, 2));
				return all;
			};
			const std::string file = film_cache::ply(film, "morph_" + m.name + "_" + to->name + "_" + hashOf(fullKeyOf(m, m.parts[0]) + fullKeyOf(*to, to->parts[0])).substr(0, 8) + "_" + std::to_string(step), [&]() {
				const Mesh a = whole(m), b = whole(*to);
				Vec3 lo, hi;
				boxOf(a, lo, hi);
				return morphed(a, b, step / 24.0, std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 0.05}) / 60.0);
			});
			Part look = m.parts[0];
			look.paint.on = false, look.colour = lerp(m.parts[0].colour, to->parts[0].colour, u);
			return file.empty() ? std::string() : attr(materialOf(look), film_cache::placement(base) + film_cache::plyShape(file));
		}
	if (const Shatter *s = mo.shatter ? mo.shatter(pl.id) : nullptr; s && mo.t >= s->t) {   // broken: its chunks where they have flown to
		const std::vector<Chunk> chunks = chunksOf(film, m.name, m.parts, *s, [&](const Part &p) { return pieceMesh(m, p, 1); });
		Vec3 lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};
		for (const Chunk &c : chunks) lo = {std::min(lo.x, c.centre.x), std::min(lo.y, c.centre.y), std::min(lo.z, c.centre.z)}, hi = {std::max(hi.x, c.centre.x), std::max(hi.y, c.centre.y), std::max(hi.z, c.centre.z)};
		const Vec3 mid = (lo + hi) * 0.5;
		for (std::size_t k = 0; k < chunks.size(); ++k)
			out += attr(materialOf(*chunks[k].part), film_cache::placement(mul(base, chunkAt(chunks[k], static_cast<int>(k), mid, *s, mo.t - s->t))) + film_cache::plyShape(chunks[k].file));
		return out;
	}
	int index = 0, drawn = 0;
	for (const Part &p : m.parts) drawn += p.tool ? 0 : 1;
	for (const Part &p : m.parts) {
		if (p.tool) continue;
		const std::string name = pl.id + "." + p.name;
		const int k = index++;
		if (mo.moves(name) && !mo.shown(name)) continue;
		Mat4 piece = mo.moves(name) ? mul(base, mo.transform(name)) : base;
		// (round 3) its shape over time: built up, flexed, wobbling, rippling, breaking up - written out as it is now
		bool reshaped = false;
		Mesh mesh;
		const auto meshNow = [&]() -> Mesh & {
			if (!reshaped) mesh = pieceMesh(m, p, lod), reshaped = true;
			return mesh;
		};
		if (mo.anim) {
			for (const Build &b : mo.anim->builds)
				if (b.id == pl.id) {
					Vec3 lo, hi;
					boxOf(pieceMesh(m, p, 4), lo, hi);
					piece = mul(piece, buildMatrix(b.style, std::clamp((mo.t - b.t0) / std::max(1e-6, b.t1 - b.t0), 0.0, 1.0), k, drawn, lo, hi));
				}
			if (!p.bones.empty() && std::any_of(mo.anim->poses.begin(), mo.anim->poses.end(), [&](const Pose &q) { return q.id == pl.id; })) {   // its own bones, as the film poses them
				std::vector<Mat4> own(p.bones.size(), identity());
				for (const Pose &q : mo.anim->poses)
					if (q.id == pl.id)
						for (std::size_t b = 0; b < p.bones.size(); ++b)
							if (p.bones[b].name == q.bone) own[b] = mul(own[b], translate(p.bones[b].a), rotate(q.axis, keyedAt(q.keys, mo.t)), translate(p.bones[b].a * -1.0));
				meshNow() = skinned(meshNow(), weightsOf(meshNow(), p.bones), chainedPoses(p.bones, own));
			}
			for (const Flex &f : mo.anim->flexes)
				if (f.name == name) meshNow() = flexed(meshNow(), f.kind, keyedAt(f.keys, mo.t));
			for (const Wobble &w : mo.anim->wobbles)
				if (w.name == name && mo.transformAt) {   // the lag of a damped spring behind the placement's motion over the last second and a half
					Vec3 lag{0, 0, 0}, vel{0, 0, 0};
					const double dt = 1.0 / 60.0;
					Vec3 prevPos = transformPoint(mo.transformAt(pl.id, mo.t - 1.5), Vec3{0, 0, 0}), prevVel{0, 0, 0};
					for (double u = mo.t - 1.5 + dt; u <= mo.t + 1e-9; u += dt) {
						const Vec3 pos = transformPoint(mo.transformAt(pl.id, u), Vec3{0, 0, 0}), v = (pos - prevPos) * (1.0 / dt), acc = (v - prevVel) * (1.0 / dt);
						vel = vel + (acc * -0.04 * w.amount - lag * 40.0 - vel * 4.0) * dt;
						lag = lag + vel * dt;
						prevPos = pos, prevVel = v;
					}
					const Vec3 local = transformPoint(invertAffine(base), transformPoint(base, Vec3{0, 0, 0}) + lag) - Vec3{0, 0, 0};
					meshNow() = wobbled(meshNow(), local);
				}
			for (const Vanish &v : mo.anim->vanishes)
				if (v.id == pl.id && mo.t > v.t0) meshNow() = vanished(meshNow(), std::clamp((mo.t - v.t0) / std::max(1e-6, v.t1 - v.t0), 0.0, 1.0), k + 1);
		}
		for (const Cloth &c : p.cloths)
			if (c.wave > 0.0) meshNow() = rippled(meshNow(), c.wave, mo.t);
		if (reshaped) {
			Part look = p;
			look.paint.on = false;
			if (!mesh.tris.empty()) out += attr(!p.texture.empty() && !p.paint.on ? picturedMaterial(p, pieceName(m, p, lod) + "_now") : materialOf(look), emitMesh(mesh, piece));
			continue;
		}
		out += "AttributeBegin\n" + film_cache::placement(piece) + pieceText(film, m, p, lod) + "AttributeEnd\n";
	}
	return out;
}
// Every placed model in a frame seen from pbrt's `eye`: one placed once is written where it stands; one placed more than once is written once (per level of detail used)
// as an object and instanced; one that moves, hides a piece or breaks (`motion`) is written by itself. A placement of a model with variants takes one of them.
inline std::string placedText(const std::string &film, const std::vector<Model> &models, const std::vector<Placed> &placed, const Vec3 &eye, const Motion &motion = {}) {
	if (placed.empty()) return std::string();
	std::map<std::string, const Model *> byName;
	for (const Model &m : models) byName[m.name] = &m;
	std::map<std::pair<std::string, int>, std::vector<Mat4>> groups;
	std::string out = "# models (film_model.h)\n", defs;
	for (std::size_t n = 0; n < placed.size(); ++n) {
		const Placed &pl = placed[n];
		auto it = byName.find(pl.model);
		if (it == byName.end()) continue;
		if (!it->second->variants.empty()) {   // (one of its variants, by the placement)
			const std::vector<std::string> &v = it->second->variants;
			it = byName.find(v[static_cast<std::size_t>(hash01(static_cast<int>(n) * 7919 + 17, 3) * v.size()) % v.size()]);
			if (it == byName.end()) continue;
		}
		const Model &m = *it->second;
		const double size = sizeOf(film, m) * pl.scale;
		const int lod = lodFor(size, length(pl.at - eye));
		const Mat4 frame = mul(translate({-pl.at.x, pl.at.y, pl.at.z}), rotate({0, 1, 0}, pl.turn), scale(pl.scale));
		if (movesAtAll(m, pl, motion)) {
			if (!motion.moves(pl.id) || motion.shown(pl.id)) out += movingText(film, m, pl, frame, lod, motion);
			continue;
		}
		groups[{m.name, lod}].push_back(frame);
	}
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

// ---- covering a piece with another model (round 3) ----
// The pieces that cover `c.piece` of `m` with copies of `child`: points strewn over the piece by area (seeded), kept by the rule - `up` where it faces up, `slope D`
// where it is within D degrees of level, `all` everywhere - each copy stood on the surface along its normal, turned at random about it and sized between s0 and s1.
// One piece for each of the child's pieces, its copies merged into one mesh (at most about two million triangles in all).
inline std::vector<Part> coveredParts(const Model &m, const Model::Cover &c, const Model &child) {
	std::vector<Part> out;
	const auto target = std::find_if(m.parts.begin(), m.parts.end(), [&](const Part &p) { return p.name == c.piece; });
	if (target == m.parts.end()) return out;
	const Mesh surface = pieceMesh(m, *target, 1);
	double area = 0.0;
	std::vector<double> run;
	for (const auto &t : surface.tris) area += 0.5 * length(cross(surface.verts[t[1]] - surface.verts[t[0]], surface.verts[t[2]] - surface.verts[t[0]])), run.push_back(area);
	if (area <= 0.0) return out;
	std::vector<Mat4> places;
	const double minUp = c.rule == "up" ? 0.6 : c.rule == "slope" ? std::cos(c.slope * kPi / 180.0) : -2.0;
	for (int k = 0; k < 40 * c.count && static_cast<int>(places.size()) < c.count; ++k) {
		const int key = c.seed * 7919 + k;
		const std::size_t ti = std::min(surface.tris.size() - 1, static_cast<std::size_t>(std::lower_bound(run.begin(), run.end(), hash01(key, 1) * area) - run.begin()));
		const auto &t = surface.tris[ti];
		const Vec3 n = normalize(cross(surface.verts[t[1]] - surface.verts[t[0]], surface.verts[t[2]] - surface.verts[t[0]]));
		if (n.y < minUp) continue;
		double a = hash01(key, 2), b = hash01(key, 3);
		if (a + b > 1.0) a = 1.0 - a, b = 1.0 - b;
		const Vec3 p = surface.verts[t[0]] + (surface.verts[t[1]] - surface.verts[t[0]]) * a + (surface.verts[t[2]] - surface.verts[t[0]]) * b;
		places.push_back(mul(translate(p), alignY({0, 0, 0}, n), rotate({0, 1, 0}, 360.0 * hash01(key, 4)), scale(lerp(c.s0, c.s1, hash01(key, 5)))));
	}
	std::size_t budget = 2000000;
	for (const Part &q : child.parts) {
		if (q.tool) continue;
		const Mesh one = pieceMesh(child, q, places.size() > 200 ? 2 : 1);
		Part copy = q;
		copy.name = c.piece + ".cover." + child.name + "." + q.name;
		copy.sdf.steps.clear(), copy.hasCage = false, copy.sweeps.clear(), copy.lofts.clear(), copy.mods.clear(), copy.displaces.clear(), copy.booleans.clear(), copy.cloths.clear();
		copy.paint.on = false, copy.fur.on = false, copy.regions.clear();
		Mesh all;
		for (const Mat4 &pl : places) {
			if (all.tris.size() + one.tris.size() > budget) break;
			append(all, transformed(one, pl));
		}
		budget -= std::min(budget, all.tris.size());
		copy.pieces = {all};
		copy.sources.push_back("cover:" + hashOf(keyOf(*target)) + ":" + std::to_string(c.count) + ":" + std::to_string(c.seed) + ":" + c.rule + ":" + std::to_string(c.slope) + ":" + std::to_string(c.s0) + ":" + std::to_string(c.s1) + ":" + hashOf(keyOf(q)));
		out.push_back(copy);
	}
	return out;
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
