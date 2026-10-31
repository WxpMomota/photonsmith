#pragma once
// film_vignettes.h -- round 17: little comic moments of sea life, each with a character of its own.
//
//   clownfish X Y Z [count N] [seed K]        an anemone on the seabed with N clownfish (1 to 4) darting round it, in among its swaying tentacles; when anyone comes
//                                             within a metre and a half - the camera, a diver, a character - they dive in and hide, peeking out
//   pufferfish X Y Z [at T]... [seed K]       a pufferfish pottering in a small circle that blows itself up into a spiky ball when someone comes within a metre, or at T,
//                                             holds it for four seconds, and goes down again
//   flounder X Z [at T] [seed K]              a flounder lying flat in the sand, sand-coloured, only its two eyes showing and swivelling; at T it bursts off the bottom in a
//                                             puff of silt, flaps two metres away and settles again
//   moray X Y Z [toward A] [at T]... [seed K] a moray eel in a hole in a rock, its head out, its mouth opening and closing as it breathes; at each T it lunges out half
//                                             its length, jaws wide, and pulls back
//   cleanershrimp NAME [from T0] [to T1]      a cleaner shrimp, red and white, walking about on character NAME's hand, its feelers waving
//   dugong X Z [radius R] [seed K]            a dugong cruising round a circle of radius R over a seagrass meadow, every nine seconds nosing down to crop the grass in a puff of silt
//
// Heights above the seabed, as the other sea lines (film_parse_sea.h); pbrt's x z. Meshes in story space (x mirrored) for emitMesh.

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "film_deepsea.h"
#include "film_sealife.h"
#include "film_undersea.h"
#include "nature_lib.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace vignettes {

struct Clownfish {
	Vec3 at;
	int count = 2, seed = 1;
};
struct Puffer {
	Vec3 at;
	std::vector<double> at_;   // (the times it blows up of itself)
	int seed = 1;
};
struct Flounder {
	double x = 0.0, z = 0.0;
	double at = 1e30;
	int seed = 1;
};
struct Moray {
	Vec3 at;
	double toward = 0.0;
	std::vector<double> lunges;
	int seed = 1;
};
struct Shrimp {
	std::string on;
	double t0 = 0.0, t1 = 1e30;
};
struct Dugong {
	Vec3 at;
	double radius = 6.0;
	int seed = 1;
};
struct World {
	std::vector<Clownfish> clownfish;
	std::vector<Puffer> puffers;
	std::vector<Flounder> flounders;
	std::vector<Moray> morays;
	std::vector<Shrimp> shrimps;
	std::vector<Dugong> dugongs;
	bool any() const { return !clownfish.empty() || !puffers.empty() || !flounders.empty() || !morays.empty() || !shrimps.empty() || !dugongs.empty(); }
};

namespace detail {
using deepsea::detail::add;
using sealife::detail::facingOf;
using sealife::detail::story;
inline Mesh ellipsoid(Vec3 c, Vec3 r, int nu = 16, int nv = 10) {
	Mesh m;
	add(m, uvSphere(1.0, nu, nv), mul(translate(c), scale(r.x, r.y, r.z)));
	return m;
}
inline Mat4 placed(Vec3 at, Vec3 fwd, double s = 1.0) { return mul(translate(story(at)), facingOf(story(fwd)), scale(s)); }
// How near the nearest of `others` is to `p`.
inline double nearest(const Vec3 &p, const std::vector<Vec3> &others) {
	double d = 1e30;
	for (const Vec3 &o : others) d = std::min(d, length(o - p));
	return d;
}
// A little cartoon fish (forward +z, length L): body, tail, eyes - into `body` and `dark`, with white bands into `bands` (a clownfish's).
inline void littleFish(Mesh &body, Mesh &bands, Mesh &dark, double L, double wag, int stripes) {
	add(body, ellipsoid({0.0, 0.0, 0.0}, {0.22 * L, 0.3 * L, 0.5 * L}));
	sealife::detail::fin(body, {0.0, 0.0, -0.45 * L}, {0.3 * L * std::sin(wag), 0.25 * L, -0.85 * L}, {0.3 * L * std::sin(wag), -0.25 * L, -0.85 * L});
	body.uvs.clear();   // (the fin has none: a mesh's uvs are all or nothing)
	for (int s = 0; s < stripes; ++s) {
		const double z = (0.25 - 0.32 * s) * L, r = 0.5 * L * std::sqrt(std::max(0.05, 1.0 - (z / (0.5 * L)) * (z / (0.5 * L))));
		add(bands, ellipsoid({0.0, 0.0, z}, {0.235 * L * r / (0.5 * L), 0.315 * L * r / (0.5 * L), 0.05 * L}, 14, 6));
	}
	for (const double side : {1.0, -1.0}) add(dark, ellipsoid({side * 0.17 * L, 0.08 * L, 0.3 * L}, {0.06 * L, 0.06 * L, 0.06 * L}, 8, 6));
}
}  // namespace detail

