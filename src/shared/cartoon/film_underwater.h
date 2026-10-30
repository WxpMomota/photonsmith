#pragma once
// film_underwater.h -- under the sea for a film file (film_file.h `underwater`, `kelp`, `coral`): the water round the camera, the light coming down through it, and what grows
// on the seabed.
//
//   the water     the camera's own medium (MediumInterface before the Camera line), ended by a sphere 3 km across whose top is the surface (so the water is everything under
//                 the surface, near enough, and the sun and sky above light it down through its depth): it absorbs red
//                 fastest and blue slowest (so the far blues out and the near keeps its colours), and scatters forwards a little (`clarity` stretches how far you see);
//   the surface   the sea's waves seen from below, as a thin water surface (it lets the sun's light through: a thick one would leave the depths unlit);
//   the light     caustics - the rippling net of light the waves focus onto the seabed - as a decal on the seabed round the camera (sand times a picture of the net, a loop of
//                 24 pictures that repeats every 2.4 s, tiled every 4 m), and shafts of light down through the water from a projector above the camera shining the same net
//                 (pbrt's "projection" light; the medium makes its beams visible);
//   marine snow   flecks drifting slowly down round the camera;
//   kelp, coral   kelp: stipes rising from the seabed with blades along them, swaying with the swell; coral: lumpy brain corals, branching corals and tube sponges in bright
//                 colours, placed on the seabed.
//
// Coordinates are pbrt's, as a film file's; meshes are built in story space (x mirrored).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_cache.h"
#include "film_caustics.h"
#include "film_sky.h"
#include "nature_lib.h"

