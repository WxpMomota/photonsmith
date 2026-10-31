#pragma once
// film_model_kind.h -- modeling round 2, part 2: a model of the film as a character (`character NAME MODEL at X Y Z ...`, its kind the model's name). Its rig comes from
// its pieces' names - a model made for it, or `make creature`, names them so:
//
//   head, and eyes, pupils, mouth      the head turns to what it looks at and nods; the eyes blink (squashed shut) and their pupils follow the look; the mouth opens as it talks
//   leg.1 .. leg.N (or leg.l, leg.r)   swing from their tops as it walks, each pair in turn; with no legs it hops along, squashing as it lands
//   arm.l, arm.r                       swing as it walks and act its gestures (wave, point, cheer, shrug: the sea creatures' library, film_sea_kinds.h)
//   tail                               wags; anything else is its body, which bobs, breathes and leans
//
// A piece's shape keys (`key NAME` then modifiers, film_model.h) are blended in by what it feels and says: smile (or happy) by a smile, sad by its opposite, open (or talk)
// by its mouth opening, surprised by wide eyes, angry by a frown, blink by its blinks. The pieces that move rigidly are kept in the film's cache and placed each frame;
// a piece with a key in use is written out with its keys blended. `character ... body red` colours the piece called body, as it colours a built-in kind's part.

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "film_model.h"
#include "film_sea_kinds.h"

