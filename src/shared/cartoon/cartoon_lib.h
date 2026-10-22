#pragma once
// cartoon_lib.h -- the toolkit behind the cartoon video presets (src/shared/video_preset.h): a short film is a function of time that returns the complete pbrt-v4 scene for that
// instant. This header supplies what such a function needs: keyframe tracks with easing, 3 x 3 / 4 x 4 transforms, triangle meshes (surfaces of revolution, spheres, boxes) written as
// pbrt `trianglemesh` text, and the small text helpers around them. Header-only, plain C++17, no renderer dependency (the launcher writes each frame's text to a file and renders it
// like any other .pbrt scene). This is the only implementation of the toolkit: scripts/anim/film.py drives ray_tracer to look at a film's stills, contact sheet and video.

#include <array>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace cartoon {

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Vectors
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
struct Vec3 {
	double x = 0.0, y = 0.0, z = 0.0;
};
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
	const double n = length(a);
	return n > 1e-12 ? a * (1.0 / n) : Vec3{0.0, 1.0, 0.0};
}
inline double clamp01(double u) { return u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u); }
inline double lerp(double a, double b, double u) { return a + (b - a) * u; }
inline Vec3 lerp(Vec3 a, Vec3 b, double u) { return {lerp(a.x, b.x, u), lerp(a.y, b.y, u), lerp(a.z, b.z, u)}; }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Easing and keyframe tracks
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
enum class Ease { Linear, Smooth, In, Out, Back, Elastic };

inline double applyEase(Ease e, double u) {
	switch (e) {
		case Ease::Linear: return u;
		case Ease::Smooth: return u * u * (3.0 - 2.0 * u);
		case Ease::In: return u * u;
		case Ease::Out: return 1.0 - (1.0 - u) * (1.0 - u);
		case Ease::Back: {   // overshoots the target a little, then settles: anticipation and follow-through
			const double c1 = 1.70158, c3 = c1 + 1.0, v = u - 1.0;
			return 1.0 + c3 * v * v * v + c1 * v * v;
		}
		case Ease::Elastic:
			if (u <= 0.0) return 0.0;
			if (u >= 1.0) return 1.0;
			return std::pow(2.0, -9.0 * u) * std::sin((u * 10.0 - 0.75) * (2.0 * kPi / 3.0)) + 1.0;
	}
	return u;
}

// Keyframes: the value at t blends from the key before to the key after with the EASE OF THE KEY AFTER (the ease says how the motion arrives). Before the first key it holds the
// first value, after the last it holds the last.
template <typename T>
class Track {
  public:
	struct Key {
		double t;
		T value;
		Ease ease;
	};
	// (default ease for keys written as {t, value})
	Track(std::initializer_list<std::pair<double, T>> keys, Ease ease = Ease::Smooth) {
		for (const auto &k : keys) keys_.push_back({k.first, k.second, ease});
	}
	// Keys that each say their own ease: Track::withEases({{0.0, 0.0, Ease::Smooth}, {1.0, 1.0, Ease::Back}}).
	static Track withEases(std::vector<Key> keys) {
		Track t;
		t.keys_ = std::move(keys);
		return t;
	}
	T operator()(double t) const {
		if (t <= keys_.front().t) return keys_.front().value;
		if (t >= keys_.back().t) return keys_.back().value;
		for (std::size_t i = 1; i < keys_.size(); ++i) {
			if (t <= keys_[i].t) {
				const Key &a = keys_[i - 1], &b = keys_[i];
				const double u = b.t > a.t ? (t - a.t) / (b.t - a.t) : 1.0;
				return lerp(a.value, b.value, applyEase(b.ease, clamp01(u)));
			}
		}
		return keys_.back().value;
	}

  private:
	Track() = default;
	std::vector<Key> keys_;
};
using Track1 = Track<double>;
using Track3 = Track<Vec3>;

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// 4 x 4 matrices (row-major, points are columns: p' = M p)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
using Mat4 = std::array<double, 16>;

