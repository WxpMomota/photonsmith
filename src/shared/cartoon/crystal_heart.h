#pragma once
// crystal_heart.h -- "The Crystal Heart", a one-minute film with no characters and no words: a valley of colossal crystal spires rising out of a still mirror lake, wrapped
// in mist, from the deep blue before dawn to sunrise. The spires glow from within; the last stars fade; the sun breaks through a notch in the ridge and its light falls between
// the spires as shafts through the mist; above the valley floats the Heart, a giant crystal in a ring of orbiting shards, which wakes - a heartbeat, then another - and ignites,
// its light cutting through the mist in beams between the shards as they fall into line. The camera glides low over the water, weaves between the spires, turns to the rising
// sun, finds the Heart, circles it as it ignites, and rises over the valley.
//
// What it shows of the renderer: the camera inside a fog that ends at a sphere, lit by a sun outside it (shafts) and by dozens of lights inside it (glows); thin glass that
// lets light through to the fog and the water (every crystal is a glass shell round a glowing core, src/shared/bxdfs_simple.h ThinDielectricTransmittance); solid glass
// shards that refract the light and block it, so the Heart's light leaves in beams; a mirror lake; bloom; and a score.
//
// Story space: +x to the right of the picture, +y up, +z towards the first camera; the valley's middle is at (0, 0, -110) and the sun rises at -z, over the far ridge.

#include "nature_lib.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace crystal_heart {

constexpr double kDuration = 60.0;
constexpr int kFps = 24;
constexpr int kSamples = 40;   // (with the denoiser 40 look like 128: measured on a 1080p frame)
constexpr int kWidth = 1920, kHeight = 1080;

// The beats
constexpr double kSunBreak = 21.0;   // the sun's rim clears the notch in the far ridge
constexpr double kReveal = 30.0;     // the Heart
constexpr double kBeat1 = 33.5, kBeat2 = 36.0, kBeat3 = 38.0;   // its heartbeats
constexpr double kIgnite = 41.0;     // it ignites ...
constexpr double kPeak = 44.0;       // ... full blaze, the shards in line
constexpr double kRise = 50.0;       // the camera rises over the valley

inline const Vec3 &valleyCentre() {
	static const Vec3 c{0.0, 0.0, -110.0};
	return c;
}
inline Vec3 heartCentre(double t) { return {0.0, 42.0 + 0.8 * std::sin(2.0 * kPi * t / 11.0), -115.0}; }
constexpr double kHeartHalfHeight = 13.0, kHeartRadius = 6.2;

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The light over time
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline double sunElevation(double t) {
	static const Track1 tr({{0.0, -6.0}, {18.0, -1.0}, {22.0, 1.2}, {26.0, 2.8}, {40.0, 5.0}, {60.0, 8.0}});
	return tr(t);
}
inline Vec3 sunDirection(double t) {
	const double el = sunElevation(t) * kPi / 180.0, az = -8.0 * kPi / 180.0;
	return normalize({std::sin(az) * std::cos(el), std::sin(el), -std::cos(az) * std::cos(el)});
}
inline double sunUp(double t) { return smoothstep(18.5, 23.0, t); }   // (hidden by the ridge until it clears the notch)
inline Vec3 sunColour(double t) { return lerp(Vec3{1.0, 0.36, 0.10}, Vec3{1.0, 0.72, 0.42}, smoothstep(21.0, 55.0, t)); }
inline Vec3 skyColour(double t) {
	const double dawn = smoothstep(14.0, 34.0, t);
	return lerp(Vec3{0.012, 0.022, 0.060}, Vec3{0.090, 0.090, 0.130}, dawn);
}
inline double starLight(double t) { return 1.0 - smoothstep(10.0, 26.0, t); }