namespace cartoon {
namespace model {

struct RigPiece {
	const Part *part = nullptr;
	std::string role;   // body, head, eye, pupil, mouth, leg, arm.l, arm.r, tail
	int index = 0;      // (a leg's number from 0)
	Vec3 lo, hi, pivot;
	Mesh mesh;                        // (at level 1, for its keys)
	std::vector<Mesh> keyed;          // (each key's mesh: the same vertices, moved)
	std::string file;                 // (its PLY in the cache)
};
struct Rig {
	std::vector<RigPiece> pieces;
	Vec3 lo, hi, headCentre, headBase;
	bool hasLegs = false, hasHead = false;
};
inline std::string roleOf(const std::string &name) {
	const auto starts = [&](const char *p) { return name.rfind(p, 0) == 0; };
	if (name == "head" || starts("head.")) return "head";
	if (name == "eyes" || starts("eye")) return "eye";
	if (name == "pupils" || starts("pupil")) return "pupil";
	if (name == "mouth" || starts("mouth")) return "mouth";
	if (starts("leg")) return "leg";
	if (name == "arm.l" || name == "arm.left") return "arm.l";
	if (name == "arm.r" || name == "arm.right") return "arm.r";
	if (starts("tail")) return "tail";
	return "body";
}
// The model's rig: its drawn pieces measured, their pivots found (a leg's and an arm's at its top, the head's at its base, a tail's at the end nearest the body).
inline Rig rigOf(const std::string &film, const Model &m) {
	Rig r;
	r.lo = {1e30, 1e30, 1e30}, r.hi = {-1e30, -1e30, -1e30};
	int legs = 0;
	for (const Part &p : m.parts) {
		if (p.tool) continue;
		RigPiece rp;
		rp.part = &p, rp.role = roleOf(p.name);
		rp.mesh = pieceMesh(m, p, 1);
		if (rp.mesh.tris.empty()) continue;
		for (const Part::ShapeKey &k : p.keys) {
			Mesh km = rp.mesh;
			for (const Modifier &mod : k.mods) km = applied(km, mod);
			rp.keyed.push_back(km.verts.size() == rp.mesh.verts.size() ? km : rp.mesh);   // (a key that changed the vertices' number cannot blend: left out)
		}
		boxOf(rp.mesh, rp.lo, rp.hi);
		r.lo = {std::min(r.lo.x, rp.lo.x), std::min(r.lo.y, rp.lo.y), std::min(r.lo.z, rp.lo.z)}, r.hi = {std::max(r.hi.x, rp.hi.x), std::max(r.hi.y, rp.hi.y), std::max(r.hi.z, rp.hi.z)};
		if (rp.role == "leg") rp.index = legs++;
		rp.file = film_cache::ply(film, pieceName(m, p, 1) + "_rig", [&]() { return rp.mesh; });
		r.pieces.push_back(rp);
	}
	r.hasLegs = legs > 0;
	for (RigPiece &rp : r.pieces) {
		const Vec3 c = (rp.lo + rp.hi) * 0.5;
		if (rp.role == "leg" || rp.role == "arm.l" || rp.role == "arm.r") rp.pivot = {c.x, rp.hi.y - 0.15 * (rp.hi.y - rp.lo.y), c.z};
		else if (rp.role == "tail") rp.pivot = {c.x, c.y, rp.hi.z};
		else rp.pivot = c;
		if (rp.role == "head") r.headCentre = c, r.headBase = {c.x, rp.lo.y, c.z}, r.hasHead = true;
	}
	if (!r.hasHead) r.headCentre = r.headBase = {0.5 * (r.lo.x + r.hi.x), r.lo.y + 0.85 * (r.hi.y - r.lo.y), 0.5 * (r.lo.z + r.hi.z)};
	return r;
}
inline Mat4 about(Vec3 pivot, const Mat4 &m) { return mul(translate(pivot), m, translate(pivot * -1.0)); }

// The kind of character made from model `m` of film `film`.
inline film_file::CharacterKind kindOf(const std::string &film, const Model &m) {
	auto rig = std::make_shared<std::shared_ptr<const Rig>>();   // (measured the first time it is drawn)
	auto lock = std::make_shared<std::mutex>();
	auto model = std::make_shared<Model>(m);
	const auto rigNow = [rig, lock, model, film]() {
		const std::lock_guard<std::mutex> hold(*lock);
		if (!*rig) *rig = std::make_shared<const Rig>(rigOf(film, *model));
		return *rig;
	};
	// the kind's measures, from its pieces' boxes (cheap: the coarsest level)
	Vec3 lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30}, head{0, 0, 0};
	bool hasHead = false;
	for (const Part &p : m.parts) {
		if (p.tool) continue;
		Vec3 a, b;
		boxOf(pieceMesh(m, p, 4), a, b);
		lo = {std::min(lo.x, a.x), std::min(lo.y, a.y), std::min(lo.z, a.z)}, hi = {std::max(hi.x, b.x), std::max(hi.y, b.y), std::max(hi.z, b.z)};
		if (roleOf(p.name) == "head") head = (a + b) * 0.5, hasHead = true;
	}
	const double h = std::max(0.05, hi.y - std::min(0.0, lo.y));
	film_file::CharacterKind k;
	k.height = h, k.radius = std::max(0.1, 0.5 * std::max(hi.x - lo.x, hi.z - lo.z));
	k.chest = {0.0, 0.6 * h, 0.0};
	k.head = hasHead ? head : Vec3{0.0, 0.9 * h, 0.0};
	k.carry = {0.3 * k.radius, 0.55 * h, 0.5 * k.radius};
	k.stride = 0.45 * h, k.firstStep = 0.5 * k.stride;
	k.scalable = true;
	k.poses = film_kinds::namesOf(film_kinds::sea::poses());
	k.voice = {std::clamp(260.0 / std::sqrt(h), 120.0, 420.0), 1.15, 0.06};
	for (const Part &p : m.parts)
		if (!p.tool) k.parts.push_back(p.name);
	k.text = [rigNow, film](const film_file::CharacterMoment &mo) {
		const std::shared_ptr<const Rig> r = rigNow();
		const film_kinds::sea::SeaPose p = film_kinds::sea::detail::posed(mo, r->headCentre);
		const double H = std::max(0.05, r->hi.y - r->lo.y), phase = kPi * mo.steps;
		// the body: a bob as it walks (a hop with no legs, squashing as it lands), breathing, a lean into its walk
		const double hop = r->hasLegs ? 0.02 * H * std::abs(std::sin(phase)) * mo.going : 0.12 * H * std::abs(std::sin(phase)) * mo.going;
		const double squash = r->hasLegs ? 1.0 : 1.0 - 0.12 * mo.going * (1.0 - std::abs(std::sin(phase)));
		const double breath = mo.idle ? 0.008 * std::sin(2.0 * kPi * mo.t / 3.6 + mo.seed) : 0.0;
		const Mat4 bodyM = mul(translate({0, hop, 0}), rotate({1, 0, 0}, 4.0 * mo.going), rotate({0, 0, 1}, p.tilt * 0.5), scale(1.0 + breath, squash * (1.0 + breath), 1.0 + breath));
		// the head: to what it looks at, nodding and tilting
		double yaw = 0.0, pitch = 0.0;
		if (p.lookWeight > 0.0) {
			const Vec3 target = transformPoint(invertAffine(mul(mo.frame, scale(mo.scale))), p.look) - r->headCentre;
			yaw = std::clamp(std::atan2(target.x, target.z) * 180.0 / kPi, -60.0, 60.0) * std::min(1.0, p.lookWeight);
			pitch = std::clamp(-std::atan2(target.y, std::hypot(target.x, target.z)) * 180.0 / kPi, -30.0, 30.0) * std::min(1.0, p.lookWeight);
		}
		const Mat4 headM = about(r->headBase, mul(rotate({0, 1, 0}, yaw), rotate({1, 0, 0}, pitch + p.nod), rotate({0, 0, 1}, p.tilt)));
		const Mat4 place = mul(mo.frame, scale(mo.scale));
		std::string out;
		for (const RigPiece &rp : r->pieces) {
			Mat4 local = identity();
			const double swing = 28.0 * mo.going * std::sin(phase + (rp.index % 2 ? kPi : 0.0) + (rp.index / 2) * 0.5 * kPi);
			if (rp.role == "leg") local = about(rp.pivot, rotate({1, 0, 0}, swing));
			else if (rp.role == "arm.l" || rp.role == "arm.r") {
				const double sideSign = rp.role == "arm.l" ? 1.0 : -1.0, walk = -20.0 * mo.going * std::sin(phase) * sideSign;
				const double raise = 150.0 * (rp.role == "arm.l" ? p.raiseL : p.raiseR) + 45.0 * p.spread, wave = rp.role == "arm.r" ? 25.0 * p.waveR : 0.0;
				const double ahead = rp.role == "arm.r" ? -80.0 * p.forwardR : 0.0;
				local = about(rp.pivot, mul(rotate({0, 0, 1}, sideSign * (raise + wave)), rotate({1, 0, 0}, walk + ahead)));
			} else if (rp.role == "tail") local = about(rp.pivot, rotate({0, 1, 0}, 18.0 * std::sin(2.0 * kPi * mo.t * (1.0 + mo.going))));
			else if (rp.role == "head" || rp.role == "eye" || rp.role == "pupil" || rp.role == "mouth") {
				local = headM;
				const Vec3 c = (rp.lo + rp.hi) * 0.5;
				if (rp.role == "eye" || rp.role == "pupil") {
					const double shut = std::clamp(std::max(p.blink, p.lids), 0.0, 0.92);
					local = mul(local, about(c, scale(1.0, 1.0 - shut, 1.0)));
					if (rp.role == "pupil" && p.lookWeight > 0.0) local = mul(local, translate({0.01 * H * std::sin(yaw * kPi / 180.0), -0.01 * H * std::sin(pitch * kPi / 180.0), 0}));
				}
				if (rp.role == "mouth") local = mul(local, about(c, scale(1.0 + 0.3 * std::max(0.0, p.smile), 1.0 + 3.0 * p.mouthOpen, 1.0)));
			}
			const Mat4 m = mul(place, bodyM, local);
			Vec3 colour = rp.part->colour;
			film_kinds::colourFrom(mo, rp.part->name.c_str(), colour);
			Part tinted = *rp.part;
			tinted.colour = colour;
			// its keys, by what it feels and says now
			std::vector<double> w(rp.keyed.size(), 0.0);
			bool keyed = false;
			for (std::size_t q = 0; q < rp.keyed.size(); ++q) {
				const std::string &n = rp.part->keys[q].name;
				w[q] = n == "smile" || n == "happy" ? std::max(0.0, p.smile) : n == "sad" ? std::max(0.0, -p.smile) : n == "open" || n == "talk" ? p.mouthOpen
					 : n == "surprised"										   ? p.eyeWide
					 : n == "angry"												   ? std::max(0.0, p.browTilt)
					 : n == "blink"												   ? p.blink
																				   : 0.0;
				w[q] = std::clamp(w[q], 0.0, 1.0);
				keyed = keyed || w[q] > 0.01;
			}
			if (!keyed && !rp.file.empty()) {
				out += attr(materialOf(tinted), film_cache::placement(m) + film_cache::plyShape(rp.file));
				continue;
			}
			Mesh blended = rp.mesh;
			for (std::size_t q = 0; q < rp.keyed.size(); ++q)
				if (w[q] > 0.01)
					for (std::size_t v = 0; v < blended.verts.size(); ++v) blended.verts[v] = blended.verts[v] + (rp.keyed[q].verts[v] - rp.mesh.verts[v]) * w[q];
			sdf::computeNormals(blended);
			out += attr(materialOf(tinted), emitMesh(blended, m));
		}
		return out;
	};
	return k;
}

}  // namespace model
}  // namespace cartoon