namespace cartoon {
namespace underwater {

struct Water {
	double clarity = 1.0;   // how far you see: 1 a clear sea (about 25 m), 2 tropical, 0.5 a murky one
	std::string colour = "blue";
	double lamp = 0.0;      // (round 14 `lamp L`: a diver's lamp on the camera, film_undersea.h)
	bool ripples = false;   // (`ripples`: the sand of the seabed in ripples)
};
// The current (round 14 `current SPEED toward A [surge P]`): the water's flow, pbrt's metres a second, the same everywhere; with a surge it swings back and forth every P
// seconds (as much as SPEED each way) about a fifth of SPEED onward. `drift(t)` is how far the water has moved by t (the flow's integral), for what drifts in it.
struct Current {
	bool on = false;
	double speed = 0.0, toward = 0.0, surge = 0.0;
	Vec3 dir() const { return {std::sin(toward * kPi / 180.0), 0.0, std::cos(toward * kPi / 180.0)}; }
	Vec3 velocity(double t) const {
		if (!on) return {0.0, 0.0, 0.0};
		return dir() * (surge > 0.0 ? 0.2 * speed + speed * std::sin(2.0 * kPi * t / surge) : speed);
	}
	Vec3 drift(double t) const {
		if (!on) return {0.0, 0.0, 0.0};
		return dir() * (surge > 0.0 ? 0.2 * speed * t - speed * surge / (2.0 * kPi) * (std::cos(2.0 * kPi * t / surge) - 1.0) : speed * t);
	}
};
struct Kelp {
	int count = 40;
	double x0 = -20.0, z0 = -20.0, x1 = 20.0, z1 = 20.0, height = 8.0;
	int seed = 1;
};
struct Coral {
	int count = 30;
	double x0 = -15.0, z0 = -15.0, x1 = 15.0, z1 = 15.0;
	int seed = 1;
};
inline const std::vector<std::string> &colours() {
	static const std::vector<std::string> k = {"blue", "green", "teal"};
	return k;
}

// The water's absorption per metre (red goes first) and its scattering (the blue glow of the water all round), for its colour and clarity.
inline Vec3 absorption(const Water &w) {
	const Vec3 a = w.colour == "green" ? Vec3{0.20, 0.06, 0.12} : w.colour == "teal" ? Vec3{0.17, 0.05, 0.07} : Vec3{0.16, 0.05, 0.03};
	return a * (1.0 / std::max(0.1, w.clarity));
}
inline Vec3 scattering(const Water &w) {
	const Vec3 s = w.colour == "green" ? Vec3{0.04, 0.07, 0.04} : Vec3{0.03, 0.055, 0.07};
	return s * (1.0 / std::max(0.1, w.clarity));
}
// The water round the camera: what goes before the Camera line (the camera in it unless `cameraInside` is false: a camera at or above the surface), and the sphere it ends
// at (story-space eye).
inline std::string beforeCamera(const Water &w, bool cameraInside = true) {
	const Vec3 a = absorption(w), s = scattering(w);
	char buf[360];
	std::snprintf(buf, sizeof buf, "MakeNamedMedium \"sea\" \"string type\" [ \"homogeneous\" ] \"rgb sigma_a\" [ %.5f %.5f %.5f ] \"rgb sigma_s\" [ %.5f %.5f %.5f ] \"float g\" [ 0.55 ]\n%s",
				  a.x, a.y, a.z, s.x, s.y, s.z, cameraInside ? "MediumInterface \"\" \"sea\"\n" : "");
	return buf;
}
// (the sphere the water ends at: centred at `centreStory` (story space). The film puts a very large one with its top at the surface: the half-space under it, near enough.)
inline std::string boundary(const Vec3 &eyeStory, double radius) {
	char buf[240];
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  MediumInterface \"sea\" \"\"\n  Material \"interface\"\n  Translate %.4f %.4f %.4f\n  Shape \"sphere\" \"float radius\" [ %.3f ]\nAttributeEnd\n", -eyeStory.x,
				  eyeStory.y, eyeStory.z, radius);
	return buf;
}

// ---- the light ----
// The net of light at (u, v) (it repeats over 0 to 1 each way) at frame k of the loop (24 of them over 2.4 s): two layers of moving cells, bright where a cell's nearest and
// second-nearest centres are equally far (the cells' edges); 0 dark to about 1 on the brightest lines.
inline double causticAt(double u, double v, int k) {
	const double ph = 2.0 * kPi * k / 24.0;
	const auto layer = [&](double u, double v, int cells, int salt, double scale) {
		const double x = u * cells, y = v * cells;
		const int cx = static_cast<int>(std::floor(x)), cy = static_cast<int>(std::floor(y));
		double f1 = 1e9, f2 = 1e9;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx) {
				const int gx = ((cx + dx) % cells + cells) % cells, gy = ((cy + dy) % cells + cells) % cells;   // (wrapped: the picture tiles)
				const double a = 2.0 * kPi * hash01(gx * 131 + gy, salt), r = 0.25 + 0.15 * hash01(gx * 17 + gy, salt + 1);
				const double turn = ph * (hash01(gx + gy * 7, salt + 2) < 0.5 ? 1.0 : -1.0);   // (each centre circles once a loop)
				const double px = cx + dx + 0.5 + r * std::cos(a + turn), py = cy + dy + 0.5 + r * std::sin(a + turn);
				const double d = std::hypot(x - px, y - py);
				if (d < f1) f2 = f1, f1 = d;
				else if (d < f2) f2 = d;
			}
		return std::pow(1.0 - smoothstep(0.0, 0.18 * scale, f2 - f1), 2.0);
	};
	return 0.55 * layer(u, v, 6, 11, 1.0) + 0.45 * layer(u + 0.37, v + 0.21, 9, 23, 0.8);
}
// Sand ripples (round 14 `ripples`) at (u, v) of a 4 m tile: 32 crests across it, wandering a little, each with a long gentle side towards the light and a short steep
// one away from it - as the light finds them, about 0.81 to 1.04.
inline double rippleShade(double u, double v) {
	const double w = u + 0.018 * std::sin(2.0 * kPi * 2.0 * v) + 0.009 * std::sin(2.0 * kPi * (5.0 * v + u)) + 0.004 * std::sin(2.0 * kPi * (13.0 * v - 2.0 * u));
	const double s = w * 32.0 - std::floor(w * 32.0);
	const double lit = s < 0.72 ? 0.07 : -0.16;   // (the gentle side a little brighter, the steep one in shade)
	return 0.97 + lit * (1.0 - std::exp(-12.0 * std::min(s, 1.0 - s)));
}
// The net as a tiling picture at `size` pixels (on rippled sand with `ripples`). `band` (round 14) fades it with the seabed's depth - the waves' focus blurs out on its way
// down: 0 the full net (a bed less than 10 m down), 1 and 2 fainter (20 m, 35 m), 3 none (deeper: plain sand).
inline double causticContrast(int band) { return band <= 0 ? 1.0 : band == 1 ? 0.6 : band == 2 ? 0.3 : 0.0; }
inline int causticBand(double bedDepth) { return bedDepth < 10.0 ? 0 : bedDepth < 20.0 ? 1 : bedDepth < 35.0 ? 2 : 3; }
inline std::string causticPicture(const std::string &film, int k, int size, bool ripples = false, int band = 0) {
	char name[64];
	if (band >= 3) k = 0;   // (no net: one picture for every frame)
	std::snprintf(name, sizeof name, "caustic_%d_%02d%s%s", size, k, ripples ? "_rip" : "", band > 0 ? (band == 1 ? "_b1" : band == 2 ? "_b2" : "_b3") : "");
	const double contrast = causticContrast(band);
	return film_sky::picture(film, name, size, size, [&](std::vector<float> &rgb) {
		for (int j = 0; j < size; ++j)
			for (int i = 0; i < size; ++i) {
				const double net = contrast > 0.0 ? 0.8 + contrast * (0.35 + 1.9 * causticAt((i + 0.5) / size, (j + 0.5) / size, k) - 0.8) : 0.8;
				const float val = static_cast<float>(net * (ripples ? rippleShade((i + 0.5) / size, (j + 0.5) / size) : 1.0));
				const std::size_t q = (static_cast<std::size_t>(j) * size + i) * 3;
				rgb[q] = rgb[q + 1] = rgb[q + 2] = val;
			}
	});
}
inline int causticFrame(double t) { return static_cast<int>(std::floor(t / 0.1)) % 24; }