inline nature::Mist mistAt(double t) {
	nature::Mist m;
	m.centre = {0.0, 20.0, -82.0};
	m.radius = 172.0;
	// thick in the blue, thinning as the sun comes up (so the sky shows behind the Heart), and thinner still for the last look over the valley
	m.density = lerp(lerp(0.0075, 0.0035, smoothstep(20.0, 36.0, t)), 0.0022, smoothstep(48.0, 58.0, t));
	m.tint = lerp(Vec3{0.88, 0.96, 1.15}, Vec3{0.98, 1.0, 1.04}, smoothstep(18.0, 40.0, t));
	m.g = 0.72;
	m.absorption = 0.0015;
	return m;
}

// The Heart's own light: a faint glow, three heartbeats, then the ignition and a slow breathing blaze.
inline double heartGlow(double t) {
	const auto beat = [t](double at, double size) { return size * (std::exp(-std::pow((t - at) / 0.12, 2.0)) + 0.6 * std::exp(-std::pow((t - at - 0.32) / 0.12, 2.0))); };
	double g = 0.5 + 0.15 * std::sin(2.0 * kPi * t / 4.0);
	g += beat(kBeat1, 3.0) + beat(kBeat2, 6.0) + beat(kBeat3, 10.0);
	g += 2.5 * smoothstep(kBeat3 + 0.6, kIgnite, t);
	g += 32.0 * smoothstep(kIgnite, kPeak, t) * (1.0 - 0.55 * smoothstep(kPeak, kPeak + 3.0, t));
	g *= 1.0 + 0.08 * std::sin(2.0 * kPi * t / 2.6) * smoothstep(kPeak, kPeak + 2.0, t);
	return g;
}
// How far the shards have fallen into line: 0 a loose cloud round the Heart, 1 a flat ring.
inline double shardOrder(double t) { return smoothstep(kIgnite - 1.0, kPeak + 0.5, t); }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The shots
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline Shot shotOf(double t0, double t1, Vec3 e0, Vec3 e1, Vec3 g0, Vec3 g1, double fov0, double fov1, double lens) {
	return {t0, Track3({{t0, e0}, {t1, e1}}), Track3({{t0, g0}, {t1, g1}}), Track1({{t0, fov0}, {t1, fov1}}), lens};
}
inline const std::vector<Shot> &shots() {
	static const std::vector<Shot> s = {
		shotOf(0.0, 10.0, {-4.0, 1.25, 46.0}, {-1.5, 1.7, 16.0}, {0.0, 9.0, -80.0}, {2.0, 11.0, -95.0}, 46.0, 44.0, 0.0),            // the still lake
		shotOf(10.0, 20.0, {12.0, 6.5, -6.0}, {-5.0, 9.5, -52.0}, {-12.0, 11.0, -60.0}, {6.0, 15.0, -112.0}, 52.0, 50.0, 0.0),        // among the spires
		shotOf(20.0, 30.0, {-14.0, 5.5, -44.0}, {-4.0, 6.5, -54.0}, {-38.0, 14.0, -300.0}, {-32.0, 17.0, -300.0}, 44.0, 42.0, 0.0),  // first light
		shotOf(30.0, 40.0, {30.0, 9.0, -60.0}, {20.0, 13.0, -72.0}, {0.0, 40.0, -115.0}, {0.0, 42.0, -115.0}, 48.0, 44.0, 0.0),        // the Heart
		shotOf(40.0, 50.0, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 50.0, 56.0, 0.0),                      // ignition (an orbit: viewAt)
		shotOf(50.0, kDuration, {34.0, 10.0, -52.0}, {80.0, 60.0, -66.0}, {0.0, 40.0, -115.0}, {-6.0, 32.0, -118.0}, 52.0, 64.0, 0.0),  // the valley at sunrise: a crane up beside the Heart, the sun off to the left
	};
	return s;
}
inline std::vector<double> shotStarts() { return startsOf(shots()); }
inline CameraView viewAt(double t) {
	CameraView v = cameraAt(shots(), t);
	if (t >= 40.0 && t < kRise) {   // round the Heart as it ignites
		const double u = smoothstep(40.0, kRise, t), a = (55.0 + 70.0 * u) * kPi / 180.0;
		const Vec3 h = heartCentre(t);
		v.eye = h + Vec3{38.0 * std::cos(a), -6.0 + 4.0 * u, 38.0 * std::sin(a)};
		v.target = h + Vec3{0.0, -1.0, 0.0};
	}
	v.eye = v.eye + handheld(t, 0.03, 5);   // a slight drift, a drone's
	v.target = v.target + handheld(t, 0.08, 6);
	return v;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The crystals
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A crystal: an n-sided prism from y = 0 to `height` with a pyramid tip `tip` high on top and a flat base, every face flat (its own vertices) so that each facet catches the
// light on its own.
inline Mesh crystalMesh(double height, double radius, double tip, int sides = 6, double twist = 0.0) {
	Mesh m;
	const auto ring = [&](double y, double r, int k) {
		const double a = 2.0 * kPi * k / sides + twist;
		return Vec3{r * std::cos(a), y, r * std::sin(a)};
	};
	const auto quad = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
		const Vec3 n = normalize(cross(b - a, d - a));
		const int i = static_cast<int>(m.verts.size());
		for (const Vec3 &p : {a, b, c, d}) {
			m.verts.push_back(p);
			m.normals.push_back(n);
		}
		m.tris.push_back({i, i + 1, i + 2});
		m.tris.push_back({i, i + 2, i + 3});
	};
	const auto tri = [&](Vec3 a, Vec3 b, Vec3 c) {
		const Vec3 n = normalize(cross(b - a, c - a));
		const int i = static_cast<int>(m.verts.size());
		for (const Vec3 &p : {a, b, c}) {
			m.verts.push_back(p);
			m.normals.push_back(n);
		}
		m.tris.push_back({i, i + 1, i + 2});
	};
	const Vec3 apex = {0.0, height + tip, 0.0}, bottom = {0.0, 0.0, 0.0};
	for (int k = 0; k < sides; ++k) {
		const Vec3 a0 = ring(0.0, radius, k), a1 = ring(0.0, radius, k + 1), b0 = ring(height, radius, k), b1 = ring(height, radius, k + 1);
		quad(a0, b0, b1, a1);   // (wound to face out)
		tri(b0, apex, b1);
		tri(a1, bottom, a0);
	}
	// (the winding of the side faces above is made outward: check one and flip all if not)
	const Vec3 c = m.verts[0], n = m.normals[0];
	if (dot(n, normalize(Vec3{c.x, 0.0, c.z})) < 0.0) {
		for (auto &t : m.tris) std::swap(t[1], t[2]);
		for (Vec3 &v : m.normals) v = v * -1.0;
	}
	return m;
}
// A bipyramid (the Heart and its shards): `sides` facets round, `half` above and below the waist of radius `radius`.
inline Mesh bipyramidMesh(double half, double radius, int sides) {
	Mesh m;
	const Vec3 top = {0.0, half, 0.0}, bottom = {0.0, -half, 0.0};
	for (int k = 0; k < sides; ++k) {
		const double a0 = 2.0 * kPi * k / sides, a1 = 2.0 * kPi * (k + 1) / sides;
		const Vec3 p0 = {radius * std::cos(a0), 0.0, radius * std::sin(a0)}, p1 = {radius * std::cos(a1), 0.0, radius * std::sin(a1)};
		for (const auto &face : {std::array<Vec3, 3>{p0, top, p1}, std::array<Vec3, 3>{p1, bottom, p0}}) {
			Vec3 n = normalize(cross(face[1] - face[0], face[2] - face[0]));
			std::array<Vec3, 3> f = face;
			if (dot(n, (f[0] + f[1] + f[2]) * (1.0 / 3.0)) < 0.0) {
				std::swap(f[1], f[2]);
				n = n * -1.0;
			}
			const int i = static_cast<int>(m.verts.size());
			for (const Vec3 &p : f) {
				m.verts.push_back(p);
				m.normals.push_back(n);
			}
			m.tris.push_back({i, i + 1, i + 2});
		}
	}
	return m;
}

