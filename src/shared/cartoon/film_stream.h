#pragma once
// film_stream.h -- rivers that run (water round 4): `river ... [clear] [rocks N] [rapids S0 S1]... [depth D] [seed K]`. A river was a flat ribbon of one colour; this is its water
// as it moves and what lies under it.
//
//   the water    a surface as fine as the camera can see, its points a hand apart near it. Ripples go with the flow, faster in the middle than at the banks (read through two
//                phases of moving noise, each fading out before it has stretched too far: a flow map). A rock in the stream humps the water up on its upstream side, trails a V of
//                standing waves and a white seam from its shoulders, and dimples the slack water behind it. Rapids (`rapids S0 S1`, metres along, or wherever the river drops fast)
//                are standing waves that stay where they are while the water runs through them, white on their crests.
//   the bed      a channel of gravel and stones under the water, darker and greener with depth, and a bank of gravel either side. `clear` water shows it, with the stones on it;
//                otherwise the water is a murky colour and only the banks and the rocks show.
//   its end      where a river runs into the film's sea or lake it widens and its surface slips under the other's.
//
// Coordinates are pbrt's; s is metres along the river, c metres across it (0 its middle). Every frame is a function of its time alone.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_cache.h"
#include "film_sky.h"
#include "film_stir.h"
#include "film_water.h"
#include "nature_lib.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace stream {

// Lays a river out once the land is known: its way, how far along each point is, the extra drop of its rapids, where it ends in `seaLevel` (a sea or lake's, or -1e30), its rocks.
inline void prepare(water::River &r, const terrain::Field *ground, double seaLevel) {
	if (r.path.empty()) r.path = water::riverPath(r, ground);
	if (r.path.size() < 2) return;
	r.along.assign(r.path.size(), 0.0);
	for (std::size_t i = 1; i < r.path.size(); ++i) r.along[i] = r.along[i - 1] + std::hypot(r.path[i].x - r.path[i - 1].x, r.path[i].z - r.path[i - 1].z);
	const double L = r.along.back();
	double extra = 0.0, last = 0.0;
	for (std::size_t i = 0; i < r.path.size(); ++i) {   // (rapids fall: all that comes after is that much lower)
		const double ds = r.along[i] - last;
		last = r.along[i];
		for (const auto &q : r.rapids)
			if (r.along[i] > q.first && r.along[i] <= q.second) extra += 0.05 * ds;
		r.path[i].y -= extra;
	}
	r.mouth = false;
	if (seaLevel > -1e29) {
		for (Vec3 &p : r.path) p.y = std::max(p.y, seaLevel - 0.03);
		r.mouth = r.path.back().y < seaLevel + 0.4;
		if (r.mouth) {
			r.seaFrom = L;
			for (std::size_t i = r.path.size(); i-- > 0;)
				if (r.path[i].y < seaLevel + 0.12) r.seaFrom = r.along[i];
		}
	}
	if (r.depth <= 0.0) r.depth = std::clamp(r.width / 7.0, 0.3, 1.4);
	r.stones.clear();
	for (int n = 0; n < r.rocks; ++n) {
		const int k = r.seed * 7919 + n;
		water::Rock q;
		q.s = (0.04 + 0.92 * hash01(k, 1)) * L;
		if (!r.rapids.empty() && hash01(k, 2) < 0.6) {   // (most of them in the rapids: they are why it is rapids)
			const auto &w = r.rapids[static_cast<std::size_t>(hash01(k, 3) * r.rapids.size()) % r.rapids.size()];
			q.s = lerp(w.first, w.second, hash01(k, 4));
		}
		q.c = (hash01(k, 5) - 0.5) * 0.84 * r.width;
		q.r = std::clamp(r.width * (0.03 + 0.07 * std::pow(hash01(k, 6), 2.0)), 0.12, 1.2);
		r.stones.push_back(q);
	}
	std::sort(r.stones.begin(), r.stones.end(), [](const water::Rock &a, const water::Rock &b) { return a.s < b.s; });
}