inline Mat4 identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }
inline Mat4 mul(const Mat4 &a, const Mat4 &b) {
	Mat4 r{};
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j) {
			double s = 0.0;
			for (int k = 0; k < 4; ++k) s += a[static_cast<std::size_t>(i * 4 + k)] * b[static_cast<std::size_t>(k * 4 + j)];
			r[static_cast<std::size_t>(i * 4 + j)] = s;
		}
	return r;
}
// Any number of matrices: mul(a, b, c, d, ...) = a * b * c * d * ...
template <typename... Rest>
inline Mat4 mul(const Mat4 &a, const Mat4 &b, const Mat4 &c, const Rest &...rest) {
	return mul(mul(a, b), c, rest...);
}
inline Mat4 translate(Vec3 v) { return {1, 0, 0, v.x, 0, 1, 0, v.y, 0, 0, 1, v.z, 0, 0, 0, 1}; }
inline Mat4 translate(double x, double y, double z) { return translate(Vec3{x, y, z}); }
inline Mat4 scale(double x, double y, double z) { return {x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, 0, 0, 0, 1}; }
inline Mat4 scale(double s) { return scale(s, s, s); }
inline Mat4 rotate(Vec3 axis, double degrees) {
	const Vec3 a = normalize(axis);
	const double c = std::cos(degrees * kPi / 180.0), s = std::sin(degrees * kPi / 180.0), t = 1.0 - c;
	return {t * a.x * a.x + c, t * a.x * a.y - s * a.z, t * a.x * a.z + s * a.y, 0,
			t * a.x * a.y + s * a.z, t * a.y * a.y + c, t * a.y * a.z - s * a.x, 0,
			t * a.x * a.z - s * a.y, t * a.y * a.z + s * a.x, t * a.z * a.z + c, 0,
			0, 0, 0, 1};
}
inline Vec3 transformPoint(const Mat4 &m, Vec3 p) {
	return {m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7], m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]};
}
// The inverse of a matrix whose bottom row is 0 0 0 1 (translations, rotations, scales).
inline Mat4 invertAffine(const Mat4 &m) {
	const double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
	const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
	const double v[9] = {(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det, (f * g - d * i) / det, (a * i - c * g) / det,
						 (c * d - a * f) / det, (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det};
	const double tx = m[3], ty = m[7], tz = m[11];
	return {v[0], v[1], v[2], -(v[0] * tx + v[1] * ty + v[2] * tz),
			v[3], v[4], v[5], -(v[3] * tx + v[4] * ty + v[5] * tz),
			v[6], v[7], v[8], -(v[6] * tx + v[7] * ty + v[8] * tz),
			0, 0, 0, 1};
}
// A rigid frame at `origin` whose local +Y is `ydir` (the other axes are picked from `hint`): what places a lathe-built part along a direction.
inline Mat4 alignY(Vec3 origin, Vec3 ydir, Vec3 hint = {0.0, 0.0, 1.0}) {
	const Vec3 ya = normalize(ydir);
	Vec3 xa = cross(hint, ya);
	if (length(xa) < 1e-6) xa = cross(Vec3{1.0, 0.0, 0.0}, ya);
	xa = normalize(xa);
	const Vec3 za = cross(xa, ya);
	return {xa.x, ya.x, za.x, origin.x, xa.y, ya.y, za.y, origin.y, xa.z, ya.z, za.z, origin.z, 0, 0, 0, 1};
}
inline Mat4 transposeRotation(const Mat4 &m) { return {m[0], m[4], m[8], 0, m[1], m[5], m[9], 0, m[2], m[6], m[10], 0, 0, 0, 0, 1}; }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Triangle meshes
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
struct Mesh {
	std::vector<Vec3> verts, normals;
	std::vector<std::pair<double, double>> uvs;
	std::vector<std::array<int, 3>> tris;

	// The mesh through `m` and appended to `out` as a pbrt trianglemesh. Normals use the inverse transpose, so a squashed or stretched mesh keeps correct shading; `flip` reverses
	// the triangle winding (for a mirroring transform).
	void appendPbrt(std::string &out, const Mat4 &m, bool flip) const {
		const Mat4 inv = invertAffine(m);
		char buf[160];
		out += "Shape \"trianglemesh\" \"integer indices\" [ ";
		for (const auto &t : tris) {
			std::snprintf(buf, sizeof(buf), "%d %d %d ", t[0], flip ? t[2] : t[1], flip ? t[1] : t[2]);
			out += buf;
		}
		out += "]\n  \"point3 P\" [ ";
		for (const Vec3 &v : verts) {
			const Vec3 p = transformPoint(m, v);
			std::snprintf(buf, sizeof(buf), "%.5f %.5f %.5f  ", p.x, p.y, p.z);
			out += buf;
		}
		out += "]\n  \"normal N\" [ ";
		for (const Vec3 &n : normals) {
			const Vec3 w = normalize({n.x * inv[0] + n.y * inv[4] + n.z * inv[8], n.x * inv[1] + n.y * inv[5] + n.z * inv[9], n.x * inv[2] + n.y * inv[6] + n.z * inv[10]});
			std::snprintf(buf, sizeof(buf), "%.5f %.5f %.5f  ", w.x, w.y, w.z);
			out += buf;
		}
		out += "]\n";
		if (!uvs.empty()) {
			out += "  \"point2 uv\" [ ";
			for (const auto &uv : uvs) {
				std::snprintf(buf, sizeof(buf), "%.5f %.5f  ", uv.first, uv.second);
				out += buf;
			}
			out += "]\n";
		}
	}
};

// A surface of revolution about local +Y: `profile` is a list of (radius, height) points from bottom to top (radius 0 closes the surface at the axis). Normals come from the
// profile's slope, smoothed along it; uv = (angle / 2 pi, position along the profile).
inline Mesh lathe(const std::vector<std::pair<double, double>> &profile, int segments = 48) {
	const int n = static_cast<int>(profile.size());
	std::vector<std::pair<double, double>> pn(static_cast<std::size_t>(n));
	for (int i = 0; i < n; ++i) {
		const auto &a = profile[static_cast<std::size_t>(std::max(i - 1, 0))];
		const auto &b = profile[static_cast<std::size_t>(std::min(i + 1, n - 1))];
		const double dr = b.first - a.first, dy = b.second - a.second;
		const double l = std::hypot(dr, dy) > 0.0 ? std::hypot(dr, dy) : 1.0;
		pn[static_cast<std::size_t>(i)] = {dy / l, -dr / l};
	}
	Mesh mesh;
	for (int j = 0; j <= segments; ++j) {
		const double ang = 2.0 * kPi * j / segments, ca = std::cos(ang), sa = std::sin(ang);
		for (int i = 0; i < n; ++i) {
			const auto &p = profile[static_cast<std::size_t>(i)];
			mesh.verts.push_back({p.first * ca, p.second, p.first * sa});
			mesh.normals.push_back({pn[static_cast<std::size_t>(i)].first * ca, pn[static_cast<std::size_t>(i)].second, pn[static_cast<std::size_t>(i)].first * sa});
			mesh.uvs.push_back({static_cast<double>(j) / segments, static_cast<double>(i) / std::max(n - 1, 1)});
		}
	}
	for (int j = 0; j < segments; ++j)
		for (int i = 0; i < n - 1; ++i) {
			const int a = j * n + i, b = (j + 1) * n + i;
			// (a ring that closes at the axis has two coincident vertices: its triangle has no area and is left out)
			const auto addTri = [&mesh](int p, int q, int r) {
				const Vec3 &vp = mesh.verts[static_cast<std::size_t>(p)];
				if (length(cross(mesh.verts[static_cast<std::size_t>(q)] - vp, mesh.verts[static_cast<std::size_t>(r)] - vp)) > 1e-12) mesh.tris.push_back({p, q, r});
			};
			addTri(a, b, a + 1);
			addTri(b, b + 1, a + 1);
		}
	return mesh;
}

inline Mesh uvSphere(double radius = 1.0, int nu = 48, int nv = 24) {
	std::vector<std::pair<double, double>> profile;
	for (int i = 0; i <= nv; ++i) {
		const double th = kPi * i / nv - kPi / 2.0;
		profile.push_back({radius * std::cos(th), radius * std::sin(th)});
	}
	profile.front() = {0.0, -radius};
	profile.back() = {0.0, radius};
	return lathe(profile, nu);
}

// An axis-aligned box centred on the origin with flat faces.
inline Mesh box(double sx, double sy, double sz) {
	const double hx = sx / 2.0, hy = sy / 2.0, hz = sz / 2.0;
	struct Face {
		Vec3 n;
		Vec3 q[4];
	};
	const Face faces[6] = {{{1, 0, 0}, {{hx, -hy, -hz}, {hx, hy, -hz}, {hx, hy, hz}, {hx, -hy, hz}}},
						   {{-1, 0, 0}, {{-hx, -hy, hz}, {-hx, hy, hz}, {-hx, hy, -hz}, {-hx, -hy, -hz}}},
						   {{0, 1, 0}, {{-hx, hy, -hz}, {-hx, hy, hz}, {hx, hy, hz}, {hx, hy, -hz}}},
						   {{0, -1, 0}, {{-hx, -hy, hz}, {-hx, -hy, -hz}, {hx, -hy, -hz}, {hx, -hy, hz}}},
						   {{0, 0, 1}, {{-hx, -hy, hz}, {hx, -hy, hz}, {hx, hy, hz}, {-hx, hy, hz}}},
						   {{0, 0, -1}, {{hx, -hy, -hz}, {-hx, -hy, -hz}, {-hx, hy, -hz}, {hx, hy, -hz}}}};
	const std::pair<double, double> corner[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
	Mesh mesh;
	for (const Face &f : faces) {
		const int base = static_cast<int>(mesh.verts.size());
		for (int k = 0; k < 4; ++k) {
			mesh.verts.push_back(f.q[k]);
			mesh.normals.push_back(f.n);
			mesh.uvs.push_back(corner[k]);
		}
		mesh.tris.push_back({base, base + 1, base + 2});
		mesh.tris.push_back({base, base + 2, base + 3});
	}
	return mesh;
}

// A box with rounded edges (a book, a block): a subdivided box whose vertices are pushed onto a rounded shape.
inline Mesh roundedBox(double sx, double sy, double sz, double radius, int steps = 4) {
	const double hx = sx / 2.0 - radius, hy = sy / 2.0 - radius, hz = sz / 2.0 - radius;
	const int n = steps * 2 + 2;
	const Vec3 axes[6][3] = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}},
							 {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}, {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}}, {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}}};
	Mesh mesh;
	for (const auto &ax : axes) {
		const int base = static_cast<int>(mesh.verts.size());
		for (int iy = 0; iy <= n; ++iy)
			for (int ix = 0; ix <= n; ++ix) {
				const double u = static_cast<double>(ix) / n * 2.0 - 1.0, v = static_cast<double>(iy) / n * 2.0 - 1.0;
				const Vec3 p = ax[0] + ax[1] * u + ax[2] * v;   // on the unit cube's surface
				const Vec3 outer = {p.x * (hx + radius), p.y * (hy + radius), p.z * (hz + radius)};
				const Vec3 inner = {std::max(-hx, std::min(hx, outer.x)), std::max(-hy, std::min(hy, outer.y)), std::max(-hz, std::min(hz, outer.z))};
				const Vec3 d = outer - inner;
				const Vec3 nn = length(d) > 1e-9 ? normalize(d) : ax[0];
				mesh.verts.push_back(inner + nn * radius);
				mesh.normals.push_back(nn);
				mesh.uvs.push_back({static_cast<double>(ix) / n, static_cast<double>(iy) / n});
			}
		for (int iy = 0; iy < n; ++iy)
			for (int ix = 0; ix < n; ++ix) {
				const int a = base + iy * (n + 1) + ix;
				mesh.tris.push_back({a, a + 1, a + n + 2});
				mesh.tris.push_back({a, a + n + 2, a + n + 1});
			}
	}
	return mesh;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Scene text helpers
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline std::string fmt3(Vec3 v) {
	char buf[96];
	std::snprintf(buf, sizeof(buf), "%.5f %.5f %.5f", v.x, v.y, v.z);
	return buf;
}
inline std::string coated(Vec3 color, double roughness = 0.05) {
	char buf[200];
	std::snprintf(buf, sizeof(buf), "Material \"coateddiffuse\" \"rgb reflectance\" [ %.5f %.5f %.5f ] \"float roughness\" [ %.4f ]", color.x, color.y, color.z, roughness);
	return buf;
}
inline std::string diffuseMaterial(Vec3 color) {
	char buf[160];
	std::snprintf(buf, sizeof(buf), "Material \"diffuse\" \"rgb reflectance\" [ %.5f %.5f %.5f ]", color.x, color.y, color.z);
	return buf;
}