// ---- clownfish in their anemone ----
inline std::string clownfishText(const Clownfish &c, double t, const std::function<double(double, double)> &bedAt, const std::vector<Vec3> &others) {
	using namespace detail;
	const Vec3 foot{c.at.x, bedAt(c.at.x, c.at.z), c.at.z};
	std::string out = "# clownfish in an anemone (film_vignettes.h)\n";
	Mesh tentacles, base, body, bands, dark;
	add(base, ellipsoid(story(foot + Vec3{0.0, 0.05, 0.0}), {0.16, 0.07, 0.16}));
	for (int i = 0; i < 60; ++i) {   // the anemone: a cushion of fat tentacles swaying together
		const double a = 2.0 * kPi * hash01(i, c.seed + 1), r = 0.15 * std::sqrt(hash01(i, c.seed + 2)), sway = 0.05 * std::sin(1.3 * t + 0.4 * r * 20.0);
		const Vec3 root = foot + Vec3{r * std::cos(a), 0.08, r * std::sin(a)};
		add(tentacles, sdf::tubeMesh({story(root), story(root + Vec3{sway + 0.03 * std::cos(a), 0.12, 0.03 * std::sin(a)}), story(root + Vec3{2.0 * sway + 0.05 * std::cos(a), 0.2, 0.05 * std::sin(a)})}, 0.012, 0.55, 6));
	}
	const double scare = smoothstep(1.6, 1.2, nearest(foot + Vec3{0.0, 0.2, 0.0}, others));   // (they hide as someone comes near)
	for (int f = 0; f < std::clamp(c.count, 1, 4); ++f) {
		const double ph = 2.0 * kPi * hash01(f, c.seed + 5), w = 0.9 + 0.6 * hash01(f, c.seed + 6);
		const double a = w * t + ph + 0.6 * std::sin(1.7 * t + f);   // (darting: a circle that hurries and hesitates)
		const double R = (0.22 + 0.1 * std::sin(0.9 * t + f)) * (1.0 - 0.75 * scare);
		const Vec3 p = foot + Vec3{R * std::cos(a), 0.24 + 0.05 * std::sin(2.1 * t + f) - 0.12 * scare, R * std::sin(a)};
		const Vec3 fwd{-std::sin(a), 0.0, std::cos(a)};
		Mesh one, oneBands, oneDark;
		littleFish(one, oneBands, oneDark, 0.09, std::sin(14.0 * t + f), 3);
		const Mat4 m = placed(p, fwd);
		add(body, one, m), add(bands, oneBands, m), add(dark, oneDark, m);
	}
	return out + attr(coated({0.75, 0.42, 0.62}, 0.4), emitMesh(tentacles, identity())) + attr(coated({0.55, 0.3, 0.45}, 0.5), emitMesh(base, identity())) +
		   attr(coated({0.95, 0.42, 0.06}, 0.25), emitMesh(body, identity())) + attr(coated({0.95, 0.95, 0.93}, 0.25), emitMesh(bands, identity())) +
		   attr(coated({0.03, 0.03, 0.03}, 0.1), emitMesh(dark, identity()));
}