// Where the river is s metres along: its middle (with the water's height), which way it runs and which way is across.
struct Place {
	Vec3 at, along, side;
};
inline Place placeAt(const water::River &r, double s) {
	const auto it = std::lower_bound(r.along.begin(), r.along.end(), s);
	const std::size_t b = std::clamp<std::size_t>(static_cast<std::size_t>(it - r.along.begin()), 1, r.along.size() - 1), a = b - 1;
	const double u = std::clamp((s - r.along[a]) / std::max(1e-9, r.along[b] - r.along[a]), 0.0, 1.0);
	Place p;
	p.at = lerp(r.path[a], r.path[b], u);
	const std::size_t far_ = std::min(r.path.size() - 1, b + 2), back = a >= 2 ? a - 2 : 0;
	Vec3 d = r.path[far_] - r.path[back];
	d.y = 0.0;
	p.along = normalize(d + Vec3{1e-9, 0.0, 0.0});
	p.side = {-p.along.z, 0.0, p.along.x};
	return p;
}
inline double widthAt(const water::River &r, double s) { return r.mouth ? r.width * (1.0 + 1.6 * smoothstep(r.seaFrom - 2.0 * r.width, r.seaFrom + 2.0 * r.width, s)) : r.width; }
// How much of a rapid it is at s, 0 to 1: a `rapids` line, or a stretch where it drops fast.
inline double rapidAt(const water::River &r, double s) {
	double w = 0.0;
	for (const auto &q : r.rapids) w = std::max(w, smoothstep(q.first - 3.0, q.first + 2.0, s) * (1.0 - smoothstep(q.second - 2.0, q.second + 3.0, s)));
	if (r.along.size() >= 2) {
		const auto it = std::lower_bound(r.along.begin(), r.along.end(), s);
		const std::size_t b = std::clamp<std::size_t>(static_cast<std::size_t>(it - r.along.begin()), 3, r.along.size() - 1), a = b - 3;
		w = std::max(w, smoothstep(0.14, 0.3, (r.path[a].y - r.path[b].y) / std::max(0.5, r.along[b] - r.along[a])));
	}
	return w;
}
// How fast the water goes c metres across: its full speed in the middle, a third of it at the banks.
inline double speedAt(const water::River &r, double c, double width) {
	const double v = std::min(1.0, std::abs(2.0 * c / width));
	return r.speed * (1.0 - 0.67 * v * v * v);
}