// Polished metal (a conductor with the given reflectance colour) and clear glass.
inline std::string metalMaterial(Vec3 reflectance, double roughness = 0.02) {
	char buf[200];
	std::snprintf(buf, sizeof(buf), "Material \"conductor\" \"rgb reflectance\" [ %.5f %.5f %.5f ] \"float roughness\" [ %.4f ]", reflectance.x, reflectance.y, reflectance.z, roughness);
	return buf;
}
inline std::string glassMaterial(double eta = 1.5, double roughness = 0.0) {
	char buf[160];
	std::snprintf(buf, sizeof(buf), "Material \"dielectric\" \"float eta\" [ %.4f ] \"float roughness\" [ %.4f ]", eta, roughness);
	return buf;
}

// An AttributeBegin ... AttributeEnd block: one material and the shapes' text, optionally an area light.
inline std::string attr(const std::string &material, const std::string &shapes, const Vec3 *emission = nullptr) {
	std::string out = "AttributeBegin\n";
	if (emission) out += "  AreaLightSource \"diffuse\" \"rgb L\" [ " + fmt3(*emission) + " ]\n";
	out += "  " + material + "\n" + shapes + "AttributeEnd\n";
	return out;
}


// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// More shapes, a two-link limb solver and camera shots (what the longer films use)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------

