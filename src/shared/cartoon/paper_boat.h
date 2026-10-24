#pragma once
// paper_boat.h -- "The Paper Boat", a 24 second film of a folded paper boat sailing across a pond in the rain at dusk. Every raindrop that lands starts a ring of ripples; the rings
// pass under the boat and rock it (its tilt follows the slope of the water where it floats), and they run into one another. The rain eases off, the water calms and the last shot pulls
// back from the boat as it sails towards the low sun, whose glitter lies on the water. Three shots: a low one beside the water, a chase from above, a wide pull back.
//
// The water is a height field, a grid of 96 x 64 cells rebuilt for every frame: the sum of the rings of the drops that landed in the last few seconds (ring radius = c * age, a ripple
// profile sin(k (r - R)) under a gaussian that widens with age and fades with time) and a faint swell. Its normals come from the slope of the same function, so the sun's reflection breaks
// up into glitter on the ripples. A coated dark surface stands for the water (the sky is what it shows: three bands of colour, and the sun as a bright sphere).
//
// The story space is +x to the right of the picture, +y up, +z towards the camera (every emitted x is negated: pbrt is left-handed).

#include "cartoon_lib.h"

namespace cartoon {
namespace paper_boat {

constexpr double kDuration = 24.0;
constexpr int kFps = 24;
constexpr int kSamples = 192;   // recommended samples per pixel (the Sampler line of the film's header)
constexpr double kPondX0 = -15.0, kPondX1 = 15.0, kPondZ0 = -11.0, kPondZ1 = 9.0;
constexpr int kGridX = 96, kGridZ = 64;
constexpr double kRippleSpeed = 1.7;   // how fast a ring spreads (units per second)
constexpr int kDrops = 200;            // candidate drops; the rain's strength thins them out

inline double fade(double t) { return fadeInOut(t, 1.0, 1.5, kDuration); }

// How hard it rains: heavy for the first twelve seconds, then easing to none by twenty.
inline double rainAt(double t) { return 1.0 - smoothstep(12.0, 20.0, t); }

struct Drop {
	double x, z, t0;
};
// Candidate drop j lands at (x, z) at time t0, and is part of the rain only while the rain is strong enough to keep it.
inline Drop dropOf(int j) { return {-11.0 + 22.0 * hash01(j, 1), -7.5 + 14.0 * hash01(j, 2), 0.4 + j * 0.12 + 0.08 * hash01(j, 3)}; }
inline bool dropKept(int j, double t0) { return hash01(j, 4) < rainAt(t0); }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The water: height and slope
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
struct Surface {
	double h, dhdx, dhdz;
};
inline Surface surfaceAt(double x, double z, double t) {
	Surface s = {0.0, 0.0, 0.0};
	// a faint swell
	s.h += 0.012 * std::sin(0.9 * x + 0.7 * t) + 0.008 * std::sin(1.3 * z - 0.9 * t + 1.0);
	s.dhdx += 0.012 * 0.9 * std::cos(0.9 * x + 0.7 * t);
	s.dhdz += 0.008 * 1.3 * std::cos(1.3 * z - 0.9 * t + 1.0);
	const double k = 2.0 * kPi / 0.55;
	const int first = std::max(0, static_cast<int>((t - 4.6) / 0.12));
	const int last = std::min(kDrops - 1, static_cast<int>((t - 0.4) / 0.12) + 1);
	for (int j = first; j <= last; ++j) {
		const Drop d = dropOf(j);
		const double age = t - d.t0;
		if (age < 0.0 || age > 4.0 || !dropKept(j, d.t0)) continue;
		const double dx = x - d.x, dz = z - d.z;
		const double r = std::sqrt(dx * dx + dz * dz) + 1e-9;
		const double R = kRippleSpeed * age, w = 0.16 + 0.22 * age;
		const double amp = 0.055 * std::exp(-age / 1.2) / (1.0 + 1.6 * age);
		const double q = (r - R) / w;
		const double g = std::exp(-q * q), ph = k * (r - R);
		const double h = amp * std::sin(ph) * g;
		// d h / d r
		const double dhdr = amp * (k * std::cos(ph) * g + std::sin(ph) * g * (-2.0 * q / w));
		s.h += h;
		s.dhdx += dhdr * dx / r;
		s.dhdz += dhdr * dz / r;
	}
	// nothing at the pond's edge, so the grid joins the flat water round it without a seam
	const double ex = std::min(x - kPondX0, kPondX1 - x), ez = std::min(z - kPondZ0, kPondZ1 - z);
	const double edge = smoothstep(0.0, 2.0, std::min(ex, ez));
	s.h *= edge;
	s.dhdx *= edge;
	s.dhdz *= edge;
	return s;
}

inline Mesh waterMesh(double t) {
	Mesh m;
	const int nx = kGridX, nz = kGridZ;
	m.verts.reserve(static_cast<std::size_t>((nx + 1) * (nz + 1)));
	for (int iz = 0; iz <= nz; ++iz) {
		for (int ix = 0; ix <= nx; ++ix) {
			const double x = kPondX0 + (kPondX1 - kPondX0) * ix / nx, z = kPondZ0 + (kPondZ1 - kPondZ0) * iz / nz;
			const Surface s = surfaceAt(x, z, t);
			m.verts.push_back({x, s.h, z});
			m.normals.push_back(normalize({-s.dhdx, 1.0, -s.dhdz}));
		}
	}
	for (int iz = 0; iz < nz; ++iz) {
		for (int ix = 0; ix < nx; ++ix) {
			const int a = iz * (nx + 1) + ix, b = a + 1, c = a + (nx + 1), d = c + 1;
			m.tris.push_back({a, c, b});   // wound so the face looks up
			m.tris.push_back({b, c, d});
		}
	}
	return m;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The boat: a faceted hull swept along its length, and one triangular sail
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline void addFlatTri(Mesh &m, Vec3 a, Vec3 b, Vec3 c, bool twoSided = false) {
	const Vec3 n = normalize(cross(b - a, c - a));
	for (int side = 0; side < (twoSided ? 2 : 1); ++side) {
		const Vec3 nn = side == 0 ? n : n * -1.0;
		const int base = static_cast<int>(m.verts.size());
		m.verts.push_back(a);
		m.verts.push_back(side == 0 ? b : c);
		m.verts.push_back(side == 0 ? c : b);
		for (int k = 0; k < 3; ++k) m.normals.push_back(nn);
		m.tris.push_back({base, base + 1, base + 2});
	}
}
inline Mesh hullMesh() {
	Mesh m;
	const int n = 10;
	auto section = [](double x, Vec3 &keel, Vec3 &chineL, Vec3 &rimL, Vec3 &chineR, Vec3 &rimR) {
		const double a = std::abs(x);
		const double beam = 0.46 * (1.0 - std::pow(a, 2.6)) + 0.03;
		const double rim = 0.30 + 0.34 * std::pow(a, 3.0);
		keel = {x, 0.0, 0.0};
		chineL = {x, 0.05, -0.55 * beam};
		rimL = {x, rim, -beam};
		chineR = {x, 0.05, 0.55 * beam};
		rimR = {x, rim, beam};
	};
	for (int i = 0; i < n; ++i) {
		Vec3 k0, cl0, rl0, cr0, rr0, k1, cl1, rl1, cr1, rr1;
		section(-1.0 + 2.0 * i / n, k0, cl0, rl0, cr0, rr0);
		section(-1.0 + 2.0 * (i + 1) / n, k1, cl1, rl1, cr1, rr1);
		// four strips along the hull: keel to chine and chine to rim, each side; two triangles each, flat
		addFlatTri(m, k0, k1, cl1);
		addFlatTri(m, k0, cl1, cl0);
		addFlatTri(m, cl0, cl1, rl1);
		addFlatTri(m, cl0, rl1, rl0);
		addFlatTri(m, k0, cr1, k1);
		addFlatTri(m, k0, cr0, cr1);
		addFlatTri(m, cr0, rr1, cr1);
		addFlatTri(m, cr0, rr0, rr1);
	}
	return m;
}
inline Mesh sailMesh() {
	Mesh m;
	addFlatTri(m, {-0.62, 0.34, 0.0}, {0.62, 0.34, 0.0}, {0.0, 1.12, 0.0}, true);
	return m;
}

struct Parts {
	Mesh hull, sail, pad;
};
inline const Parts &parts() {
	static const Parts p = [] {
		Parts q;
		q.hull = hullMesh();
		q.sail = sailMesh();
		q.pad = lathe({{0.0, 0.012}, {0.50, 0.012}, {0.53, 0.0}, {0.0, 0.0}}, 28);
		return q;
	}();
	return p;
}

// Where the boat is: it drifts from the left of the pond to the right, in a gentle curve.
inline Vec3 boatBase(double t) { return {-8.0 + 15.5 * smoothstep(0.0, 23.0, t) * (0.65 + 0.35 * smoothstep(0.0, 23.0, t)), 0.0, -0.4 + 1.3 * std::sin(0.22 * t + 0.5)}; }
struct BoatPose {
	Vec3 position;
	double yaw, pitch, roll;   // degrees
};
inline BoatPose boatAt(double t) {
	const Vec3 b = boatBase(t), ahead = boatBase(t + 0.1);
	const Vec3 d = normalize(ahead - b);
	const Surface s = surfaceAt(b.x, b.z, t);
	const double yaw = std::atan2(-d.z, d.x) * 180.0 / kPi;   // +x of the boat points along the drift
	// the slope along and across the boat's own axes
	const double along = s.dhdx * d.x + s.dhdz * d.z, across = -s.dhdx * d.z + s.dhdz * d.x;
	return {{b.x, s.h - 0.03, b.z}, yaw, std::atan(along) * 180.0 / kPi * 2.2, std::atan(across) * 180.0 / kPi * 2.2};
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Cameras
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline Vec3 boatPoint(double t, Vec3 off) { return boatBase(t) + off; }

inline const std::vector<Shot> &shots() {
	static const std::vector<Shot> s = {
		{0.0, Track3::withEases({{0.0, {-3.4, 0.42, 4.2}, Ease::Smooth}, {7.0, {-1.4, 0.55, 3.4}, Ease::Smooth}}),
		 sampledTrack(0.0, 7.0, [](double t) { return boatPoint(t, {1.2, 0.45, 0.0}); }), Track1::withEases({{0.0, 36.0, Ease::Smooth}}), 0.035},
		{7.0, sampledTrack(7.0, 15.0, [](double t) { return boatPoint(t, {2.4, 2.6, 3.0}); }), sampledTrack(7.0, 15.0, [](double t) { return boatPoint(t, {0.2, 0.2, 0.0}); }), Track1::withEases({{7.0, 40.0, Ease::Smooth}}), 0.03},
		{15.0, Track3::withEases({{15.0, {1.0, 1.4, 7.0}, Ease::Smooth}, {kDuration, {5.0, 3.8, 11.0}, Ease::Smooth}}), sampledTrack(15.0, kDuration, [](double t) { return boatPoint(t, {0.5, 0.5, 0.0}); }),
		 Track1::withEases({{15.0, 38.0, Ease::Smooth}}), 0.02},
	};
	return s;
}

// Everything up to and including WorldBegin: the camera at t, the picture's size, the sampler and the integrator.
inline std::string headerAt(double t) { return sceneHeader(cameraText(cameraAt(shots(), t)), kSamples); }
inline std::vector<double> shotStarts() { return startsOf(shots()); }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The scene
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A band of the sky: a cylinder of radius 95 between two heights, whose normals point at the middle of the pond (so the light it gives comes inwards).
inline Mesh sky(double y0, double y1) {
	Mesh m = lathe({{95.0, y0}, {95.0, y1}}, 48);
	return m;
}

inline std::string buildSky(double f) {
	std::string out;
	const std::string dark = diffuseMaterial({0.02, 0.02, 0.03});
	const Vec3 low = {f * 2.8, f * 1.5, f * 0.7}, mid = {f * 0.60, f * 0.45, f * 0.55}, high = {f * 0.22, f * 0.28, f * 0.50};
	// three bands of sky round the pond, as cylinders seen from inside (their lowest edge is a little below the water, so the join is hidden)
	out += attr(dark, emitMesh(sky(-2.0, 9.0), identity()), &low);
	out += attr(dark, emitMesh(sky(9.0, 23.0), identity()), &mid);
	out += attr(dark, emitMesh(sky(23.0, 90.0), identity()), &high);
	// the low sun, left of the picture's centre behind the pond: a bright sphere whose glitter lies on the water
	const Vec3 sun = {f * 140.0, f * 78.0, f * 26.0};
	out += emissiveSphere({-16.0, 5.2, -60.0}, 4.2, sun);
	return out;
}

inline std::string buildWater(double t) {
	const Mesh water = waterMesh(t);
	const std::string mat = "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.02 0.06 0.07 ] \"float roughness\" [ 0.006 ]";
	std::string out = attr(mat, emitMesh(water, identity()));
	// the flat water beyond the grid (the pond's own edge is flat: surfaceAt fades to nothing there)
	out += attr(mat, emitMesh(box(300.0, 1.0, 300.0), translate(0.0, -0.503, 0.0)));
	return out;
}

inline std::string buildBoat(double t) {
	const Parts &P = parts();
	const BoatPose b = boatAt(t);
	const Mat4 m = mul(translate(b.position), rotate({0, 1, 0}, b.yaw), rotate({0, 0, 1}, b.pitch), rotate({1, 0, 0}, b.roll), translate(0.0, 0.0, 0.0), scale(1.2));
	const std::string paper = diffuseMaterial({0.90, 0.88, 0.82});
	return attr(paper, emitMesh(P.hull, m) + emitMesh(P.sail, m));
}

inline std::string buildPads(double t) {
	const Parts &P = parts();
	const double px[6] = {-4.0, -1.5, 3.5, 5.5, -6.5, 1.0}, pz[6] = {-2.0, 1.8, -3.2, 1.0, 0.6, -4.5};
	std::string pads;
	for (int i = 0; i < 6; ++i) {
		const Surface s = surfaceAt(px[i], pz[i], t);
		pads += emitMesh(P.pad, mul(translate(px[i], s.h + 0.004, pz[i]), rotate({0, 1, 0}, 60.0 * i), scale(0.8 + 0.15 * (i % 3), 1.0, 0.8 + 0.15 * (i % 3))));
	}
	return attr(coated({0.12, 0.36, 0.12}, 0.2), pads);
}

inline std::string buildReeds(double t) {
	std::string stems, tips;
	for (int i = 0; i < 14; ++i) {
		const double x = -13.0 + 2.6 * hash01(i, 21) + 0.5 * (i % 3), z = -8.4 - 1.2 * hash01(i, 22);
		const double h = 3.0 + 1.6 * hash01(i, 23), sway = 0.12 * std::sin(0.8 * t + 5.0 * hash01(i, 24)) + 0.5 * (hash01(i, 25) - 0.5);
		const Vec3 base = {x, -0.1, z}, top = {x + sway * h * 0.5, h, z + 0.15 * h * (hash01(i, 26) - 0.5)};
		stems += bar(base, top, 0.035, 6);
		tips += bar(top - normalize(top - base) * 0.5, top + normalize(top - base) * 0.08, 0.075, 8);
	}
	return attr(coated({0.18, 0.30, 0.10}, 0.4), stems) + attr(coated({0.22, 0.12, 0.06}, 0.5), tips);
}

inline std::string buildRain(double t, double f) {
	// falling drops: those about to hit the pond, and a curtain of others across the view
	std::string out;
	const Vec3 glint = {f * 1.4, f * 1.5, f * 1.8};
	std::string streaks;
	const int first = std::max(0, static_cast<int>((t - 0.4) / 0.12) - 1);
	const int last = std::min(kDrops - 1, static_cast<int>((t + 0.5 - 0.4) / 0.12) + 1);
	for (int j = first; j <= last; ++j) {
		const Drop d = dropOf(j);
		const double ahead = d.t0 - t;
		if (ahead <= 0.0 || ahead > 0.4 || !dropKept(j, d.t0)) continue;
		streaks += bar({d.x, ahead * 22.0, d.z}, {d.x, ahead * 22.0 + 0.5, d.z}, 0.007, 5);
	}
	for (int i = 0; i < 90; ++i) {
		if (hash01(i, 31) > rainAt(t)) continue;
		const double x = -9.0 + 18.0 * hash01(i, 32), z = -6.0 + 12.0 * hash01(i, 33);
		const double y = 9.0 - std::fmod(t * (16.0 + 6.0 * hash01(i, 34)) + 9.0 * hash01(i, 35), 9.0);
		streaks += bar({x, y, z}, {x - 0.04, y + 0.55, z}, 0.006, 5);
	}
	if (!streaks.empty()) out += attr(diffuseMaterial({0.5, 0.55, 0.6}), streaks, &glint);
	return out;
}

inline std::string sceneAt(double t) {
	const double f = fade(t);
	std::string out = headerAt(t);
	char buf[160];
	std::snprintf(buf, sizeof(buf), "LightSource \"infinite\" \"rgb L\" [ %.4f %.4f %.4f ]\n", f * 0.12, f * 0.13, f * 0.19);
	out += buf;
	out += buildSky(f) + buildWater(t) + buildPads(t) + buildReeds(t) + buildBoat(t) + buildRain(t, f);
	return out;
}

}  // namespace paper_boat
}  // namespace cartoon