// One crystal of a cluster: where it stands, how it leans, its size and its glow's colour.
struct Crystal {
	Vec3 base;
	double yaw, leanX, leanZ;   // degrees
	double height, radius, tip;
	Vec3 colour;
	double glow;
	int sides;
};
struct Spire {
	Vec3 base;
	double scale;
	std::vector<Crystal> crystals;
	double rockSize;
};
inline Vec3 palette(int k) {
	static const Vec3 colours[] = {{0.30, 0.85, 1.00}, {0.62, 0.36, 1.00}, {0.20, 1.00, 0.72}, {1.00, 0.38, 0.78}, {0.38, 0.62, 1.00}, {1.00, 0.72, 0.30}};
	return colours[k % 6];
}
// The spires: clusters of crystals on rocks standing in the lake, kept clear of every camera path, taller towards the back of the valley.
inline const std::vector<Spire> &spires() {
	static const std::vector<Spire> all = [] {
		std::vector<Spire> out;
		std::vector<Vec3> eyes;
		for (double t = 0.0; t <= kDuration; t += 0.5) eyes.push_back(viewAt(t).eye);
		int salt = 0;
		for (int tries = 0; tries < 4000 && out.size() < 46; ++tries, ++salt) {
			const double a = 2.0 * kPi * hash01(salt, 1), r = 16.0 + 125.0 * std::sqrt(hash01(salt, 2));
			const Vec3 base = valleyCentre() + Vec3{r * std::cos(a) * 1.05, 0.0, r * std::sin(a) * 0.95};
			if (base.z > 30.0) continue;   // (not behind the first camera)
			const double scale = 0.55 + 0.9 * hash01(salt, 3) + 0.5 * smoothstep(-20.0, -180.0, base.z);
			bool clear = true;
			for (const Vec3 &e : eyes) clear = clear && length(Vec3{e.x - base.x, 0.0, e.z - base.z}) > 9.0 + 5.0 * scale;
			for (const Spire &o : out) clear = clear && length(o.base - base) > 9.0 + 5.0 * (scale + o.scale);
			if (length(Vec3{base.x - heartCentre(0.0).x, 0.0, base.z - heartCentre(0.0).z}) < 14.0) clear = false;   // (the Heart floats over open water)
			if (!clear) continue;
			Spire s;
			s.base = base;
			s.scale = scale;
			s.rockSize = 3.2 * scale;
			const Vec3 colour = palette(static_cast<int>(6.0 * hash01(salt, 4)));
			const int count = 3 + static_cast<int>(4.0 * hash01(salt, 5));
			for (int k = 0; k < count; ++k) {
				Crystal c;
				const double main = k == 0 ? 1.0 : 0.30 + 0.45 * hash01(salt * 7 + k, 6);
				const double ang = 2.0 * kPi * hash01(salt * 7 + k, 7);
				c.base = base + Vec3{std::cos(ang), 0.0, std::sin(ang)} * (k == 0 ? 0.0 : 1.8 * scale + 0.8 * scale * hash01(salt * 7 + k, 8)) + Vec3{0.0, 0.4 * scale, 0.0};
				c.yaw = 360.0 * hash01(salt * 7 + k, 9);
				c.leanX = (k == 0 ? 5.0 : 12.0) * (hash01(salt * 7 + k, 10) - 0.5) + (k == 0 ? 0.0 : 16.0 * std::cos(ang));
				c.leanZ = (k == 0 ? 5.0 : 12.0) * (hash01(salt * 7 + k, 11) - 0.5) + (k == 0 ? 0.0 : 16.0 * std::sin(ang));
				c.height = scale * main * (16.0 + 18.0 * hash01(salt * 7 + k, 12));
				c.radius = scale * main * (2.0 + 0.9 * hash01(salt * 7 + k, 13));
				c.tip = c.radius * (1.6 + 1.0 * hash01(salt * 7 + k, 14));
				c.colour = k == 0 || hash01(salt * 7 + k, 15) < 0.7 ? colour : palette(static_cast<int>(6.0 * hash01(salt * 7 + k, 16)));
				c.glow = 0.7 + 0.6 * hash01(salt * 7 + k, 17);
				c.sides = hash01(salt * 7 + k, 18) < 0.75 ? 6 : 5;
				s.crystals.push_back(c);
			}
			out.push_back(s);
		}
		return out;
	}();
	return all;
}
inline Mat4 crystalFrame(const Crystal &c) { return mul(translate(c.base), rotate({1, 0, 0}, c.leanZ), rotate({0, 0, 1}, -c.leanX), rotate({0, 1, 0}, c.yaw)); }