// ---- a pufferfish ----
inline double puffedAt(const Puffer &p, double t, double nearestNow) {
	double puff = smoothstep(1.1, 0.8, nearestNow);
	for (const double at : p.at_) puff = std::max(puff, smoothstep(at, at + 0.4, t) * (1.0 - smoothstep(at + 4.0, at + 5.0, t)));
	return puff;
}
inline std::string pufferText(const Puffer &p, double t, const std::vector<Vec3> &others) {
	using namespace detail;
	const double a = 0.35 * t + p.seed;
	const Vec3 pos = p.at + Vec3{0.4 * std::cos(a), 0.05 * std::sin(1.3 * t), 0.4 * std::sin(a)}, fwd{-std::sin(a), 0.0, std::cos(a)};
	const double puff = puffedAt(p, t, nearest(pos, others)), L = 0.22;
	Mesh body, spikes, dark;
	const double round = 1.0 + 1.4 * puff;   // (it swells into a ball)
	add(body, ellipsoid({0.0, 0.0, 0.0}, {0.25 * L * round, 0.28 * L * round, (0.5 - 0.15 * puff) * L * round}));
	sealife::detail::fin(body, {0.0, 0.0, -0.45 * L}, {0.2 * L * std::sin(9.0 * t), 0.15 * L, -0.8 * L}, {0.2 * L * std::sin(9.0 * t), -0.15 * L, -0.8 * L});
	body.uvs.clear();
	if (puff > 0.05)
		for (int i = 0; i < 40; ++i) {   // spikes standing up as it swells
			const double u = hash01(i, p.seed + 3), v = 2.0 * kPi * hash01(i, p.seed + 4), th = std::acos(2.0 * u - 1.0);
			const Vec3 n{std::sin(th) * std::cos(v), std::cos(th), std::sin(th) * std::sin(v)};
			const Vec3 s = Vec3{n.x * 0.25, n.y * 0.28, n.z * 0.35} * (L * round);
			add(spikes, sdf::tubeMesh({s, s + n * (0.12 * L * puff)}, 0.012 * L, 0.1, 4));
		}
	for (const double side : {1.0, -1.0}) add(dark, ellipsoid({side * 0.2 * L * round, 0.1 * L * round, 0.3 * L * round}, {0.07 * L, 0.07 * L, 0.07 * L}, 8, 6));
	const Mat4 m = placed(pos, fwd);
	return "# a pufferfish (film_vignettes.h)\n" + attr(coated({0.78, 0.7, 0.42}, 0.3), emitMesh(body, m)) + attr(coated({0.55, 0.48, 0.3}, 0.4), emitMesh(spikes, m)) +
		   attr(coated({0.03, 0.03, 0.03}, 0.1), emitMesh(dark, m));
}

// ---- a flounder ----
inline std::string flounderText(const Flounder &f, double t, const std::function<double(double, double)> &bedAt, const underwater::Current &) {
	using namespace detail;
	// where it lies: at first its own place; after its burst, two metres on
	const double since = t - f.at, a = 2.0 * kPi * hash01(1, f.seed + 9);
	const Vec3 away{2.0 * std::cos(a), 0.0, 2.0 * std::sin(a)};
	const double go = smoothstep(0.0, 1.6, since);
	Vec3 p{f.x + away.x * go, 0.0, f.z + away.z * go};
	const double hop = since > 0.0 && since < 1.6 ? std::sin(kPi * since / 1.6) * 0.5 : 0.0;   // (up off the bottom and down again)
	p.y = bedAt(p.x, p.z) + 0.01 + hop;
	const double flap = since > 0.0 && since < 1.6 ? std::sin(2.0 * kPi * 4.0 * since) : 0.0;
	const double buried = since < 0.0 ? 1.0 : smoothstep(3.0, 6.0, since);   // (it shuffles back under the sand)
	std::string out = "# a flounder (film_vignettes.h)\n";
	Mesh body, eyes, pupils;
	// a flat oval, its edge rippling as it flaps
	const int n = 24;
	const int c0 = static_cast<int>(body.verts.size());
	body.verts.push_back({0.0, 0.0, 0.0}), body.normals.push_back({0.0, 1.0, 0.0});
	for (int i = 0; i < n; ++i) {
		const double u = 2.0 * kPi * i / n;
		body.verts.push_back({0.14 * std::cos(u), 0.025 * flap * std::sin(3.0 * u), 0.22 * std::sin(u)}), body.normals.push_back({0.0, 1.0, 0.0});
	}
	for (int i = 0; i < n; ++i) body.tris.push_back({c0, c0 + 1 + (i + 1) % n, c0 + 1 + i});
	for (const double side : {1.0, -1.0}) {   // both eyes on its upper side, swivelling
		const Vec3 e{side * 0.03, 0.012, 0.09};
		add(eyes, ellipsoid(e, {0.016, 0.016, 0.016}, 8, 6));
		add(pupils, ellipsoid(e + Vec3{0.008 * std::sin(1.3 * t + side), 0.01, 0.008 * std::cos(0.9 * t + side)}, {0.008, 0.008, 0.008}, 6, 4));
	}
	const Mat4 m = mul(translate(story(p)), rotate({0, 1, 0}, 360.0 * hash01(2, f.seed)));
	const Vec3 sand{0.78, 0.68, 0.5}, own{0.55, 0.45, 0.32};
	out += attr(coated(lerp(own, sand, 0.6 * buried), 0.6), emitMesh(body, m)) + attr(coated({0.85, 0.8, 0.5}, 0.2), emitMesh(eyes, m)) + attr(coated({0.03, 0.03, 0.03}, 0.1), emitMesh(pupils, m));
	if (since >= 0.0) {   // the silt it throws up where it was, and where it lands
		out += undersea::siltText({f.x, bedAt(f.x, f.z), f.z}, since, 0.5, f.seed * 11);
		out += undersea::siltText({f.x + away.x, bedAt(f.x + away.x, f.z + away.z), f.z + away.z}, since - 1.6, 0.35, f.seed * 13);
	}
	return out;
}

