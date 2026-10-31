#pragma once
// model_part.h -- a part of a model (film_model.h): what it is made of, how it is turned into a mesh, and its material.
//
// A part gathers any of: primitives combined as a signed distance field (model_sdf.h), a subdivision cage (model_subdiv.h), sweeps and lofts (model_curves.h) and kit pieces
// (model_kit.h). They are meshed and put together, then go through the part's modifiers in order (model_modifiers.h; `displace` among them, model_detail.h); a `hard` part
// is meshed by dual contouring and gets split normals. Its material: plastic (coated, the default), matte, metal, glass or glow, a colour and a roughness - or `paint`
// (model_texture.h), a picture baked for it.
//
// Levels of detail (film_model.h): `lod` 1 is the part as written; 2 and 4 mesh its fields on cells that much bigger and subdivide its cage a level (or two) less - what a
// model far from the camera is drawn with.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "model_curves.h"
#include "model_detail.h"
#include "model_modifiers.h"
#include "model_sdf.h"
#include "model_subdiv.h"
#include "model_texture.h"

namespace cartoon {
namespace model {

struct Part {
	std::string name = "main";
	std::string finish = "plastic";   // plastic, matte, metal, glass, glow
	Vec3 colour{0.8, 0.8, 0.8};
	double rough = 0.35, glow = 0.0;
	Sdf sdf;
	bool hard = false;
	double smoothAngle = 35.0, cell = 0.0;   // (cell 0: a fiftieth of the field's size, an eightieth for a hard piece)
	bool hasCage = false;
	Cage cage;
	int levels = 2;
	std::vector<Sweep> sweeps;
	std::vector<Loft> lofts;
	std::vector<Mesh> pieces;   // (kit pieces, already meshed in the part's space)
	std::vector<Modifier> mods;
	std::vector<Displace> displaces;   // (each at its place among the modifiers: a modifier of kind "displace" with v[0] its index here)
	Paint paint;
	bool empty() const { return sdf.steps.empty() && !hasCage && sweeps.empty() && lofts.empty() && pieces.empty(); }
};

// A text that changes whenever anything that shapes or paints the part does (the cache's key).
inline std::string keyOf(const Part &p) {
	std::ostringstream o;
	o.precision(9);
	o << p.name << '|' << p.finish << p.colour.x << ',' << p.colour.y << ',' << p.colour.z << '|' << p.rough << '|' << p.glow << '|' << p.hard << p.smoothAngle << '|' << p.cell << '|' << p.sdf.round << ',' << p.sdf.shell;
	for (const Step &s : p.sdf.steps)
		o << "|s" << int(s.op) << ',' << s.k << ',' << s.prim.kind << ',' << s.prim.at.x << ',' << s.prim.at.y << ',' << s.prim.at.z << ',' << s.prim.size.x << ',' << s.prim.size.y << ','
		  << s.prim.size.z << ',' << s.prim.to.x << ',' << s.prim.to.y << ',' << s.prim.to.z << ',' << s.prim.turn.x << ',' << s.prim.turn.y << ',' << s.prim.turn.z << ',' << s.prim.round;
	if (p.hasCage) {
		o << "|c" << p.levels;
		for (const Vec3 &v : p.cage.verts) o << ',' << v.x << ',' << v.y << ',' << v.z;
		for (const auto &f : p.cage.faces) {
			o << ';';
			for (int i : f) o << i << ',';
		}
		for (const auto &[e, s] : p.cage.creases) o << '~' << e.first << ',' << e.second << ',' << s;
		for (int c : p.cage.corners) o << '^' << c;
	}
	for (const Sweep &s : p.sweeps) {
		o << "|w" << s.twist << ',' << s.taper << ',' << s.loop;
		for (const auto &[x, y] : s.profile) o << ',' << x << ',' << y;
		for (const Vec3 &q : s.path) o << ';' << q.x << ',' << q.y << ',' << q.z;
	}
	for (const Loft &l : p.lofts) {
		o << "|l" << l.sides << l.square << l.capTop << l.capBottom;
		for (const Ring &r : l.rings) o << ';' << r.y << ',' << r.rx << ',' << r.rz << ',' << r.x << ',' << r.z;
	}
	for (const Mesh &m : p.pieces) o << "|k" << m.verts.size() << ',' << m.tris.size() << ',' << (m.verts.empty() ? 0.0 : m.verts[0].x + 3.0 * m.verts.back().y);
	for (const Modifier &m : p.mods) {
		o << "|m" << m.kind << m.axis;
		for (double v : m.v) o << ',' << v;
	}
	for (const Displace &d : p.displaces) o << "|d" << d.kind << d.amount << ',' << d.size << ',' << d.seed;
	if (p.paint.on) {
		const Paint &q = p.paint;
		o << "|p" << q.base.x << q.base.y << q.base.z << q.wear.x << q.wear.y << q.wear.z << q.dirt.x << q.dirt.y << q.dirt.z << q.stripe.x << q.stripe.y << q.stripe.z << q.spot.x << q.spot.y
		  << q.spot.z << ',' << q.wearAmount << ',' << q.dirtAmount << ',' << q.mottle << ',' << q.stripeWidth << q.stripeAxis << ',' << q.spotAmount << ',' << q.res << ',' << q.seed;
	}
	return o.str();
}
inline std::string hashOf(const std::string &text) {
	unsigned long long h = 1469598103934665603ULL;
	for (const unsigned char c : text) h = (h ^ c) * 1099511628211ULL;
	char buf[20];
	std::snprintf(buf, sizeof buf, "%016llx", h);
	return buf;
}

// The part as a mesh at level of detail `lod` (1, 2 or 4), in the model's space.
inline Mesh partMesh(const Part &p, int lod = 1) {
	Mesh m;
	if (!p.sdf.steps.empty()) {
		Sdf f = p.sdf;
		f.prepare();
		Vec3 lo, hi;
		f.bounds(lo, hi);
		const double size = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
		double thinnest = 1e30;   // (the thinnest shape added: at least four cubes across it, unless that would be finer than a four-hundredth of the piece)
		for (const Step &st : p.sdf.steps)
			if (st.op == Op::Add || st.op == Op::Blend) thinnest = std::min(thinnest, thinnestOf(st.prim));
		const double cell0 = p.cell > 0.0 ? p.cell : std::max(size / 400.0, std::min(size / (p.hard ? 80.0 : 50.0), 0.25 * thinnest));
		// (soft or hard: dual contouring keeps a closed surface; `hard` only splits its normals at the edges. A far level whose cells are thicker than the piece - a roof
		// slab, a mast - would mesh to nothing: it takes the next finer level instead)
		for (int l = lod;; l /= 2) {
			const double cell = cell0 * l;
			const Mesh mesh = dualContour(f, lo - Vec3{2 * cell, 2 * cell, 2 * cell}, hi + Vec3{2 * cell, 2 * cell, 2 * cell}, cell);
			if (!mesh.tris.empty() || l <= 1) {
				append(m, mesh);
				break;
			}
		}
	}
	if (p.hasCage) append(m, subdivide(p.cage, std::max(0, p.levels - (lod >= 4 ? 2 : lod >= 2 ? 1 : 0))));
	for (const Sweep &s : p.sweeps) append(m, sweepMesh(s));
	for (const Loft &l : p.lofts) append(m, loftMesh(l));
	for (const Mesh &k : p.pieces) append(m, k);
	for (const Modifier &mod : p.mods) {
		if (mod.kind == "displace") {
			const std::size_t i = static_cast<std::size_t>(mod.v.empty() ? 0 : mod.v[0]);
			if (i < p.displaces.size()) {
				m = displaced(m, p.displaces[i]);
			}
			continue;
		}
		m = applied(m, mod);
	}
	if (p.hard) m = autoSmooth(m, p.smoothAngle);
	else if (m.normals.size() != m.verts.size()) sdf::computeNormals(m);
	return m;
}

// The part's material as pbrt text (its paint's picture, when it has one, at `picture`).
inline std::string materialOf(const Part &p, const std::string &texName = std::string(), const std::string &picture = std::string()) {
	char buf[400];
	if (p.paint.on && !picture.empty()) {
		std::snprintf(buf, sizeof buf, "Texture \"%s\" \"spectrum\" \"imagemap\" \"string filename\" [ \"%s\" ]\n  Material \"coateddiffuse\" \"texture reflectance\" \"%s\" \"float roughness\" [ %.4f ]",
					  texName.c_str(), picture.c_str(), texName.c_str(), p.rough);
		return buf;
	}
	if (p.finish == "matte") return diffuseMaterial(p.colour);
	if (p.finish == "metal") return metalMaterial(p.colour, p.rough);
	if (p.finish == "glass") return glassMaterial(1.5, std::min(p.rough, 0.3));
	if (p.finish == "glow") {
		const Vec3 L = p.colour * std::max(0.1, p.glow);
		std::snprintf(buf, sizeof buf, "AreaLightSource \"diffuse\" \"rgb L\" [ %.4f %.4f %.4f ]\n  Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", L.x, L.y, L.z);
		return buf;
	}
	return coated(p.colour, p.rough);
}

}  // namespace model
}  // namespace cartoon