// The height of the water at (s, c) above its plain surface, from the rocks and the rapids, and how white it is there.
inline double shapeAt(const water::River &r, const std::vector<const water::Rock *> &rocks, double s, double c, double t, double rapid, double &white) {
	double h = 0.0;
	const double U = std::max(0.3, r.speed), lambda = std::clamp(6.28 * U * U / 9.81, 0.5, 4.0), width = widthAt(r, s), edge = 1.0 - smoothstep(0.34, 0.5, std::abs(c) / width);
	for (const water::Rock *q : rocks) {
		const double ds = s - q->s, dc = c - q->c, R = q->r, a = 0.3 * R * std::min(1.0, U / 2.0);
		const double near_ = ds * ds + dc * dc;
		h += a * std::exp(-((ds + 1.1 * R) * (ds + 1.1 * R) + dc * dc) / (1.1 * R * R));   // (the pillow it pushes up)
		if (near_ < R * R * 5.0) white = std::max(white, 0.9 * std::exp(-((ds + R) * (ds + R) + dc * dc * 0.5) / (0.9 * R * R)));
		if (ds <= 0.0) continue;
		h -= 0.5 * a * std::exp(-((ds - 2.0 * R) * (ds - 2.0 * R) + dc * dc) / (1.5 * R * R));   // (the dimple behind it)
		const double spread = R + 0.36 * ds, off = std::abs(dc) - spread, thick = 0.25 * R + 0.1 * ds, seam = std::exp(-off * off / (thick * thick)), fall = std::exp(-ds / (9.0 * R));
		h += 0.55 * a * seam * fall * std::cos(6.283 * ds / lambda);
		white = std::max(white, 0.75 * seam * std::exp(-ds / (2.2 * R)) * (0.7 + 0.5 * nature::noise2(ds * 2.0 - U * t, dc * 2.0, 417)));
	}
	for (const water::River::Bore &b : r.bores) {   // a wall of water coming down the river
		const double age = t - b.t;
		if (age <= 0.0) continue;
		const double go = U + std::sqrt(9.81 * 2.0 * b.height), front = b.from + go * age, d = front - s;
		if (d < -2.0 || s < b.from - 2.0) continue;
		h += b.height * smoothstep(-1.5, 1.0, d) * std::exp(-std::max(0.0, d) / (24.0 + 3.0 * go));
		white = std::max(white, smoothstep(-2.0, -0.4, d) * (1.0 - smoothstep(0.8, 4.5, d)) * (0.7 + 0.6 * nature::noise2(s * 1.7 - go * t, c * 2.1, 423)));
	}
	if (rapid > 0.0) {   // standing waves: they keep their place while the water runs through
		const double wave = std::sin(6.283 * s / lambda + 0.6 * std::sin(0.9 * c + 1.3)) * (0.65 + 0.35 * std::cos(2.4 * c + 0.4 * s));
		const double A = rapid * edge * (0.05 + 0.02 * r.width / 6.0);
		h += A * (wave + 0.2 * std::sin(3.0 * t + 1.7 * s + 2.2 * c));
		white = std::max(white, rapid * edge * smoothstep(0.35, 0.85, wave) * (0.55 + 0.75 * nature::noise2(s * 1.4 - 0.7 * U * t, c * 1.8, 419)));
		white = std::max(white, 0.6 * rapid * edge * smoothstep(0.55, 0.8, nature::noise2(s * 0.6 - U * t, c * 0.9, 421)));
	}
	return h;
}
// The ripples' tilts along and across at (s, c): moving noise at two scales, read through two phases that fade in and out (a flow map).
inline void ripplesAt(double s, double c, double v, double t, double strength, double &ta, double &tb) {
	const double P = 1.8;
	for (int phase = 0; phase < 2; ++phase) {
		const double u = t / P + 0.5 * phase, whole = std::floor(u), f = u - whole, w = 1.0 - std::abs(2.0 * f - 1.0), go = s - v * (f - 0.5) * P + 13.7 * whole;
		const int salt = 500 + 2 * phase;
		ta += w * strength * ((nature::noise2(go * 0.8, c * 1.3, salt) - 0.5) + 0.7 * (nature::noise2(go * 2.6, c * 3.4, salt + 40) - 0.5) + 0.45 * (nature::noise2(go * 7.0, c * 9.0, salt + 80) - 0.5));
		tb += w * strength * ((nature::noise2(go * 0.8 + 7.0, c * 1.3, salt + 1) - 0.5) + 0.7 * (nature::noise2(go * 2.6 + 3.0, c * 3.4, salt + 41) - 0.5) + 0.45 * (nature::noise2(go * 7.0 + 5.0, c * 9.0, salt + 81) - 0.5));
	}
}
inline std::vector<const water::Rock *> rocksNear(const water::River &r, double s) {
	std::vector<const water::Rock *> out;
	for (const water::Rock &q : r.stones)
		if (s > q.s - 4.0 * q.r && s < q.s + 40.0 * q.r) out.push_back(&q);
	return out;
}
// The surface's height at pbrt's (x, z)-side terms (s, c): for what floats on it.
inline void surfaceAt(const water::River &r, double s, double c, double t, double &y, double &slopeS, double &slopeC) {
	const std::vector<const water::Rock *> rocks = rocksNear(r, s);
	double white = 0.0;
	const double rapid = rapidAt(r, s), e = 0.25;
	const double h = shapeAt(r, rocks, s, c, t, rapid, white);
	y = placeAt(r, s).at.y + h;
	slopeS = (shapeAt(r, rocks, s + e, c, t, rapid, white) - h) / e;
	slopeC = (shapeAt(r, rocks, s, c + e, t, rapid, white) - h) / e;
}

// The surface's height at pbrt's (x, z): for what floats on it.
inline double heightAt(const water::River &r, double x, double z, double t) {
	std::size_t best = 0;
	double closest = 1e30;
	for (std::size_t i = 0; i < r.path.size(); i += 3) {
		const double d = std::hypot(r.path[i].x - x, r.path[i].z - z);
		if (d < closest) closest = d, best = i;
	}
	const Place p = placeAt(r, r.along[best]);
	const double s = r.along[best] + (x - p.at.x) * p.along.x + (z - p.at.z) * p.along.z, c = (x - p.at.x) * p.side.x + (z - p.at.z) * p.side.z;
	double y = 0.0, a = 0.0, b = 0.0;
	surfaceAt(r, s, c, t, y, a, b);
	return y;
}