// ---- a moray in its hole ----
inline std::string morayText(const Moray &mo, double t, const std::function<double(double, double)> &bedAt) {
	using namespace detail;
	const Vec3 foot{mo.at.x, bedAt(mo.at.x, mo.at.z), mo.at.z};
	const Vec3 hole = foot + Vec3{0.0, std::max(0.25, mo.at.y), 0.0};
	const double th = mo.toward * kPi / 180.0;
	const Vec3 out_{std::sin(th), 0.0, std::cos(th)};
	double lunge = 0.0;
	for (const double at : mo.lunges) lunge = std::max(lunge, smoothstep(at, at + 0.25, t) * (1.0 - smoothstep(at + 0.9, at + 1.8, t)));
	const double breathe = 0.5 + 0.5 * std::sin(2.0 * kPi * t / 1.4 + mo.seed), gape = std::max(0.25 * breathe, lunge);
	std::string s = "# a moray in its hole (film_vignettes.h)\n";
	// its rock: a lump with the hole facing out
	s += attr(coated({0.38, 0.35, 0.32}, 0.8), emitMesh(nature::rock(0.6, mo.seed), mul(translate(story(foot + Vec3{0.0, 0.25, 0.0} - out_ * 0.35)), scale(1.0, 0.9, 1.0))));
	// the eel: a fat tube from inside the hole out along its way, swaying, its head at the end
	const double reach = 0.18 + 0.45 * lunge;
	std::vector<Vec3> spine;
	for (int i = 0; i <= 8; ++i) {
		const double u = i / 8.0, sway = 0.03 * std::sin(1.1 * t + 3.0 * u) * (1.0 - lunge);
		spine.push_back(story(hole - out_ * 0.4 + out_ * ((0.4 + reach) * u) + Vec3{out_.z * sway, 0.02 * std::sin(0.8 * t + u), -out_.x * sway}));
	}
	Mesh eel, jaw, dark;
	add(eel, sdf::tubeMesh(spine, 0.06, 0.8, 10));
	const Vec3 head = hole + out_ * reach;
	const Mat4 hm = placed(head, out_);
	add(eel, ellipsoid({0.0, 0.02, 0.05}, {0.055, 0.05, 0.1}), hm);
	add(jaw, ellipsoid({0.0, -0.025 - 0.03 * gape, 0.06}, {0.045, 0.02, 0.085}), mul(hm, rotate({1, 0, 0}, 25.0 * gape)));
	add(dark, ellipsoid({0.0, -0.005, 0.07}, {0.035, 0.012 + 0.03 * gape, 0.07}), hm);
	for (const double side : {1.0, -1.0}) add(dark, ellipsoid({side * 0.04, 0.05, 0.1}, {0.012, 0.012, 0.012}, 8, 6), hm);
	return s + attr(coated({0.35, 0.42, 0.18}, 0.3), emitMesh(eel, identity())) + attr(coated({0.42, 0.5, 0.22}, 0.3), emitMesh(jaw, identity())) +
		   attr(coated({0.04, 0.03, 0.03}, 0.2), emitMesh(dark, identity()));
}

// ---- a cleaner shrimp on a hand ----
// `hand`: the character's hand at t (pbrt's), `up` the way up there.
inline std::string shrimpText(const Shrimp &sh, double t, const Vec3 &hand) {
	using namespace detail;
	if (t < sh.t0 || t > sh.t1) return std::string();
	const double a = 1.3 * t, walk = 0.03 * std::sin(0.7 * t);
	const Vec3 p = hand + Vec3{walk * std::cos(a), 0.025, walk * std::sin(a)}, fwd{std::cos(0.5 * t), 0.0, std::sin(0.5 * t)};
	Mesh red, white;
	add(red, ellipsoid({0.0, 0.0, 0.0}, {0.008, 0.009, 0.03}, 10, 6));
	add(white, ellipsoid({0.0, 0.007, 0.0}, {0.003, 0.003, 0.028}, 6, 4));
	for (const double side : {1.0, -1.0}) {
		add(white, sdf::tubeMesh({{side * 0.004, 0.006, 0.025}, {side * 0.02, 0.02 + 0.006 * std::sin(6.0 * t + side), 0.06}}, 0.0012, 0.5, 4));   // (its feelers waving)
		for (int l = 0; l < 3; ++l) add(red, sdf::tubeMesh({{side * 0.006, -0.004, 0.012 - 0.01 * l}, {side * 0.016, -0.014, 0.014 - 0.01 * l + 0.004 * std::sin(10.0 * t + l)}}, 0.0012, 0.5, 4));
	}
	const Mat4 m = placed(p, fwd);
	return "# a cleaner shrimp (film_vignettes.h)\n" + attr(coated({0.85, 0.12, 0.08}, 0.2), emitMesh(red, m)) + attr(coated({0.95, 0.95, 0.92}, 0.2), emitMesh(white, m));
}