// The spires' glow over the film: slowly pulsing, a little brighter while the valley is dark, and answering the Heart when it ignites.
inline double spireGlow(double t, int salt) {
	const double pulse = 1.0 + 0.18 * std::sin(2.0 * kPi * (t / (5.0 + 2.0 * hash01(salt, 41)) + hash01(salt, 42)));
	const double dark = lerp(1.25, 0.85, smoothstep(18.0, 34.0, t));
	const double answer = 1.0 + 0.9 * smoothstep(kIgnite + 0.5, kPeak + 1.0, t) * (1.0 - 0.5 * smoothstep(kPeak + 1.0, kPeak + 6.0, t));
	return pulse * dark * answer;
}

inline std::string emissive(const std::string &shapes, Vec3 L) {
	return attr("Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", shapes, &L);
}
inline std::string crystalGlass() { return "Material \"thindielectric\" \"float eta\" [ 1.55 ]"; }

inline std::string buildSpires(double t) {
	std::string out, shells;
	int salt = 0;
	for (const Spire &s : spires()) {
		++salt;
		out += nature::placeObject(salt % 2 ? "rockA" : "rockB", mul(translate(s.base + Vec3{0.0, -0.6 * s.rockSize, 0.0}), rotate({0, 1, 0}, 47.0 * salt), scale(s.rockSize, 0.75 * s.rockSize, s.rockSize)));
		const double glow = spireGlow(t, salt);
		for (const Crystal &c : s.crystals) {
			const Mat4 f = crystalFrame(c);
			shells += emitMesh(crystalMesh(c.height, c.radius, c.tip, c.sides), f);
			// the core: a slim crystal inside, in four bands that glow brighter towards the tip
			const double band = c.height * 0.25;
			const double levels[4] = {0.30, 0.55, 0.85, 1.30};
			for (int b = 0; b < 4; ++b) {
				const Mesh piece = crystalMesh(band * 0.98, c.radius * 0.34, b == 3 ? c.tip * 0.75 : 0.0, c.sides);
				out += emissive(emitMesh(piece, mul(f, translate(0.0, 0.05 + band * b, 0.0))), c.colour * (1.6 * c.glow * glow * levels[b]));
			}
		}
		// the light the cluster casts on the mist and the water (the solid crystals keep their cores' own light in): an unseen lamp beside its tallest crystal, on the
		// side that faces the middle of the valley
		const Crystal &main = s.crystals.front();
		const Vec3 inward = normalize(Vec3{valleyCentre().x - s.base.x, 0.0, valleyCentre().z - s.base.z + 60.0});
		const Vec3 at = main.base + Vec3{0.0, 0.35 * main.height, 0.0} + inward * (main.radius * 1.8);
		const Vec3 I = main.colour * (55.0 * main.glow * glow * s.scale);
		char lamp[220];
		std::snprintf(lamp, sizeof(lamp), "LightSource \"point\" \"point3 from\" [ %.4f %.4f %.4f ] \"rgb I\" [ %.4f %.4f %.4f ]\n", -at.x, at.y, at.z, I.x, I.y, I.z);
		out += lamp;
	}
	// solid glass: it bends what is seen through it, and it stands in the sunlight's way, so the sun's shafts fall between the spires
	out += attr("Material \"dielectric\" \"float eta\" [ 1.55 ]", shells);
	return out;
}