// A torus about local +Y: a ring of tube radius `tube` whose centre line has radius `ring`.
inline Mesh torus(double ring, double tube, int segments = 40, int sides = 14) {
	std::vector<std::pair<double, double>> profile;
	for (int i = 0; i <= sides; ++i) {
		const double a = 2.0 * kPi * i / sides;
		profile.push_back({ring + tube * std::cos(a), tube * std::sin(a)});
	}
	return lathe(profile, segments);
}

// A capsule along local +Y from y = 0 to y = length: a cylinder of radius r with a round cap at each end.
inline Mesh capsule(double r, double length, int segments = 20, int capSteps = 6) {
	std::vector<std::pair<double, double>> profile;
	profile.push_back({0.0, -r});
	for (int i = 1; i <= capSteps; ++i) {
		const double a = -kPi / 2.0 + (kPi / 2.0) * i / capSteps;
		profile.push_back({r * std::cos(a), r * std::sin(a)});
	}
	for (int i = 1; i <= capSteps; ++i) {
		const double a = (kPi / 2.0) * i / capSteps;
		profile.push_back({r * std::cos(a), length + r * std::sin(a)});
	}
	profile.back() = {0.0, length + r};
	return lathe(profile, segments);
}

// A mesh placed by `m`, written as pbrt text in the films' story space (+x to the right of the picture, so x is negated: pbrt is left-handed, with the winding flipped to match).
inline std::string emitMesh(const Mesh &mesh, const Mat4 &m) {
	static const Mat4 mirror = scale(-1.0, 1.0, 1.0);
	std::string out;
	mesh.appendPbrt(out, mul(mirror, m), true);
	return out;
}

