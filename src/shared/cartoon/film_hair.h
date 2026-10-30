#pragma once
// film_hair.h -- hair of strands on the films' characters (`hair [NAME] STYLE ...`, round 12): thousands of pbrt curves with pbrt's hair material (Marschner's model:
// a highlight and a coloured second one, light through the strands), in place of the character's sculpted hair.
//
//   the groom    roots spread over the scalp inside a hairline (high at the forehead, above the ears, low at the nape); each strand leaves the scalp, flows away from
//                the crown, falls under gravity (the world's down, so a tilted head's hair still hangs) and lies over the skull instead of through it; strands near
//                each other gather into clumps towards their tips, and a little frizz breaks them up
//   the styles   short (a crop), bob (to the chin), long (past the shoulders), ponytail (gathered at the back of the head and hanging from a tie), curly (springs),
//                mop (a shaggy cap)
//   the motion   each strand's tip trails where the head was a moment ago - more for a long strand than a short one - so hair swings when the head turns, nods or
//                runs, and settles when it stops; the film's gusts (`gusts`, `wind`) blow it
//
// The head is given as its frame (+x forward, +y up, +z to the right, story space) and the skull as an ellipsoid in it. Each strand is one curve of a few cubic
// segments, written in the coordinates the head's frame is in (as the kinds' meshes are).

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "cartoon_lib.h"