// The caustics on the seabed round the camera's ground point (pbrt's x z), `groundAt` the seabed's height: a decal of sand coloured by the net of light, tiled every 4 m.
// `tracedDepth` (0 or more: `caustics traced`, film_caustics.h): the net traced from the surface for a bed that deep, instead of the painted one.
// `bedDepth` (round 14): how deep the bed under the camera is - the painted net fades out deeper (causticBand).
inline std::string causticText(const std::string &film, double t, double cx, double cz, const std::function<double(double, double)> &groundAt, double tracedDepth = -1.0, bool ripples = false,
							   double bedDepth = 0.0) {
	const std::string picture = tracedDepth >= 0.0 ? caustics::picture(film, causticFrame(t), 512, tracedDepth) : causticPicture(film, causticFrame(t), 512, ripples, causticBand(bedDepth));
	Mesh m;
	const int n = 64;
	const double half = 40.0, x0 = std::floor((cx - half) / 4.0) * 4.0, z0 = std::floor((cz - half) / 4.0) * 4.0, step = 2.0 * half / n;
	for (int j = 0; j <= n; ++j)
		for (int i = 0; i <= n; ++i) {
			const double x = x0 + i * step, z = z0 + j * step;
			m.verts.push_back({-x, groundAt(x, z) + 0.04, z});
			const double e = 0.5;
			m.normals.push_back(normalize(Vec3{-(groundAt(x - e, z) - groundAt(x + e, z)), 2.0 * e, groundAt(x, z - e) - groundAt(x, z + e)}));
			m.uvs.push_back({x / 4.0, z / 4.0});
		}
	for (int j = 0; j < n; ++j)
		for (int i = 0; i < n; ++i) {
			const int a = j * (n + 1) + i;
			m.tris.push_back({a, a + 1, a + n + 1});
			m.tris.push_back({a + 1, a + n + 2, a + n + 1});
		}
	return "# caustics on the seabed (film_underwater.h)\nAttributeBegin\n  Texture \"caustic\" \"float\" \"imagemap\" \"string filename\" [ \"" + picture +
		   "\" ]\n  Texture \"caustic_sand\" \"spectrum\" \"scale\" \"texture tex\" \"caustic\" \"rgb scale\" [ 0.5 0.44 0.32 ]\n  Material \"diffuse\" \"texture reflectance\" \"caustic_sand\"\n" +
		   emitMesh(m, identity()) + "AttributeEnd\n";
}
// The shafts of light: a projector above the camera (pbrt's eye) shining the net down through the water.
inline std::string shaftsText(const std::string &film, double t, const Vec3 &eye, double surface, double tracedDepth = -1.0) {
	const std::string picture = tracedDepth >= 0.0 ? caustics::picture(film, causticFrame(t), 64, tracedDepth) : causticPicture(film, causticFrame(t), 64);
	char buf[256];
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.3f %.3f %.3f\n  Rotate 90 1 0 0\n", eye.x, surface + 2.0, eye.z);
	return "# light shafts (film_underwater.h)\n" + std::string(buf) + "  LightSource \"projection\" \"float fov\" [ 70 ] \"string filename\" [ \"" + picture + "\" ] \"float scale\" [ 2.5 ]\nAttributeEnd\n";
}