// A thin round bar from `a` to `b` (radius `r`, round ends): a rod, a string, a post.
inline std::string bar(Vec3 a, Vec3 b, double r, int segments = 10) {
	const double len = length(b - a);
	if (len < 1e-9) return std::string();
	return emitMesh(capsule(r, std::max(0.0, len - 2.0 * r), segments, 3), alignY(a + normalize(b - a) * r, b - a));
}

// A two-link limb (shoulder to elbow to hand) reaching for `target`: the elbow position, bent towards `pole` (a direction the elbow prefers). The hand is where the limb can reach.
struct Limb {
	Vec3 elbow, hand;
};
inline Limb limbIk(Vec3 shoulder, Vec3 target, double l1, double l2, Vec3 pole) {
	Vec3 d = target - shoulder;
	double dist = length(d);
	const double reach = (l1 + l2) * 0.999;
	if (dist > reach) {
		d = d * (reach / dist);
		dist = reach;
	}
	const Vec3 dir = dist > 1e-9 ? d * (1.0 / dist) : Vec3{0.0, -1.0, 0.0};
	const double a = std::max(-1.0, std::min(1.0, (l1 * l1 + dist * dist - l2 * l2) / (2.0 * l1 * std::max(dist, 1e-9))));
	const double along = l1 * a, off = l1 * std::sqrt(std::max(0.0, 1.0 - a * a));
	Vec3 bend = pole - dir * dot(pole, dir);
	bend = length(bend) > 1e-9 ? normalize(bend) : normalize(cross(dir, Vec3{0.0, 0.0, 1.0}));
	return {shoulder + dir * along + bend * off, shoulder + d};
}