namespace detail {
struct Vec3Scale {
	double x, y, z;
};
inline Vec3 operator*(Vec3 a, Vec3Scale s) { return {a.x * s.x, a.y * s.y, a.z * s.z}; }
inline std::string keyOf(const water::River &r) {
	unsigned long long v = 1469598103934665603ull;
	const auto mix = [&v](double d) {
		const long long q = static_cast<long long>(d * 100.0);
		for (int k = 0; k < 8; ++k) v = (v ^ static_cast<unsigned long long>((q >> (8 * k)) & 255)) * 1099511628211ull;
	};
	mix(r.width), mix(r.depth), mix(r.seed), mix(r.rocks), mix(r.along.empty() ? 0.0 : r.along.back());
	for (const Vec3 &p : r.path) mix(p.x), mix(p.y), mix(p.z);
	char b[32];
	std::snprintf(b, sizeof b, "%016llx", v);
	return b;
}
inline double bedDrop(const water::River &r, double s, double v) {   // how far under the water the bed is, v -1 to 1 across
	const double D = r.depth * (1.0 - 0.45 * rapidAt(r, s));
	return D * std::pow(std::max(0.0, 1.0 - v * v), 0.7);
}
// One stone of a bed of gravel at (x, y) in cells: how much each is its own (0 to 1), and how near the crack between it and the next (0 in a crack). It repeats every `period` cells along y.
inline void gravelAt(double x, double y, int period, double &tone, double &crack) {
	const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
	double d1 = 1e9, d2 = 1e9;
	int best = 0;
	for (int a = ix - 1; a <= ix + 1; ++a)
		for (int b = iy - 1; b <= iy + 1; ++b) {
			const int wrapped = ((b % period) + period) % period, k = a * 7919 + wrapped * 104729;
			const double dx = a + hash01(k, 1) - x, dy = b + hash01(k, 2) - y, d = dx * dx + dy * dy;
			if (d < d1) d2 = d1, d1 = d, best = k;
			else if (d < d2) d2 = d;
		}
	tone = hash01(best, 3), crack = smoothstep(0.0, 0.14, std::sqrt(d2) - std::sqrt(d1));
}
// The bed's picture: across (u 0 to 1 is v -1.6 to 1.6) and along, repeating.
inline std::string bedPicture(const std::string &film, const water::River &r) {
	const int W = 512, H = 1536;
	const bool clear = r.clear;
	return film_sky::picture(film, std::string("river3_bed") + (clear ? "_c" : "_m") + std::to_string(r.seed), W, H, [&](std::vector<float> &rgb) {
		const int cells = 256;   // (along: about a stone every 9 cm)
		const double perCell = static_cast<double>(cells) / H;
		for (int y = 0; y < H; ++y)
			for (int x = 0; x < W; ++x) {
				const double u = (x + 0.5) / W, across = -1.6 + 3.2 * u, a = std::abs(across);
				double tone = 0.0, crack = 0.0, small = 0.0, gap = 0.0;
				gravelAt(x * perCell, y * perCell, cells, tone, crack);
				gravelAt(x * perCell * 2.3, y * perCell * 2.3 * 2.0 / 2.0, cells * 2, small, gap);
				const double shade = nature::noise2(u * 9.0, std::sin(6.283 * (y + 0.5) / H) * 3.0, 707);
				Vec3 c = lerp(Vec3{0.2, 0.17, 0.13}, Vec3{0.6, 0.54, 0.45}, std::clamp(0.1 + 0.7 * tone + 0.3 * (shade - 0.5), 0.0, 1.0));
				if (tone > 0.86) c = c * Vec3Scale{1.25, 1.05, 0.9};   // (a rusty one here and there)
				c = c * (0.35 + 0.65 * crack) * (0.75 + 0.25 * gap);
				if (a > 1.0) c = c * lerp(0.6, 1.0, smoothstep(1.0, 1.2, a));   // (dark where it is wet, paler above that)
				else c = c * 0.72;   // (wet under it)
				if (clear) c = lerp(Vec3{c.x * 0.42, c.y * 0.62, c.z * 0.55}, c, smoothstep(0.1, 0.95, a));   // (the water's own green-brown, deeper in the middle)
				const std::size_t k = (static_cast<std::size_t>(y) * W + x) * 3;
				rgb[k] = static_cast<float>(c.x), rgb[k + 1] = static_cast<float>(c.y), rgb[k + 2] = static_cast<float>(c.z);
			}
	});
}
inline Mesh boulder(double radius, int seed, int nu = 18, int nv = 10) {
	Mesh m = uvSphere(1.0, nu, nv);
	for (std::size_t i = 0; i < m.verts.size(); ++i) {
		const Vec3 p = m.verts[i];
		const double b = 0.72 + 0.5 * nature::noise2(p.x * 1.4 + seed, p.z * 1.4 + p.y * 1.1, 733) + 0.14 * nature::noise2(p.x * 4.0 + p.y * 3.0, p.z * 4.0 - seed, 735);
		m.verts[i] = {p.x * radius * b, p.y * radius * b * 0.8, p.z * radius * b};
	}
	sdf::computeNormals(m);
	return m;
}
// The rocks in the stream, and the stones along its edges (the same every frame).
inline Mesh stonesMesh(const water::River &r) {
	Mesh all;
	const auto put = [&all](const Mesh &m, Vec3 at) {
		const int base = static_cast<int>(all.verts.size());
		const Vec3 story{-at.x, at.y, at.z};
		for (std::size_t i = 0; i < m.verts.size(); ++i) all.verts.push_back(m.verts[i] + story), all.normals.push_back(m.normals[i]);
		for (const auto &t : m.tris) all.tris.push_back({t[0] + base, t[1] + base, t[2] + base});
	};
	for (std::size_t n = 0; n < r.stones.size(); ++n) {
		const water::Rock &q = r.stones[n];
		const Place p = placeAt(r, q.s);
		const double w = widthAt(r, q.s);
		put(boulder(q.r, r.seed + static_cast<int>(n)), p.at + p.side * q.c + Vec3{0.0, 0.32 * q.r - 0.45 * bedDrop(r, q.s, 2.0 * q.c / w) * 0.3, 0.0});
	}
	const double L = r.along.back();
	const int stones = std::min(1600, static_cast<int>(2.2 * L));
	for (int n = 0; n < stones; ++n) {
		const int k = r.seed * 131 + n;
		const double s = L * hash01(k, 1), v = (hash01(k, 2) < 0.5 ? -1.0 : 1.0) * (0.55 + 0.6 * hash01(k, 3)), size = 0.05 + 0.2 * std::pow(hash01(k, 4), 3.0);
		const Place p = placeAt(r, s);
		const double w = widthAt(r, s);
		put(boulder(size, k, 8, 5), p.at + p.side * (0.5 * v * w) + Vec3{0.0, (std::abs(v) < 1.0 ? -bedDrop(r, s, v) : 0.02) + 0.3 * size, 0.0});
	}
	return all;
}
}  // namespace detail