// The Heart: a tall eight-sided bipyramid of thin glass round a glowing core (a smaller bipyramid and a white-hot centre), turning slowly, and its ring of solid glass shards.
inline std::string buildHeart(double t) {
	std::string out;
	const Vec3 h = heartCentre(t);
	const Mat4 frame = mul(translate(h), rotate({0, 1, 0}, 9.0 * t), rotate({0, 0, 1}, 4.0 * std::sin(2.0 * kPi * t / 13.0)));
	out += attr(crystalGlass(), emitMesh(bipyramidMesh(kHeartHalfHeight, kHeartRadius, 8), frame));
	const double g = heartGlow(t);
	const Vec3 core = lerp(Vec3{0.45, 0.80, 1.00}, Vec3{1.00, 0.86, 0.62}, smoothstep(kIgnite, kPeak, t));
	out += emissive(emitMesh(bipyramidMesh(kHeartHalfHeight * 0.62, kHeartRadius * 0.55, 8), frame), core * (1.2 * g));
	out += emissiveSphere(h, 0.9, Vec3{1.0, 0.96, 0.9} * (12.0 * g));
	// a lattice of light inside it: the edges of a smaller bipyramid, turning the other way, so that it reads as a solid crystal with depth rather than a flat glow
	{
		std::string edges;
		const Mat4 inner = mul(translate(h), rotate({0, 1, 0}, -14.0 * t + 20.0));
		const double hh = kHeartHalfHeight * 0.82, rr = kHeartRadius * 0.74;
		const Vec3 top = transformPoint(inner, {0.0, hh, 0.0}), bottom = transformPoint(inner, {0.0, -hh, 0.0});
		for (int k = 0; k < 8; ++k) {
			const double a0 = 2.0 * kPi * k / 8.0, a1 = 2.0 * kPi * (k + 1) / 8.0;
			const Vec3 p0 = transformPoint(inner, {rr * std::cos(a0), 0.0, rr * std::sin(a0)}), p1 = transformPoint(inner, {rr * std::cos(a1), 0.0, rr * std::sin(a1)});
			edges += bar(p0, top, 0.07, 6) + bar(p0, bottom, 0.07, 6) + bar(p0, p1, 0.07, 6);
		}
		out += emissive(edges, core * (3.5 * g + 2.0));
	}
	// the shards: a loose cloud round it that falls into a flat ring as it ignites, and turns faster
	std::string shards;
	const int n = 18;
	const double order = shardOrder(t);
	for (int k = 0; k < n; ++k) {
		const double phase = 2.0 * kPi * k / n;
		const double spin = (0.10 + 0.25 * order) * t + 0.6 * order * smoothstep(kIgnite, kPeak + 6.0, t);
		const double a = phase + spin + (1.0 - order) * 0.6 * (hash01(k, 51) - 0.5);
		const double r = lerp(15.0 + 12.0 * hash01(k, 52), 21.0, order);
		const double y = lerp(-13.0 + 26.0 * hash01(k, 53), 0.0, order);
		const double len = 2.2 + 2.2 * hash01(k, 54);
		const Vec3 p = h + Vec3{r * std::cos(a), y, r * std::sin(a)};
		const double tiltX = lerp(80.0 * (hash01(k, 55) - 0.5), 0.0, order), tiltZ = lerp(80.0 * (hash01(k, 56) - 0.5), 0.0, order);
		shards += emitMesh(bipyramidMesh(len, len * 0.28, 6), mul(translate(p), rotate({0, 1, 0}, -a * 180.0 / kPi), rotate({1, 0, 0}, tiltX), rotate({0, 0, 1}, tiltZ + 90.0 * order)));
	}
	out += attr("Material \"dielectric\" \"float eta\" [ 1.55 ]", shards);
	return out;
}