// ---- a dugong grazing a seagrass meadow ----
// It cruises slowly round a circle of `radius` about the place, and every nine seconds tips its nose down into the grass and crops it, a puff of silt at its snout.
inline std::string dugongText(const Dugong &d, double t, const std::function<double(double, double)> &bedAt) {
	using namespace detail;
	const double R = std::max(2.0, d.radius), a = 0.3 * t / R + 2.0 * kPi * hash01(1, d.seed);
	const double cycle = std::fmod(t + 9.0 * hash01(2, d.seed), 9.0), graze = smoothstep(0.5, 2.0, cycle) * (1.0 - smoothstep(4.5, 6.0, cycle));
	const Vec3 fwd{-std::sin(a), 0.0, std::cos(a)};
	Vec3 p = d.at + Vec3{R * std::cos(a), 0.0, R * std::sin(a)};
	p.y = bedAt(p.x, p.z) + 0.75 - 0.4 * graze + 0.05 * std::sin(0.5 * t);
	const double L = 2.6, beat = std::sin(1.6 * t) * (1.0 - 0.6 * graze);
	Mesh body, flippers, dark;
	add(body, deepsea::torpedo(L, 0.95, 0.85, 0.06 * beat, 0.0));
	add(body, ellipsoid({0.0, -0.06 * L, 0.5 * L}, {0.11 * L, 0.08 * L, 0.07 * L}));   // (its big drooping snout)
	sealife::detail::fin(flippers, {0.0, 0.0, -0.46 * L}, {0.32 * L, 0.12 * L * beat, -0.62 * L}, {-0.32 * L, 0.12 * L * beat, -0.62 * L});   // (the whale-like fluke, level)
	for (const double side : {1.0, -1.0}) {
		sealife::detail::fin(flippers, {side * 0.12 * L, -0.04 * L, 0.28 * L}, {side * 0.26 * L, -0.1 * L, 0.2 * L + 0.03 * L * std::sin(1.2 * t + side)}, {side * 0.14 * L, -0.06 * L, 0.16 * L});
		add(dark, ellipsoid({side * 0.085 * L, 0.03 * L, 0.42 * L}, {0.012 * L, 0.012 * L, 0.012 * L}, 8, 6));
	}
	const Mat4 m = mul(placed(p, fwd), rotate({1, 0, 0}, 22.0 * graze));   // (nose down to graze)
	std::string out = "# a dugong (film_vignettes.h)\n" + attr(coated({0.55, 0.5, 0.47}, 0.5), emitMesh(body, m)) + attr(coated({0.47, 0.42, 0.4}, 0.5), emitMesh(flippers, m)) +
					  attr(coated({0.03, 0.03, 0.03}, 0.1), emitMesh(dark, m));
	// each crop of grass: a puff of silt where its snout touched (the last two, so the clouds hang about)
	const double start = t - cycle + 1.6;
	for (int k = 0; k < 2; ++k) {
		const double when = start - 9.0 * k;
		if (t - when < 0.0 || when < 0.0) continue;
		const double aw = 0.3 * when / R + 2.0 * kPi * hash01(1, d.seed);
		const Vec3 snout = d.at + Vec3{R * std::cos(aw), 0.0, R * std::sin(aw)} + Vec3{-std::sin(aw), 0.0, std::cos(aw)} * (0.5 * L);
		out += undersea::siltText({snout.x, bedAt(snout.x, snout.z), snout.z}, t - when, 0.4, d.seed * 17 + static_cast<int>(std::floor(when)));
	}
	return out;
}

// The vignettes with their heights made pbrt's (above the seabed there).
inline World lifted(World w, const std::function<double(double, double)> &groundAt) {
	for (Clownfish &c : w.clownfish) c.at.y += groundAt(c.at.x, c.at.z);
	for (Puffer &p : w.puffers) p.at.y += groundAt(p.at.x, p.at.z);
	return w;
}

}  // namespace vignettes
}  // namespace cartoon