// ---- marine snow ----
// Flecks drifting down round the camera (story-space eye): a fixed pattern in tiles 6 m square (the same in each, so it stands still in the water), sinking slowly.
// `drift` (pbrt's): how far the current has carried the water by t (the flecks go with it); none above `ceiling` (pbrt's y: the surface, for a camera at it).
inline std::string snowText(double t, const Vec3 &eyeStory, const Vec3 &drift = Vec3{0.0, 0.0, 0.0}, double ceiling = 1e30) {
	Mesh m;
	const Mesh fleck = uvSphere(1.0, 4, 3);
	const double tile = 6.0;
	const Vec3 carried{-drift.x, drift.y, drift.z};
	const Vec3 look = eyeStory - carried;   // (the pattern moves with the water: look it up where the water came from)
	const int tx = static_cast<int>(std::floor(look.x / tile)), ty = static_cast<int>(std::floor(look.y / tile)), tz = static_cast<int>(std::floor(look.z / tile));
	for (int a = -1; a <= 1; ++a)
		for (int b = -1; b <= 1; ++b)
			for (int c = -1; c <= 1; ++c)
				for (int k = 0; k < 40; ++k) {
					const Vec3 base{(tx + a + hash01(k, 1)) * tile, (ty + b + hash01(k, 2)) * tile, (tz + c + hash01(k, 3)) * tile};
					const double sink = std::fmod(t * 0.03 + hash01(k, 4) * tile, tile);
					Vec3 p = base - Vec3{0.0, sink, 0.0} + Vec3{0.2 * std::sin(t * 0.3 + k), 0.0, 0.2 * std::cos(t * 0.25 + 2.0 * k)} + carried;
					if (length(p - eyeStory) > 9.0 || length(p - eyeStory) < 0.3 || p.y > ceiling) continue;
					const double r = 0.006 + 0.012 * hash01(k, 5);
					const int base0 = static_cast<int>(m.verts.size());
					for (std::size_t i = 0; i < fleck.verts.size(); ++i) {
						m.verts.push_back(p + fleck.verts[i] * r);
						m.normals.push_back(fleck.normals[i]);
					}
					for (const auto &tr : fleck.tris) m.tris.push_back({tr[0] + base0, tr[1] + base0, tr[2] + base0});
				}
	return m.tris.empty() ? std::string() : "# marine snow (film_underwater.h)\n" + attr(diffuseMaterial({0.8, 0.82, 0.8}), emitMesh(m, identity()));
}