// Camera shots: each shot owns a stretch of the film's time and its own camera tracks; `cameraAt` picks the shot of the moment (a hard cut at each shot's start).
struct Shot {
	double start;      // seconds
	Track3 eye, target;
	Track1 fov;
	double lensRadius;
};
struct CameraView {
	Vec3 eye, target;
	double fov, lensRadius;
	int shot;
};
inline CameraView cameraAt(const std::vector<Shot> &shots, double t) {
	std::size_t k = 0;
	for (std::size_t i = 0; i < shots.size(); ++i)
		if (t >= shots[i].start) k = i;
	const Shot &s = shots[k];
	return {s.eye(t), s.target(t), s.fov(t), s.lensRadius, static_cast<int>(k)};
}
// pbrt's LookAt and Camera lines for a view (x negated: pbrt is left-handed; the films write +x to the right of the picture).
inline std::string cameraText(const CameraView &v) {
	char buf[420];
	std::snprintf(buf, sizeof(buf), "LookAt %s %s 0 1 0\nCamera \"perspective\" \"float fov\" [ %.3f ] \"float lensradius\" [ %.4f ] \"float focaldistance\" [ %.3f ]\n",
				  fmt3({-v.eye.x, v.eye.y, v.eye.z}).c_str(), fmt3({-v.target.x, v.target.y, v.target.z}).c_str(), v.fov, v.lensRadius, length(v.eye - v.target));
	return buf;
}

// A film's scene up to and including WorldBegin: `camera` (cameraText of the view at t), the picture's size, the sampler and the integrator. Every film starts this way.
inline std::string sceneHeader(const std::string &camera) {
	std::string out = camera;
	out += "Film \"rgb\" \"integer xresolution\" [ 1280 ] \"integer yresolution\" [ 720 ]\n";
	out += "Sampler \"halton\" \"integer pixelsamples\" [ 128 ]\n";
	out += "Integrator \"volpath\" \"integer maxdepth\" [ 8 ]\n";
	out += "WorldBegin\n";
	return out;
}
inline double smoothstep(double a, double b, double x) {
	const double u = clamp01((x - a) / (b - a));
	return u * u * (3.0 - 2.0 * u);
}

// A film: what a cartoon video preset renders. `sceneAt(t)` is the scene text at time t seconds (0 <= t <= duration); `resolutionHint` is only what its Film line says (the renderer's
// own --width / --height / --samples settings take over).
struct Film {
	const char *key;           // "lamp-and-ball": the --cartoon argument
	const char *name;          // "Lamp and Ball"
	const char *description;   // one line
	double duration;           // seconds
	int fps;
	std::string (*sceneAt)(double t);
	std::string (*headerAt)(double t);   // sceneAt's first part (up to WorldBegin) on its own
	double stillTime;          // a time that makes a good still picture (a thumbnail, a gallery image)
};

}  // namespace cartoon