// The mountains round the valley: a ring of craggy ridges, with a notch in the far one where the sun comes up.
inline const Mesh &mountains() {
	static const Mesh m = [] {
		Mesh g;
		const int na = 220, nr = 14;
		const double sunAz = -8.0 * kPi / 180.0;
		for (int j = 0; j <= nr; ++j)
			for (int i = 0; i <= na; ++i) {
				const double a = 2.0 * kPi * i / na, u = static_cast<double>(j) / nr;
				const double r = 168.0 + 120.0 * u;
				Vec3 p = valleyCentre() + Vec3{r * std::sin(a), 0.0, -r * std::cos(a)};
				// (a = 0 is straight ahead, at -z: the far ridge)
				double ridge = 46.0 + 22.0 * nature::fbm2(std::cos(a) * 3.0, std::sin(a) * 3.0, 3);
				double da = std::remainder(a - sunAz, 2.0 * kPi);
				ridge *= lerp(0.30, 1.0, smoothstep(0.05, 0.22, std::abs(da)));   // the notch
				const double shape = std::sin(kPi * std::pow(u, 0.75));          // rising from the shore to the crest and down behind it
				p.y = -1.0 + ridge * shape + 7.0 * nature::fbm2(p.x * 0.05, p.z * 0.05, 7) * shape;
				g.verts.push_back(p);
			}
		for (int j = 0; j < nr; ++j)
			for (int i = 0; i < na; ++i) {
				const int a = j * (na + 1) + i, b = a + 1, c = a + na + 1, d = c + 1;
				g.tris.push_back({a, c, b});
				g.tris.push_back({b, c, d});
			}
		sdf::computeNormals(g);
		for (Vec3 &n : g.normals)
			if (n.y < 0.0) n = n * -1.0;
		return g;
	}();
	return m;
}

