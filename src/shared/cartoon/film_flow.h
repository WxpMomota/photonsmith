#pragma once
// film_flow.h -- under the sea, round 18: what trails from a character and flows - a mermaid's hair and tail fin, a jellyfish's arms, a cape, a scarf, a veil - worked out from
// where the character has been (CharacterMoment::wake, filled for a kind that `flows` or a character that `wear`s something: film_seaworld.cpp).
//
// A chain from a root on the body lies along the way the root came while it moves (each point where the root was a moment ago: as far back as the chain is long, at its
// speed), and in its rest shape while it is still - down in the air, up and back in the water, in the current - with a slow wave running along it. Its points come back in
// body space, to be drawn with the rest of the character.
//
//   wear WHO scarf|cape|veil|ribbon|sash [colour C] [length L] [width W]   a garment that trails from the neck (a scarf, a ribbon, a sash), the shoulders (a cape) or
//                                                                          the head (a veil): it hangs in the air and flutters in the gusts, it floats under the water

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "cartoon_lib.h"

namespace cartoon {
namespace flow {

constexpr double kWakeStep = 0.08;   // seconds between the moments a wake keeps
constexpr int kWakeSteps = 14;       // (so a chain can trail back over a second)

// A tube along `pts` with a radius at each point (`radii` as many), its cross-section `flat` times as wide across `side` as it is the other way (a tail, a ribbon of hair).
inline Mesh tube(const std::vector<Vec3> &pts, const std::vector<double> &radii, int sides = 10, double flat = 1.0, Vec3 sideHint = {1.0, 0.0, 0.0}) {
	Mesh m;
	const int n = static_cast<int>(pts.size());
	if (n < 2 || radii.size() != pts.size()) return m;
	Vec3 prev = sideHint;
	for (int i = 0; i < n; ++i) {
		const Vec3 t = normalize(pts[static_cast<std::size_t>(std::min(i + 1, n - 1))] - pts[static_cast<std::size_t>(std::max(i - 1, 0))] + Vec3{0.0, 1e-9, 0.0});
		Vec3 side = prev - t * dot(prev, t);
		if (length(side) < 1e-6) side = cross(t, std::abs(t.y) < 0.9 ? Vec3{0.0, 1.0, 0.0} : Vec3{1.0, 0.0, 0.0});
		side = normalize(side);
		prev = side;
		const Vec3 up = cross(side, t);
		const double r = radii[static_cast<std::size_t>(i)];
		for (int k = 0; k < sides; ++k) {
			const double a = 2.0 * kPi * k / sides;
			m.verts.push_back(pts[static_cast<std::size_t>(i)] + side * (std::cos(a) * r * flat) + up * (std::sin(a) * r));
			m.normals.push_back(normalize(side * (std::cos(a) / flat) + up * std::sin(a)));
		}
	}
	for (int i = 0; i + 1 < n; ++i)
		for (int k = 0; k < sides; ++k) {
			const int a = i * sides + k, b = i * sides + (k + 1) % sides;
			m.tris.push_back({a, a + sides, b});
			m.tris.push_back({b, a + sides, b + sides});
		}
	for (int e = 0; e < 2; ++e) {   // (each end closed by a fan to its middle)
		const int ring = e == 0 ? 0 : (n - 1) * sides, mid = static_cast<int>(m.verts.size());
		const Vec3 dir = e == 0 ? normalize(pts[0] - pts[1]) : normalize(pts[static_cast<std::size_t>(n - 1)] - pts[static_cast<std::size_t>(n - 2)]);
		m.verts.push_back(pts[static_cast<std::size_t>(e == 0 ? 0 : n - 1)] + dir * (0.3 * radii[static_cast<std::size_t>(e == 0 ? 0 : n - 1)]));
		m.normals.push_back(dir);
		for (int k = 0; k < sides; ++k) {
			const int a = ring + k, b = ring + (k + 1) % sides;
			if (e == 0) m.tris.push_back({mid, a, b});
			else m.tris.push_back({mid, b, a});
		}
	}
	return m;
}

// A strip, two-sided: from `left[i]` to `right[i]` along the lists (a cape, a ribbon), with uvs (u across, v along).
inline Mesh strip(const std::vector<Vec3> &left, const std::vector<Vec3> &right) {
	Mesh m;
	const int n = static_cast<int>(std::min(left.size(), right.size()));
	if (n < 2) return m;
	for (int face = 0; face < 2; ++face) {
		const int base = static_cast<int>(m.verts.size());
		for (int i = 0; i < n; ++i) {
			const Vec3 along = left[static_cast<std::size_t>(std::min(i + 1, n - 1))] - left[static_cast<std::size_t>(std::max(i - 1, 0))];
			Vec3 nrm = cross(right[static_cast<std::size_t>(i)] - left[static_cast<std::size_t>(i)], along);
			nrm = length(nrm) > 1e-12 ? normalize(nrm) : Vec3{0.0, 0.0, 1.0};
			if (face == 1) nrm = nrm * -1.0;
			for (const Vec3 &p : {left[static_cast<std::size_t>(i)], right[static_cast<std::size_t>(i)]}) m.verts.push_back(p), m.normals.push_back(nrm);
			m.uvs.push_back({0.0, i / (n - 1.0)});
			m.uvs.push_back({1.0, i / (n - 1.0)});
		}
		for (int i = 0; i + 1 < n; ++i) {
			const int a = base + 2 * i;
			if (face == 0) m.tris.push_back({a, a + 2, a + 1}), m.tris.push_back({a + 1, a + 2, a + 3});
			else m.tris.push_back({a, a + 1, a + 2}), m.tris.push_back({a + 1, a + 3, a + 2});
		}
	}
	return m;
}

// Where a character has been (CharacterMoment::wake): its frames now and every kWakeStep before (story space, frames[0] now; none when its film has nothing that flows),
// its scale, the water's flow round it (story space, m/s), whether it is in the water, the wind round it in the air.
struct Wake {
	std::vector<Mat4> frames;
	double scale = 1.0, t = 0.0;
	Vec3 water;
	bool wet = false;
	Vec3 wind;
};

// The world place (story space) of a body-space point at `back` seconds ago (0 now), `local` the kind's own lean of its body (a mermaid laid level as she swims).
inline Vec3 pastPoint(const Wake &w, const Mat4 &local, Vec3 p, double back) {
	const double k = std::clamp(back / kWakeStep, 0.0, static_cast<double>(w.frames.size() - 1));
	const std::size_t i = std::min(w.frames.size() - 1, static_cast<std::size_t>(k)), j = std::min(w.frames.size() - 1, i + 1);
	const Vec3 a = transformPoint(mul(w.frames[i], scale(w.scale), local), p), b = transformPoint(mul(w.frames[j], scale(w.scale), local), p);
	return lerp(a, b, k - static_cast<double>(i));
}

// A chain of n+1 points from `root` (body space at scale 1, before the kind's own lean `local`), `length` long (scale 1), lying in `rest` (the same space: its way when
// still) when the root is still and along the root's way when it moves; a wave `sway` (a share of its length) runs down it. The points come back in root's space, for the
// kind to draw with the rest of it (through m.frame, its scale and `local`).
inline std::vector<Vec3> chain(const Wake &w, const Mat4 &local, Vec3 root, Vec3 rest, double len, int n, double sway, int seed) {
	if (w.frames.empty()) {   // (nowhere it has been: its rest shape, a gentle wave)
		std::vector<Vec3> out;
		for (int i = 0; i <= n; ++i) out.push_back(root + normalize(rest) * (len * i / n) + Vec3{sway * len * std::sin(1.3 * w.t - 4.0 * i / n + seed), 0.0, 0.0} * (static_cast<double>(i) / n));
		return out;
	}
	const Mat4 now = mul(w.frames[0], scale(w.scale), local), back = invertAffine(now);
	const Vec3 r0 = transformPoint(now, root);
	const Vec3 r1 = pastPoint(w, local, root, kWakeStep);
	const double speed = length(r0 - r1) / kWakeStep, L = len * w.scale;
	const double moving = smoothstep(0.08 * w.scale, 0.5 * w.scale, speed);
	Vec3 restW = transformPoint(now, root + normalize(rest)) - r0;
	restW = normalize(restW);
	const Vec3 drift = w.wet ? w.water : w.wind * 0.12;   // (the current carries it; a wind blows a little of it aside)
	const double span = kWakeStep * (w.frames.size() - 1);
	std::vector<Vec3> out;
	Vec3 last = r0;
	for (int i = 0; i <= n; ++i) {
		const double s = L * i / n, u = static_cast<double>(i) / n;
		Vec3 still = r0 + restW * s + drift * (0.35 * u * L);
		Vec3 trailing = still;
		if (moving > 0.0 && speed > 1e-6) {
			const double ago = s / speed;
			trailing = ago <= span ? pastPoint(w, local, root, ago) : pastPoint(w, local, root, span) + normalize(pastPoint(w, local, root, span) - pastPoint(w, local, root, span - kWakeStep) + Vec3{0.0, 1e-9, 0.0}) * ((ago - span) * speed);
			trailing = trailing + restW * (0.15 * s);   // (a little of its rest shape kept: hair lifts as it streams)
		}
		Vec3 p = lerp(still, trailing, moving);
		// a wave running down it, across its way
		const Vec3 along = normalize(p - last + restW * 1e-6);
		Vec3 across = cross(along, Vec3{0.0, 1.0, 0.0});
		if (length(across) < 1e-3) across = cross(along, Vec3{1.0, 0.0, 0.0});
		across = normalize(across);
		const double f = w.wet ? 0.6 : 1.4 + 2.0 * std::min(1.0, length(w.wind) / 6.0);   // (slow in the water; a flutter in a wind)
		p = p + across * (sway * L * u * std::sin(2.0 * kPi * f * w.t - 5.0 * u + 1.7 * seed)) + Vec3{0.0, 0.5, 0.0} * (sway * L * u * 0.4 * std::cos(2.0 * kPi * 0.7 * f * w.t - 4.0 * u + seed));
		if (i > 0) p = last + normalize(p - last + Vec3{0.0, 1e-9, 0.0}) * (L / n);   // (each link its length: it does not stretch)
		out.push_back(transformPoint(back, p));
		last = p;
	}
	return out;
}

// A glow that is seen but is no light to aim at (pbrt "bool sampled" false): lit windows, lamps, spots of light by the thousand would each be a light every scatter in the
// water must choose among (a town's still took an hour on the processor) - what lights the scene round them is a few point lights of their own.
inline std::string glowAttr(const std::string &shapes, Vec3 L) {
	char buf[220];
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  AreaLightSource \"diffuse\" \"rgb L\" [ %.4f %.4f %.4f ] \"bool twosided\" true \"bool sampled\" false\n  Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]\n", L.x, L.y, L.z);
	return buf + shapes + "AttributeEnd\n";
}

// ---- garments (`wear`) ----
struct Garment {
	std::string who, kind = "scarf";
	Vec3 colour{0.8, 0.15, 0.15};
	double length = 0.0, width = 0.0;   // (0: the kind's own, by the wearer's height)
};
inline const std::vector<std::string> &garments() {
	static const std::vector<std::string> k = {"scarf", "cape", "veil", "ribbon", "sash"};
	return k;
}
// A garment at a moment as scene text, on a character whose kind has its head and chest at `head`, `chest` (body space, scale 1) and is H tall; `w` where it has been,
// `local` its kind's own lean of its body (and a swimmer laid level), `seed` its own number.
inline std::string garmentText(const Garment &g, Vec3 head, Vec3 chest, double H, const Wake &w, const Mat4 &local, int seed) {
	if (w.frames.empty()) return std::string();
	const Vec3 neck = head + Vec3{0.0, -0.09 * H, -0.03 * H};
	const double len = g.length > 0.0 ? g.length / w.scale : (g.kind == "cape" ? 0.48 : g.kind == "veil" ? 0.5 : g.kind == "sash" ? 0.35 : 0.6) * H;
	const double wide = g.width > 0.0 ? g.width / w.scale : (g.kind == "cape" ? 0.24 : g.kind == "veil" ? 0.16 : g.kind == "ribbon" ? 0.03 : 0.06) * H;
	const Vec3 rest = w.wet ? Vec3{0.0, 0.45, -1.0} : Vec3{0.0, -1.0, -0.18};   // (it hangs; under the water it floats up and back)
	const int n = 10;
	std::vector<Vec3> left, right;
	const auto strand = [&](Vec3 root, double sway, int s) { return chain(w, local, root, rest, len, n, sway, s); };
	if (g.kind == "cape") {   // across the shoulders
		left = strand(neck + Vec3{0.5 * wide, -0.02 * H, -0.05 * H}, 0.05, seed);
		right = strand(neck + Vec3{-0.5 * wide, -0.02 * H, -0.05 * H}, 0.05, seed + 1);
		for (int i = 0; i <= n; ++i) {   // (its hem wider than its collar)
			const double u = static_cast<double>(i) / n;
			const Vec3 mid = (left[static_cast<std::size_t>(i)] + right[static_cast<std::size_t>(i)]) * 0.5, half = (left[static_cast<std::size_t>(i)] - right[static_cast<std::size_t>(i)]) * 0.5;
			left[static_cast<std::size_t>(i)] = mid + half * (1.0 + 0.5 * u), right[static_cast<std::size_t>(i)] = mid - half * (1.0 + 0.5 * u);
		}
	} else {
		const Vec3 root = g.kind == "veil" ? head + Vec3{0.0, 0.06 * H, -0.05 * H} : g.kind == "sash" ? chest + Vec3{0.08 * H, -0.02 * H, -0.06 * H} : neck + Vec3{0.03 * H, 0.0, -0.05 * H};
		const std::vector<Vec3> mid = strand(root, g.kind == "ribbon" ? 0.12 : 0.08, seed);
		for (std::size_t i = 0; i < mid.size(); ++i) {
			const Vec3 along = mid[std::min(i + 1, mid.size() - 1)] - mid[i == 0 ? 0 : i - 1];
			Vec3 across = cross(along, Vec3{0.0, 0.0, 1.0});
			if (length(across) < 1e-6) across = {1.0, 0.0, 0.0};
			across = normalize(across);
			const double u = static_cast<double>(i) / (mid.size() - 1), wi = wide * (g.kind == "veil" ? 0.6 + 1.2 * u : 1.0);
			left.push_back(mid[i] + across * (0.5 * wi)), right.push_back(mid[i] - across * (0.5 * wi));
		}
	}
	const Mesh cloth = strip(left, right);
	return "# " + g.kind + " worn by its character (film_flow.h)\n" + attr(diffuseMaterial(g.colour), emitMesh(cloth, mul(w.frames[0], scale(w.scale), local)));
}

}  // namespace flow
}  // namespace cartoon
