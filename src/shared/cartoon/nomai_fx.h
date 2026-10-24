#pragma once
// nomai_fx.h -- the light-work of "The Last Nomai Signal" (nomai_signal.h): the Nomai's spiral writing (the game's branching spirals: a conversation, each reply a
// spiral growing from the one before), holograms that flicker and break up (the explorer's own in pale blue; the old, remote transmission in orange, as the game's
// projection walls show local and remote messages), the star map, the waveform of the signal, the dust of a landing and of footsteps in low gravity, and the beam of the
// answer.
//
// Every glow here is a light (an emitting surface): kept thin and few-sided, so that a frame with a star map of a thousand points still renders in seconds.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "nomai_explorer.h"
#include "nomai_ruins.h"

namespace cartoon {
namespace nomai {

inline Vec3 remoteOrange() { return {1.0, 0.56, 0.22}; }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The spiral writing
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// One spiral: a curve winding outwards from its centre (a logarithmic spiral, like the game's), in a plane (centre, right, up), `size` across.
struct Spiral {
	Vec3 centre, right, up;
	double size, turns, start;   // start: the angle it begins at
};
inline std::vector<Vec3> spiralPoints(const Spiral &s, int n) {
	std::vector<Vec3> p;
	const double total = s.turns * 2.0 * kPi;
	for (int i = 0; i <= n; ++i) {
		const double u = static_cast<double>(i) / n;
		const double a = s.start + u * total;
		const double r = s.size * 0.5 * std::exp(-2.2 * (1.0 - u)) * u;
		p.push_back(s.centre + s.right * (r * std::cos(a)) + s.up * (r * std::sin(a)));
	}
	return p;
}
// A wall of writing: a main spiral and replies branching from it (each smaller, turned the other way), as one conversation. `reveal` 0..1 draws the strokes in order
// (writing that appears), `glow` lights them (0: carved, dark).
inline std::string writingText(Vec3 centre, Vec3 right, Vec3 up, double size, int seed, double reveal, double glow, Vec3 colour) {
	std::vector<Spiral> spirals = {{centre, right, up, size, 1.6, 0.4 * seed}};
	for (int k = 0; k < 4; ++k) {
		const double a = 2.0 * kPi * hash01(k, seed + 300);
		const Vec3 c = centre + right * (size * (0.35 + 0.15 * hash01(k, seed + 301)) * std::cos(a)) + up * (size * (0.35 + 0.15 * hash01(k, seed + 301)) * std::sin(a));
		spirals.push_back({c, right * -1.0, up, size * (0.35 + 0.25 * hash01(k, seed + 302)), 1.3, a});
	}
	std::string strokes, dots;
	const int n = static_cast<int>(spirals.size());
	for (int k = 0; k < n; ++k) {
		const double from = static_cast<double>(k) / n, to = static_cast<double>(k + 1) / n;
		const double shown = clamp01((reveal - from) / (to - from));
		if (shown <= 0.0) continue;
		const std::vector<Vec3> pts = spiralPoints(spirals[static_cast<std::size_t>(k)], 60);
		const int upto = static_cast<int>(shown * 60.0);
		for (int i = 0; i < upto; ++i) strokes += rod(pts[static_cast<std::size_t>(i)], pts[static_cast<std::size_t>(i + 1)], size * 0.006, 4);
		// the little marks along the stroke (the script's letters)
		for (int i = 6; i < upto; i += 5) {
			const Vec3 d = normalize(pts[static_cast<std::size_t>(i + 1)] - pts[static_cast<std::size_t>(i)]);
			const Vec3 side = normalize(cross(d, cross(right, up)));
			const double len = size * (0.015 + 0.02 * hash01(i, seed + k));
			strokes += rod(pts[static_cast<std::size_t>(i)], pts[static_cast<std::size_t>(i)] + side * len, size * 0.004, 3);
		}
	}
	if (strokes.empty()) return std::string();
	if (glow <= 0.0) return attr(diffuseMaterial({0.18, 0.16, 0.14}), strokes);
	return glowText(strokes, colour, glow);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Holograms
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A figure made of light: the explorer's shape (robe, head, antlers) sculpted coarsely, sliced into scan lines with gaps and torn where the projection fails. `frame`
// places it (body space -> story space, any scale); `t` drives the flicker.
inline std::string holoFigureText(const ExplorerPose &p, const Mat4 &frame, Vec3 colour, double strength, double t, int seed, double broken = 0.25) {
	if (strength <= 0.0) return std::string();
	const ExplorerBody b = explorerBody(p);
	const double band = 0.035, phase = std::fmod(t * 0.7, 1.0);
	const auto f = [&](Vec3 q) {
		double d = outerRobeSdf(q, p, b);
		const Vec3 h = transformPoint(b.headInv, q);
		d = std::min(d, std::min(headSkinSdf(h), std::min(hornSdf(h), earSdf(h, p.ears))));
		// scan lines: thin gaps every few centimetres, drifting upwards
		const double s = std::fmod(q.y / band + phase + 100.0, 1.0);
		if (s < 0.16) d = std::max(d, band * 0.3);
		// torn regions where the projection fails, moving with time
		const double tear = nature::noise2(q.y * 6.0 + t * 1.3, std::atan2(q.x, q.z) * 2.0 + seed, seed + 400);
		if (tear < broken) d = std::max(d, 0.02);
		return d;
	};
	const Mesh m = sdf::surfaceNets(f, {-0.5, 0.0, -0.5}, {0.5, 1.95, 0.6}, 0.025);
	const double flick = (nature::noise2(t * 9.0, seed * 1.3, seed + 401) > 0.85 ? 0.35 : 1.0) * (0.85 + 0.15 * std::sin(t * 31.0 + seed));
	return glowText(meshText(m, frame), colour, strength * flick);
}
// A scatter of glowing motes: a hologram's breakup or its edge, `count` points in a box round `centre`, drifting.
inline std::string motesText(Vec3 centre, Vec3 extent, int count, Vec3 colour, double strength, double t, int seed, double radius = 0.012) {
	if (strength <= 0.0 || count <= 0) return std::string();
	std::string out;
	for (int k = 0; k < count; ++k) {
		const Vec3 p = centre + Vec3{extent.x * (hash01(k, seed) - 0.5) * 2.0, extent.y * std::fmod(hash01(k, seed + 1) + 0.03 * t * (0.5 + hash01(k, seed + 2)), 1.0),
									 extent.z * (hash01(k, seed + 3) - 0.5) * 2.0};
		const double tw = 0.5 + 0.5 * std::sin(t * (2.0 + 3.0 * hash01(k, seed + 4)) + k);
		out += emissiveSphere(p, radius, colour * (strength * tw));
	}
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The star map: a sphere of stars round the explorer, faint lines between some, and the star of the signal (orange: a remote message), flashing, then gone
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline Vec3 signalStarLocal() { return normalize(Vec3{0.45, 0.62, -0.64}) * 5.2; }
// `centre` the map's middle, `appear` 0..1 (stars arrive from the middle outwards), `flash` the signal star's brightness (0: none), `gone` true once it has vanished.
inline std::string starMapText(Vec3 centre, double appear, double flash, bool gone, double t, double scaleR = 1.0) {
	if (appear <= 0.0) return std::string();
	std::string stars;
	const int n = 900;
	for (int k = 0; k < n; ++k) {
		const double z = 2.0 * hash01(k, 501) - 1.0, a = 2.0 * kPi * hash01(k, 502);
		const double r = (2.4 + 5.5 * std::sqrt(hash01(k, 503))) * scaleR;
		const double show = clamp01((appear * 8.5 - r / scaleR) / 1.5);
		if (show <= 0.0) continue;
		const Vec3 d{std::sqrt(1.0 - z * z) * std::cos(a), z, std::sqrt(1.0 - z * z) * std::sin(a)};
		const double tw = 0.75 + 0.25 * std::sin(t * (1.0 + 2.0 * hash01(k, 504)) + k);
		const double size = 0.010 + 0.016 * std::pow(hash01(k, 505), 4.0);
		stars += emissiveSphere(centre + d * r, size * scaleR, lerp(nomaiBlue(), Vec3{1.0, 1.0, 1.0}, 0.4 * hash01(k, 506)) * (3.0 * show * tw));
	}
	// constellation lines, faint
	std::string lines;
	for (int k = 0; k < 40; ++k) {
		const int i = static_cast<int>(hash01(k, 507) * 300), j = i + 1 + static_cast<int>(hash01(k, 508) * 3);
		const auto at = [&](int q) {
			const double z = 2.0 * hash01(q, 501) - 1.0, a = 2.0 * kPi * hash01(q, 502);
			const double r = (2.4 + 5.5 * std::sqrt(hash01(q, 503))) * scaleR;
			return centre + Vec3{std::sqrt(1.0 - z * z) * std::cos(a), z, std::sqrt(1.0 - z * z) * std::sin(a)} * r;
		};
		if (length(at(i) - at(j)) < 3.0 * scaleR) lines += rod(at(i), at(j), 0.004 * scaleR, 3);
	}
	std::string out = stars;
	if (!lines.empty()) out += glowText(lines, nomaiBlue(), 0.6 * appear);
	const Vec3 s = centre + signalStarLocal() * scaleR;
	if (!gone && flash > 0.0) {
		out += emissiveSphere(s, 0.045 * scaleR * (1.0 + flash), remoteOrange() * (6.0 + 30.0 * flash));
		out += glowText(meshText(torus(0.16 * scaleR * (1.0 + flash), 0.006 * scaleR, 32, 3), alignY(s, s - centre)), remoteOrange(), 2.0 * flash);
	} else if (gone) {
		// where it was: a faint empty ring
		out += glowText(meshText(torus(0.14 * scaleR, 0.004 * scaleR, 32, 3), alignY(s, s - centre)), remoteOrange(), 0.5);
	}
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The waveform: the signal's pulse drawn as a line, beating every few seconds, in a frame (x along the line, y up)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
constexpr double kPulsePeriod = 3.6;   // seconds between the beeps
inline double pulseAt(double t, double t0 = 0.0) {   // how much a beep is sounding at t (0..1)
	const double ph = std::fmod(t - t0 + 100.0 * kPulsePeriod, kPulsePeriod);
	return ph < 0.35 ? std::sin(kPi * ph / 0.35) : 0.0;
}
inline std::string waveformText(const Mat4 &frame, double width, double height, double t, Vec3 colour, double strength) {
	std::string strokes;
	const int n = 90;
	Vec3 prev{};
	for (int i = 0; i <= n; ++i) {
		const double x = (static_cast<double>(i) / n - 0.5) * width;
		// the waveform scrolls: older beeps to the left
		const double age = (0.5 - static_cast<double>(i) / n) * 2.0 * kPulsePeriod;
		const double amp = pulseAt(t - age);
		const double y = height * 0.45 * amp * std::sin(x / width * 90.0) + height * 0.03 * std::sin(x * 37.0 + t * 3.0);
		const Vec3 p = transformPoint(frame, {x, y, 0.0});
		if (i > 0) strokes += rod(prev, p, height * 0.012, 4);
		prev = p;
	}
	return glowText(strokes, colour, strength);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Dust in low gravity: a landing's slow ring of dust, a footstep's puff
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
constexpr double kGravity = 1.9;   // m/s^2: a small moon's
// A puff of dust thrown from `at` (story space, `up` the local up) at time t0: `count` grains flying out and up, falling back slowly, fading as they settle. t is now.
inline std::string dustPuffText(Vec3 at, Vec3 up, double t0, double t, int count, double speed, int seed, double grain = 0.05) {
	const double dt = t - t0;
	if (dt <= 0.0 || dt > 9.0) return std::string();
	const Vec3 side = normalize(cross(up, std::abs(up.y) > 0.9 ? Vec3{1.0, 0.0, 0.0} : Vec3{0.0, 1.0, 0.0})), fwd = cross(side, up);
	std::string grains;
	const double fade = 1.0 - smoothstep(3.5, 9.0, dt);   // (grains settle and vanish one by one)
	for (int k = 0; k < count; ++k) {
		if (hash01(k, seed + 7) > fade) continue;
		const double a = 2.0 * kPi * hash01(k, seed), v = speed * (0.3 + 0.7 * hash01(k, seed + 1)), lift = 0.15 + 0.5 * hash01(k, seed + 2);
		const double drag = 1.0 - std::exp(-1.2 * dt);   // (the thin dust slows as it spreads)
		const double outward = v / 1.2 * drag;
		const double h = std::max(0.0, v * lift * dt - 0.5 * kGravity * 0.25 * dt * dt);
		const Vec3 p = at + (side * std::cos(a) + fwd * std::sin(a)) * outward + up * (h + 0.02);
		const double r = grain * (0.4 + 1.2 * hash01(k, seed + 3)) * (1.0 + 0.15 * dt);
		grains += meshText(uvSphere(r, 6, 4), translate(p));
	}
	if (grains.empty()) return std::string();
	return attr(translucentMaterial({0.62, 0.56, 0.50}, {0.70, 0.68, 0.64}), grains);
}

}  // namespace nomai
}  // namespace cartoon