// A flat square `size` across, facing up (the lake and its bed).
inline Mesh flatQuad(double size) {
	Mesh m;
	const double h = size / 2.0;
	m.verts = {{-h, 0.0, -h}, {h, 0.0, -h}, {h, 0.0, h}, {-h, 0.0, h}};
	m.normals.assign(4, Vec3{0.0, 1.0, 0.0});
	m.tris = {{0, 2, 1}, {0, 3, 2}};
	return m;
}

// The stars: tiny glowing balls far beyond the mist, fading as it gets light.
inline std::string buildStars(double t) {
	const double light = starLight(t);
	if (light <= 0.001) return std::string();
	std::string out;
	for (int k = 0; k < 220; ++k) {
		const double a = 2.0 * kPi * hash01(k, 61), y = 0.06 + 0.9 * std::pow(hash01(k, 62), 0.7);
		const Vec3 dir = normalize({std::cos(a) * std::sqrt(1.0 - y * y), y, std::sin(a) * std::sqrt(1.0 - y * y)});
		const double b = (0.3 + 0.7 * hash01(k, 63)) * light * (1.0 + 0.2 * std::sin(t * (2.0 + 3.0 * hash01(k, 64)) + 9.0 * k));
		out += emissiveSphere(valleyCentre() + dir * 900.0, 0.9 + 0.8 * hash01(k, 65), Vec3{0.85, 0.9, 1.0} * (60.0 * b));
	}
	return out;
}

// Motes of light drifting up off the water round the spires.
inline std::string buildMotes(double t) {
	std::string out;
	for (int k = 0; k < 70; ++k) {
		const Spire &s = spires()[static_cast<std::size_t>(k) % spires().size()];
		const double rise = std::fmod(t * (0.6 + 0.6 * hash01(k, 71)) + 30.0 * hash01(k, 72), 30.0);
		const double a = 2.0 * kPi * hash01(k, 73) + 0.15 * t;
		const Vec3 p = s.base + Vec3{(3.0 + 4.0 * hash01(k, 74)) * std::cos(a), 0.5 + rise, (3.0 + 4.0 * hash01(k, 74)) * std::sin(a)};
		const double fade = smoothstep(0.0, 2.0, rise) * (1.0 - smoothstep(22.0, 30.0, rise));
		out += emissiveSphere(p, 0.07, palette(k) * (40.0 * fade + 0.01));
	}
	return out;
}