// ---- kelp and coral ----
// A kelp forest at t: each stipe rising from the seabed in a gentle S, swaying with the swell (its top furthest), blades along it.
// `flow` (pbrt's, metres a second: round 14's current): the stipes lean down-current, the more the higher up them.
inline std::string kelpText(const Kelp &k, double t, const std::function<double(double, double)> &groundAt, const Vec3 &flow = Vec3{0.0, 0.0, 0.0}) {
	Mesh m;
	for (int i = 0; i < k.count; ++i) {
		const int s = k.seed * 977 + i;
		const double x = k.x0 + (k.x1 - k.x0) * hash01(s, 1), z = k.z0 + (k.z1 - k.z0) * hash01(s, 2);
		const double h = k.height * (0.6 + 0.6 * hash01(s, 3)), phase = 2.0 * kPi * hash01(s, 4);
		const Vec3 root{-x, groundAt(x, z), z};
		const Vec3 sway = Vec3{0.6 * std::sin(t * 0.7 + phase + x * 0.05), 0.0, 0.4 * std::cos(t * 0.6 + phase)} + Vec3{-flow.x, 0.0, flow.z} * 6.0;
		const auto stem = [&](double u) { return root + Vec3{0.0, h * u, 0.0} + sway * (u * u * h * 0.25) + Vec3{0.3 * std::sin(u * 5.0 + phase), 0.0, 0.0}; };
		const Vec3 side = normalize(Vec3{std::cos(phase), 0.0, std::sin(phase)});
		nature::addStrip(m, stem, [](double u) { return 0.05 * (1.0 - 0.6 * u); }, side, 10);
		for (int b = 0; b < 14; ++b) {   // blades, alternating sides, trailing with the sway
			const double u = 0.15 + 0.8 * b / 14.0;
			const Vec3 at = stem(u), dir = normalize(Vec3{(b % 2 ? 1.0 : -1.0) * side.x, 0.9, (b % 2 ? 1.0 : -1.0) * side.z} + sway * 0.4);
			nature::addLeaf(m, at, dir, normalize(cross(dir, Vec3{0, 1, 0}) + Vec3{0, 0.01, 0}), 0.9 + 0.5 * hash01(s + b, 6), 0.22);
		}
	}
	return m.tris.empty() ? std::string() : "# kelp (film_underwater.h)\n" + attr(translucentMaterial({0.24, 0.2, 0.06}, {0.22, 0.2, 0.05}), emitMesh(m, identity()));
}
// Corals on the seabed: brain corals, branching corals and tube sponges, each its own bright colour (the same every frame).
inline std::string coralText(const Coral &c, const std::function<double(double, double)> &groundAt) {
	static const Vec3 col[6] = {{0.85, 0.35, 0.15}, {0.8, 0.3, 0.5}, {0.55, 0.25, 0.7}, {0.9, 0.75, 0.2}, {0.3, 0.6, 0.55}, {0.85, 0.5, 0.45}};
	std::string out = "# coral (film_underwater.h)\n";
	for (int i = 0; i < c.count; ++i) {
		const int s = c.seed * 613 + i;
		const double x = c.x0 + (c.x1 - c.x0) * hash01(s, 1), z = c.z0 + (c.z1 - c.z0) * hash01(s, 2);
		const Vec3 foot{-x, groundAt(x, z), z};
		const double size = 0.4 + 0.8 * hash01(s, 3);
		const int kind = static_cast<int>(hash01(s, 4) * 3.0);
		Mesh m;
		if (kind == 0) {   // a brain coral: a lumpy dome
			m = nature::rock(size * 1.4, s);
			for (Vec3 &v : m.verts) v = v + Vec3{0.0, 0.1 * std::sin(v.x * 20.0) * std::sin(v.z * 20.0) * size, 0.0};
		} else if (kind == 1) {   // a branching coral: forks rising and spreading
			std::function<void(Vec3, Vec3, double, double, int)> grow = [&](Vec3 a, Vec3 d, double len, double r, int depth) {
				const Vec3 b = a + d * len;
				const Mesh tube = lathe({{r, 0.0}, {r * 0.75, len}, {0.0, len + r * 0.5}}, 6);
				const Mat4 tr = alignY(a, d);
				const int base0 = static_cast<int>(m.verts.size());
				for (std::size_t q = 0; q < tube.verts.size(); ++q) {
					m.verts.push_back(transformPoint(tr, tube.verts[q]));
					m.normals.push_back(tube.normals[q]);
				}
				for (const auto &tt : tube.tris) m.tris.push_back({tt[0] + base0, tt[1] + base0, tt[2] + base0});
				if (depth == 0) return;
				for (int k = 0; k < 2 + depth % 2; ++k) {
					const double a2 = 2.0 * kPi * hash01(s * 7 + depth * 13 + k, 5);
					grow(b, normalize(d + Vec3{0.6 * std::cos(a2), 0.3, 0.6 * std::sin(a2)}), len * 0.75, r * 0.7, depth - 1);
				}
			};
			grow({0, 0, 0}, {0, 1, 0}, 0.35 * size, 0.05 * size, 4);
		} else {   // tube sponges: a few hollow-looking columns
			for (int k = 0; k < 3 + s % 3; ++k) {
				const double a2 = 2.0 * kPi * k / 5.0, h = size * (0.6 + 0.8 * hash01(s + k, 6));
				const Mesh tube = lathe({{0.08 * size, 0.0}, {0.11 * size, h}, {0.07 * size, h}, {0.06 * size, h * 0.85}}, 10);
				const int base0 = static_cast<int>(m.verts.size());
				for (std::size_t q = 0; q < tube.verts.size(); ++q) {
					m.verts.push_back(tube.verts[q] + Vec3{0.18 * size * std::cos(a2), 0.0, 0.18 * size * std::sin(a2)});
					m.normals.push_back(tube.normals[q]);
				}
				for (const auto &tt : tube.tris) m.tris.push_back({tt[0] + base0, tt[1] + base0, tt[2] + base0});
			}
		}
		out += attr(diffuseMaterial(col[(s / 7) % 6]), emitMesh(m, mul(translate(foot), rotate({0, 1, 0}, 360.0 * hash01(s, 9)))));
	}
	return out;
}

}  // namespace underwater
}  // namespace cartoon