namespace cartoon {
namespace hair {

// A groom: a `hair` line.
struct Groom {
	std::string who;              // "" every character that can have hair
	std::string style = "short";
	double length = 0.0;          // metres at scale 1 (0: the style's)
	double density = 1.0;         // how many strands, a share of the style's
	double curl = -1.0;           // the radius of its curls, a share of the length (-1: the style's)
	double frizz = 0.15, clump = 0.5;
	Vec3 colour;
	bool hasColour = false;
	double width = 1.0;           // how thick a strand is, a share of 0.9 mm (at its root)
};
inline const std::vector<std::string> &styles() {
	static const std::vector<std::string> s = {"short", "bob", "long", "ponytail", "curly", "mop"};
	return s;
}

// The skull of a head: its frame (story space, scaled with the character) and its ellipsoid in that frame.
struct Scalp {
	Mat4 head;
	Vec3 centre, radii;
};

namespace detail {
struct Style {
	double length, strands, lift, gravity, curl;
};
inline Style styleOf(const std::string &s) {
	if (s == "bob") return {0.17, 14000, 0.10, 1.4, 0.0};
	if (s == "long") return {0.36, 16000, 0.08, 1.8, 0.0};
	if (s == "ponytail") return {0.30, 12000, 0.05, 1.6, 0.0};
	if (s == "curly") return {0.09, 11000, 0.55, 0.3, 0.12};
	if (s == "mop") return {0.09, 12000, 0.30, 0.9, 0.0};
	return {0.05, 12000, 0.30, 0.35, 0.0};   // short
}
// Whether a direction from the skull's centre (+x forward, +y up, +z right) is inside the hairline.
inline bool onScalp(Vec3 d) {
	double low = -0.50 + 0.92 * smoothstep(-0.7, 0.75, d.x);   // (the nape low at the back, the forehead high in front)
	if (std::abs(d.z) > 0.72 && d.x > -0.35 && d.x < 0.45) low = std::max(low, 0.08);   // (above the ears)
	return d.y > low;
}
// A point pushed out of the skull's ellipsoid (grown by `pad`) along the ellipsoid's normal.
inline Vec3 outside(Vec3 p, Vec3 c, Vec3 r, double pad) {
	const Vec3 q = p - c;
	const Vec3 u = {q.x / (r.x * (1.0 + pad)), q.y / (r.y * (1.0 + pad)), q.z / (r.z * (1.0 + pad))};
	const double l = length(u);
	if (l >= 1.0 || l < 1e-9) return p;
	return c + Vec3{q.x / l, q.y / l, q.z / l};
}
inline Vec3 transformDir(const Mat4 &m, Vec3 d) { return transformPoint(m, d) - transformPoint(m, {0.0, 0.0, 0.0}); }
}  // namespace detail

// The groom's strands on a head as scene text. `before` is the head's frame a moment ago (its tips trail towards where they were then: the motion), `gust` the
// wind's bend at the head (dressing::gustAt), `colour` the hair's colour (the character's), `size` the character's scale, `seed` its own number.
inline std::string text(const Groom &g, const Scalp &s, const Mat4 &before, Vec3 gust, Vec3 colour, double size, int seed) {
	const detail::Style st = detail::styleOf(g.style);
	const double L = (g.length > 0.0 ? g.length : st.length) * size, curl = (g.curl >= 0.0 ? g.curl : st.curl);
	const int count = std::clamp(static_cast<int>(st.strands * g.density), 50, 60000);
	const Mat4 inv = invertAffine(s.head);
	const double unit = length(detail::transformDir(s.head, {1.0, 0.0, 0.0}));   // (the frame's own scale: lengths in it are world lengths / unit)
	const double l = L / std::max(1e-9, unit);
	const Vec3 down = normalize(detail::transformDir(inv, {0.0, -1.0, 0.0}));
	const Vec3 wind = detail::transformDir(inv, gust) * (1.0 / std::max(1e-9, unit));
	const Vec3 crown = s.centre + Vec3{-0.35 * s.radii.x, 0.94 * s.radii.y, 0.0};
	const Vec3 tie = s.centre + Vec3{-1.02 * s.radii.x, 0.15 * s.radii.y, 0.0};   // (a ponytail's)
	const bool pony = g.style == "ponytail", parted = g.style == "bob" || g.style == "long" || pony;
	const Vec3 tailDir = normalize(down + Vec3{-0.45, 0.0, 0.0});   // (a ponytail hangs back from the head as well as down)
	constexpr int kPoints = 10;   // (three cubic segments)
	std::vector<std::array<Vec3, kPoints>> strands;
	std::vector<int> clumps;
	strands.reserve(static_cast<std::size_t>(count));
	for (int i = 0; strands.size() < static_cast<std::size_t>(count) && i < count * 4; ++i) {
		// a root on the scalp
		const double z = 2.0 * hash01(i, seed + 1) - 1.0, a = 2.0 * kPi * hash01(i, seed + 2), r = std::sqrt(std::max(0.0, 1.0 - z * z));
		const Vec3 d = {r * std::cos(a), z, r * std::sin(a)};
		if (!detail::onScalp(d)) continue;
		const Vec3 root = s.centre + Vec3{d.x * s.radii.x, d.y * s.radii.y, d.z * s.radii.z};
		const Vec3 n = normalize(Vec3{d.x / s.radii.x, d.y / s.radii.y, d.z / s.radii.z});
		Vec3 flow = root - crown;
		flow = flow - n * dot(flow, n);
		flow = length(flow) > 1e-9 ? normalize(flow) : Vec3{-1.0, 0.0, 0.0};
		if (parted) {   // (a parting: the front's strands swept to the sides and back, off the face)
			Vec3 swept = Vec3{-0.8, -0.2, d.z >= 0.0 ? 1.0 : -1.0};
			swept = normalize(swept - n * dot(swept, n));
			flow = normalize(lerp(flow, swept, smoothstep(-0.2, 0.5, d.x)));
		}
		const double own = 0.75 + 0.5 * hash01(i, seed + 3);   // (each a little longer or shorter)
		std::array<Vec3, kPoints> p;
		p[0] = root - n * (0.01 * l);
		Vec3 dir = normalize(n * st.lift + flow * (1.0 - st.lift));
		Vec3 at = root;
		const double ds = l * own / (kPoints - 1);
		for (int k = 1; k < kPoints; ++k) {
			const double u = static_cast<double>(k) / (kPoints - 1);
			if (pony && u < 0.35) {   // (gathered towards the tie over the skull, then hanging from it)
				dir = normalize(lerp(dir, normalize(tie - at), 0.6));
			} else {
				dir = normalize(dir + down * (st.gravity * ds / std::max(1e-9, l)) * (pony ? 3.0 : 1.0));
			}
			at = at + dir * ds;
			at = detail::outside(at, s.centre, s.radii, 0.025 + 0.05 * u);   // (over the skull, the outer strands a little further out)
			if (pony && u > 0.3) {   // (the tail: a tight bundle hanging back and down from the tie)
				const Vec3 axis = tie + tailDir * ((u - 0.3) / 0.7 * l * own);
				at = axis + (at - axis) * (0.30 + 0.45 * (u - 0.3));
			}
			Vec3 q = at;
			if (curl > 0.0) {   // (a spring round the strand's way)
				const Vec3 side = normalize(cross(dir, n) + Vec3{1e-9, 0.0, 0.0}), side2 = cross(dir, side);
				const double ph = 2.0 * kPi * (u * l * own / std::max(1e-6, 2.5 * curl * l)) + 6.2832 * hash01(i, seed + 4);
				q = q + (side * std::cos(ph) + side2 * std::sin(ph)) * (curl * l * smoothstep(0.0, 0.25, u));
			}
			p[static_cast<std::size_t>(k)] = q;
		}
		strands.push_back(p);
		// its clump: a cell of the scalp
		const int cx = static_cast<int>(std::floor(d.x * 7.0)), cy = static_cast<int>(std::floor(d.y * 7.0)), cz = static_cast<int>(std::floor(d.z * 7.0));
		clumps.push_back(((cx * 73856093) ^ (cy * 19349663) ^ (cz * 83492791)) & 0x7fffffff);
	}
	// clumps: each strand drawn towards its clump's mean, more towards the tip
	if (g.clump > 0.0) {
		std::vector<std::pair<int, std::size_t>> order;
		for (std::size_t i = 0; i < clumps.size(); ++i) order.push_back({clumps[i], i});
		std::sort(order.begin(), order.end());
		for (std::size_t a = 0; a < order.size();) {
			std::size_t b = a;
			while (b < order.size() && order[b].first == order[a].first) ++b;
			std::array<Vec3, kPoints> mean{};
			for (std::size_t k = a; k < b; ++k)
				for (int j = 0; j < kPoints; ++j) mean[static_cast<std::size_t>(j)] = mean[static_cast<std::size_t>(j)] + strands[order[k].second][static_cast<std::size_t>(j)] * (1.0 / static_cast<double>(b - a));
			for (std::size_t k = a; k < b; ++k) {
				auto &p = strands[order[k].second];
				const Vec3 shift = (mean[0] - p[0]);
				for (int j = 1; j < kPoints; ++j) {
					const double u = static_cast<double>(j) / (kPoints - 1), w = g.clump * 0.85 * u * u;
					p[static_cast<std::size_t>(j)] = lerp(p[static_cast<std::size_t>(j)], mean[static_cast<std::size_t>(j)] - shift * (1.0 - u), w);
				}
			}
			a = b;
		}
	}
	// the motion, the wind and the frizz; then into the world and out as curves
	const Mat4 moved = mul(inv, before);   // (a point of the head's frame a moment ago, in the frame now)
	const double lag = std::clamp(0.35 + 2.5 * L, 0.35, 1.2), sway = std::clamp(0.4 + 4.0 * L, 0.4, 2.0);
	std::string out = "# hair (film_hair.h): " + g.style + "\n";
	// the scalp under it, in the hair's colour (as a groom's scalp is painted): thin hair does not show the skin through
	{
		Mesh cap;
		const int nu = 40, nv = 20;
		for (int i = 0; i <= nv; ++i)
			for (int j = 0; j <= nu; ++j) {
				const double th = kPi * i / nv, ph = 2.0 * kPi * j / nu;
				const Vec3 d = {std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
				cap.verts.push_back(transformPoint(s.head, s.centre + Vec3{d.x * s.radii.x, d.y * s.radii.y, d.z * s.radii.z} * 1.006));
				cap.normals.push_back(normalize(detail::transformDir(s.head, {d.x / s.radii.x, d.y / s.radii.y, d.z / s.radii.z})));
			}
		for (int i = 0; i < nv; ++i)
			for (int j = 0; j < nu; ++j) {
				const int a = i * (nu + 1) + j, b = a + nu + 1;
				const double th = kPi * (i + 0.5) / nv, ph = 2.0 * kPi * (j + 0.5) / nu;
				if (!detail::onScalp({std::sin(th) * std::cos(ph), std::cos(th) - 0.06, std::sin(th) * std::sin(ph)})) continue;   // (a little inside the hairline)
				cap.tris.push_back({a, b, a + 1});
				cap.tris.push_back({a + 1, b, b + 1});
			}
		out += attr(diffuseMaterial(colour * 0.55), emitMesh(cap, identity()));
	}
	char buf[200];
	const auto tone = [](double c) { return std::pow(std::clamp(c, 0.02, 0.95), 1.6); };   // (pbrt's hair reflectance is a thick head of hair's: one layer looks lighter)
	std::snprintf(buf, sizeof buf, "AttributeBegin\nMaterial \"hair\" \"rgb reflectance\" [ %.4f %.4f %.4f ] \"float beta_m\" [ 0.28 ] \"float beta_n\" [ 0.3 ] \"float alpha\" [ 2 ]\n",
				  tone(colour.x), tone(colour.y), tone(colour.z));
	out += buf;
	const double w0 = 0.0009 * g.width * size, w1 = 0.0003 * g.width * size;
	for (std::size_t i = 0; i < strands.size(); ++i) {
		auto &p = strands[i];
		std::snprintf(buf, sizeof buf, "Shape \"curve\" \"string type\" \"cylinder\" \"float width0\" [ %.6f ] \"float width1\" [ %.6f ] \"point3 P\" [", w0, w1);
		out += buf;
		for (int j = 0; j < kPoints; ++j) {
			const double u = static_cast<double>(j) / (kPoints - 1);
			Vec3 q = p[static_cast<std::size_t>(j)];
			q = q + (transformPoint(moved, q) - q) * (lag * u * u) + wind * (sway * u * u * l);
			const double f = g.frizz * l * 0.08 * u;
			q = q + Vec3{hash01(static_cast<int>(i) * 13 + j, seed + 5) - 0.5, hash01(static_cast<int>(i) * 13 + j, seed + 6) - 0.5, hash01(static_cast<int>(i) * 13 + j, seed + 7) - 0.5} * (2.0 * f);
			if (j > 0) q = detail::outside(q, s.centre, s.radii, 0.02);
			const Vec3 w = transformPoint(s.head, q);
			std::snprintf(buf, sizeof buf, " %.5f %.5f %.5f", w.x, w.y, w.z);
			out += buf;
		}
		out += " ]\n";
	}
	return out + "AttributeEnd\n";
}

}  // namespace hair
}  // namespace cartoon