inline const std::string &objectDefinitions() {
	static const std::string s = [] {
		std::string out;
		const std::string rock = diffuseMaterial({0.045, 0.045, 0.055});
		out += nature::objectBegin("rockA") + attr(rock, emitMesh(nature::rock(1.0, 3), identity())) + nature::objectEnd();
		out += nature::objectBegin("rockB") + attr(rock, emitMesh(nature::rock(1.0, 8), identity())) + nature::objectEnd();
		return out;
	}();
	return s;
}

// The camera's exposure: opened up in the blue before dawn, closed down as the sun and then the Heart blaze.
inline double exposureAt(double t) {
	double iso = lerp(100.0, 70.0, smoothstep(18.0, 30.0, t));
	iso *= lerp(1.0, 0.6, smoothstep(kIgnite, kPeak, t));
	return iso;
}
inline std::string headerAt(double t) {
	char look[200];
	const double warmth = lerp(-0.30, 0.08, smoothstep(16.0, 34.0, t));
	const double bloom = 0.28 + 0.22 * smoothstep(kIgnite, kPeak, t) * (1.0 - 0.4 * smoothstep(kPeak, kPeak + 4.0, t));
	std::snprintf(look, sizeof(look), "# @rt-look bloom %.3f vignette 0.38 warmth %.3f saturation 1.12\n", bloom, warmth);
	std::string h = sceneHeader(cameraText(viewAt(t)), kSamples, kWidth, kHeight);
	char iso[64];
	std::snprintf(iso, sizeof(iso), "Film \"rgb\" \"float iso\" [ %.1f ] ", exposureAt(t));
	const std::size_t at = h.find("Film \"rgb\" ");
	if (at != std::string::npos) h.replace(at, 11, iso);
	// six bounces: as good as eight here (a path that has bounced six times in this valley carries next to nothing) and a little faster
	const std::size_t depth = h.find("\"integer maxdepth\" [ 8 ]");
	if (depth != std::string::npos) h.replace(depth, 24, "\"integer maxdepth\" [ 6 ]");
	return std::string(look) + nature::mistBeforeCamera(mistAt(t)) + h;
}

inline std::string sceneAt(double t) {
	std::string out = headerAt(t);
	out += nature::mistBoundary(mistAt(t));
	char buf[200];
	const Vec3 sky = skyColour(t);
	std::snprintf(buf, sizeof(buf), "LightSource \"infinite\" \"rgb L\" [ %.5f %.5f %.5f ]\n", sky.x, sky.y, sky.z);
	out += buf;
	// the sun: a glowing ball 1.5 km away, about a degree across - its disc and all the sunlight
	if (const double up = sunUp(t); up > 0.0) out += emissiveSphere(valleyCentre() + sunDirection(t) * 1500.0, 15.0, sunColour(t) * (30000.0 * up));
	out += buildStars(t);
	out += objectDefinitions();
	// the lake: one still mirror of water, and its dark bed below
	out += attr("Material \"dielectric\" \"float eta\" [ 1.33 ]", emitMesh(flatQuad(700.0), translate(valleyCentre())));
	out += attr(diffuseMaterial({0.015, 0.02, 0.025}), emitMesh(flatQuad(700.0), translate(valleyCentre() + Vec3{0.0, -3.0, 0.0})));
	out += attr(diffuseMaterial({0.075, 0.07, 0.085}), emitMesh(mountains(), identity()));
	out += buildSpires(t);
	out += buildHeart(t);
	out += buildMotes(t);
	return out;
}

}  // namespace crystal_heart
}  // namespace cartoon