// The river as scene text at t, seen from `eye` (story space) looking at `target`.
// `flat`: in a toon film, the colour it is painted (film_toonwater.h), or null.
inline std::string text(const std::string &film, const water::River &r, double t, const terrain::Field *ground, const Vec3 &eyeStory, const Vec3 &targetStory, double hfov, const stir::Field *stirred = nullptr, const Vec3 *flat = nullptr) {
	if (r.path.size() < 2 || r.along.empty()) return std::string();
	const Vec3 eye{-eyeStory.x, eyeStory.y, eyeStory.z}, ahead = normalize(Vec3{-(targetStory.x - eyeStory.x), 0.0, targetStory.z - eyeStory.z} + Vec3{1e-9, 0.0, 0.0});
	const double L = r.mouth ? std::min(r.along.back(), r.seaFrom + 2.0 * r.width) : r.along.back(), wide = std::cos(std::min(kPi, (0.5 * hfov + 16.0) * kPi / 180.0));   // (past the sea's edge there is no more of it)
	std::vector<double> rows;
	for (double s = 0.0; s < L;) {
		rows.push_back(s);
		const Vec3 p = placeAt(r, s).at;
		s += std::clamp(0.012 * length(p - eye), 0.16, 6.0);
	}
	rows.push_back(L);
	const int across = std::clamp(static_cast<int>(r.width / 0.16), 16, 64), n = across + 1;
	const auto seen = [&](double s) {
		const Vec3 p = placeAt(r, s).at, to{p.x - eye.x, 0.0, p.z - eye.z};
		const double d = length(to);
		return d < 2.0 * r.width + 12.0 || dot(to * (1.0 / d), ahead) > wide;
	};
	std::vector<char> kept(rows.size(), 0);
	for (std::size_t j = 0; j < rows.size(); ++j)
		if (seen(rows[j])) {
			for (std::size_t k = j > 0 ? j - 1 : j; k <= std::min(rows.size() - 1, j + 1); ++k) kept[k] = 1;
		}
	Mesh m, foam;
	std::vector<int> first(rows.size(), -1);
	std::vector<double> whites;
	const std::vector<int> allRings = [&] {
		std::vector<int> v;
		if (stirred)
			for (std::size_t q = 0; q < stirred->rings.size(); ++q) v.push_back(static_cast<int>(q));
		return v;
	}();
	const double calm = 0.05 + 0.03 * std::min(1.0, r.speed / 2.0);
	for (std::size_t j = 0; j < rows.size(); ++j) {
		if (!kept[j]) continue;
		first[j] = static_cast<int>(m.verts.size());
		const double s = rows[j], width = widthAt(r, s), rapid = rapidAt(r, s);
		const Place p = placeAt(r, s);
		const std::vector<const water::Rock *> rocks = rocksNear(r, s);
		const double d = length(p.at - eye), e = std::max(0.03, 0.003 * d);
		for (int k = 0; k <= across; ++k) {
			const double c = (k / double(across) - 0.5) * width, v = speedAt(r, c, width);
			double white = 0.0, w2 = 0.0;
			const double h = shapeAt(r, rocks, s, c, t, rapid, white), hs = shapeAt(r, rocks, s + e, c, t, rapid, w2), hc = shapeAt(r, rocks, s, c + e, t, rapid, w2);
			double ta = (hs - h) / e, tb = (hc - h) / e;
			const double strength = (calm + 0.1 * rapid) * std::exp(-d / 90.0) * 1.6;
			ripplesAt(s, c, v, t, strength, ta, tb);
			Vec3 q = p.at + p.side * c + Vec3{0.0, h, 0.0};
			if (r.mouth) q.y -= 0.5 * smoothstep(r.seaFrom, r.seaFrom + 1.5 * r.width, s);   // (slipping under the sea it runs into)
			if (stirred && stirred->any()) {
				double rh = 0.0, rx = 0.0, rz = 0.0;
				stirred->at(allRings, q.x, q.z, t, rh, rx, rz);
				q.y += rh;
				ta += rx * p.along.x + rz * p.along.z, tb += rx * p.side.x + rz * p.side.z;
			}
			const Vec3 up = normalize(Vec3{0.0, 1.0, 0.0} - p.along * ta - p.side * tb);
			m.verts.push_back({-q.x, q.y, q.z});
			m.normals.push_back({-up.x, up.y, up.z});
			whites.push_back(white);
		}
	}
	for (std::size_t j = 0; j + 1 < rows.size(); ++j) {
		if (first[j] < 0 || first[j + 1] < 0) continue;
		const double width_ = widthAt(r, rows[j]);
		for (int k = 0; k < across; ++k) {
			const int a = first[j] + k, b = first[j + 1] + k;
			m.tris.push_back({a, a + 1, b}), m.tris.push_back({a + 1, b + 1, b});
			const double w0 = whites[static_cast<std::size_t>(a)], w1 = whites[static_cast<std::size_t>(a + 1)], w2 = whites[static_cast<std::size_t>(b)], w3 = whites[static_cast<std::size_t>(b + 1)];
			if (std::max({w0, w1, w2, w3}) < 0.3 || foam.verts.size() > 600000) continue;
			const auto corner = [&](double a0, double b0) {
				return lerp(lerp(m.verts[static_cast<std::size_t>(a)], m.verts[static_cast<std::size_t>(a + 1)], a0), lerp(m.verts[static_cast<std::size_t>(b)], m.verts[static_cast<std::size_t>(b + 1)], a0), b0);
			};
			const double size = std::max(0.012, 0.0022 * length(corner(0.5, 0.5) - eyeStory));   // (pieces a pixel or two across, so its edge is the noise's and not the mesh's)
			const int cutsU = std::clamp(static_cast<int>(std::ceil(width_ / across / size)), 1, 12), cutsV = std::clamp(static_cast<int>(std::ceil((rows[j + 1] - rows[j]) / size)), 1, 12);
			for (int u = 0; u < cutsU; ++u)
				for (int v = 0; v < cutsV; ++v) {
					const double fu = (u + 0.5) / cutsU, fv = (v + 0.5) / cutsV, w = lerp(lerp(w0, w1, fu), lerp(w2, w3, fu), fv);
					const Vec3 mid = corner(fu, fv);
					const double lace = 0.45 * nature::noise2(mid.x * 2.1 + r.speed * t, mid.z * 2.1, 431) + 0.35 * nature::noise2(mid.x * 7.0, mid.z * 7.0 - r.speed * t, 433) + 0.2 * nature::noise2(mid.x * 19.0, mid.z * 19.0, 435);
					if (w * (0.55 + lace) < 0.62) continue;
					const int base = static_cast<int>(foam.verts.size());
					for (const auto &[x, y] : {std::pair<double, double>{u + 0.0, v + 0.0}, {u + 1.0, v + 0.0}, {u + 1.0, v + 1.0}, {u + 0.0, v + 1.0}}) {
						foam.verts.push_back(corner(x / cutsU, y / cutsV) + Vec3{0.0, 0.012, 0.0});
						foam.normals.push_back({0.0, 1.0, 0.0});
					}
					foam.tris.push_back({base, base + 1, base + 2}), foam.tris.push_back({base, base + 2, base + 3});
				}
		}
	}
	// the bed and its banks: coarser, and the same every frame
	const std::string key = detail::keyOf(r);
	const std::string bed = film_cache::ply(film, "river3_bed_" + key, [&] {
		Mesh b;
		const int cols = 28;
		std::vector<double> at;
		for (double s = 0.0; s < L; s += 1.2) at.push_back(s);
		at.push_back(L);
		for (double s : at) {
			const Place p = placeAt(r, s);
			const double width = widthAt(r, s);
			for (int k = 0; k <= cols; ++k) {
				const double u = k / double(cols), v = -1.6 + 3.2 * u, c = 0.5 * width * v, off = 0.5 * width * (std::abs(v) - 1.0);
				Vec3 q = p.at + p.side * c;
				if (std::abs(v) < 1.0) q.y -= detail::bedDrop(r, s, v);
				else q.y += 0.02 + 0.5 * off;
				if (r.mouth) q.y -= (1.4 + 0.5 * std::max(0.0, off)) * smoothstep(r.seaFrom - 1.5 * r.width, r.seaFrom, s);   // (it goes under the sea it runs into)
				b.verts.push_back({-q.x, q.y, q.z});   // (story space, as a ply file's points are given: film_cache.h mirrors them)
				b.uvs.push_back({u, s / (3.0 * r.width)});
				b.normals.push_back({0.0, 1.0, 0.0});
			}
		}
		const int w = cols + 1;
		for (int j = 0; j + 1 < static_cast<int>(at.size()); ++j)
			for (int k = 0; k < cols; ++k) {
				const int a = j * w + k;
				b.tris.push_back({a, a + w, a + 1}), b.tris.push_back({a + 1, a + w, a + w + 1});
			}
		sdf::computeNormals(b);
		return b;
	});
	const std::string stones = film_cache::ply(film, "river2_stones_" + key, [&] { return detail::stonesMesh(r); });
	std::string out = "# a river (film_stream.h)\n";
	if (!bed.empty()) {
		const std::string tex = "river_bed_" + std::to_string(r.seed);
		out += "AttributeBegin\n  Texture \"" + tex + "\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + detail::bedPicture(film, r) + "\" ]\n  Material \"diffuse\" \"texture reflectance\" [ \"" + tex + "\" ]\n  Shape \"plymesh\" \"string filename\" [ \"" + bed + "\" ]\nAttributeEnd\n";
	}
	if (!stones.empty()) out += "AttributeBegin\n  Material \"coateddiffuse\" \"rgb reflectance\" [ 0.26 0.24 0.21 ] \"float roughness\" [ 0.35 ]\n  Shape \"plymesh\" \"string filename\" [ \"" + stones + "\" ]\nAttributeEnd\n";
	out += attr(flat ? diffuseMaterial(*flat) : r.clear ? glassMaterial(1.333, 0.0) : coated({0.05, 0.07, 0.05}, 0.0), emitMesh(water::detail::agreeing(m), identity()));
	if (!foam.tris.empty()) out += attr(diffuseMaterial({0.8, 0.83, 0.84}), emitMesh(water::detail::agreeing(foam), identity()));
	return out;
}

}  // namespace stream
}  // namespace cartoon
