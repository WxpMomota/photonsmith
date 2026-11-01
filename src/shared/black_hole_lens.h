#pragma once
// black_hole_lens.h -- a black hole as light really goes round it, done on the finished picture (lens_look.h `hole ...`, from a film's `blackhole ... lens 1`).
//
// Light passing a Schwarzschild hole of radius 1 moves in a plane, u = 1 / r following u'' + u = 1.5 u^2 with the angle round the hole. Worked out once for a range of impact parameters
// b, that gives for each pixel, from its distance from the hole's middle in the picture alone:
//
//   the shadow      b below 2.6: the light came from the hole. Black.
//   the sky bent    otherwise the light has been turned through an angle, more the closer it passed: the picture behind is read from that much further round, so stars crowd round
//                   the hole in a ring, each with a second image on the far side, and the film's own nebula or ship behind it is drawn out into arcs.
//   the disc        the path crosses the plane of the disc at the angles the disc's tilt says. Each crossing inside the disc's radius is a piece of disc that light came from: the
//                   near side across the front, the far side lifted up over the top of the hole, and a thin bright line under it from light that went right round. Hot and blue-white
//                   within, orange without (Shakura-Sunyaev's temperature), and much brighter on the side turning towards the camera (Doppler beaming, g^4).
//
// A renderer that followed curved rays would know more (what is near the hole is bent too, not only the sky); this needs no change to any renderer, and the disc is exact.
//
// Coordinates: the picture's, x right and y up, in units of half its height, the hole's frame with z from the camera to the hole.

#include <algorithm>
#include <cmath>
#include <vector>

namespace black_hole_lens {

struct Params {
	double x = 0.0, y = 0.0, size = 0.1;   // (where the hole is and its radius, in the picture)
	double nx = 0.0, ny = 1.0, nz = 0.1;   // (the disc's axis, in the camera's frame)
	double disc = 12.0;                    // (how far out it goes, in the hole's radii; none below 3)
	double brightness = 1.0, fov = 40.0, time = 0.0, spin = 1.0;
};

namespace detail {
constexpr int kImpacts = 640, kAngles = 360;
constexpr double kPi = 3.14159265358979323846, kFirst = 0.5, kLast = 60.0, kStep = 0.007, kMostAngle = 4.0 * kPi;
struct Table {
	std::vector<float> r;                 // (kImpacts x kAngles: r at each angle along the path; 0 past where it ends)
	std::vector<float> end;               // (the angle at which it goes off to infinity, or 0 where it fell in)
};
inline double impactAt(int i) { return kFirst * std::pow(kLast / kFirst, static_cast<double>(i) / (kImpacts - 1)); }
inline const Table &table() {
	static const Table t = [] {
		Table o;
		o.r.assign(static_cast<std::size_t>(kImpacts) * kAngles, 0.0f);
		o.end.assign(kImpacts, 0.0f);
		const auto accel = [](double u) { return -u + 1.5 * u * u; };
		for (int i = 0; i < kImpacts; ++i) {
			double u = 0.0, v = 1.0 / impactAt(i), phi = 0.0;
			float *row = &o.r[static_cast<std::size_t>(i) * kAngles];
			int next = 0;
			while (phi < kMostAngle) {
				// (fourth-order Runge-Kutta)
				const double k1u = v, k1v = accel(u), k2u = v + 0.5 * kStep * k1v, k2v = accel(u + 0.5 * kStep * k1u), k3u = v + 0.5 * kStep * k2v, k3v = accel(u + 0.5 * kStep * k2u), k4u = v + kStep * k3v, k4v = accel(u + kStep * k3u);
				u += kStep / 6.0 * (k1u + 2.0 * k2u + 2.0 * k3u + k4u), v += kStep / 6.0 * (k1v + 2.0 * k2v + 2.0 * k3v + k4v), phi += kStep;
				while (next < kAngles && next * kMostAngle / (kAngles - 1) <= phi) {
					if (u > 1e-6 && u < 1.0) row[next] = static_cast<float>(1.0 / u);
					++next;
				}
				if (u >= 1.0) break;                                            // (it fell in)
				if (u <= 0.0) {                                                 // (it got away)
					o.end[static_cast<std::size_t>(i)] = static_cast<float>(phi);
					break;
				}
			}
		}
		return o;
	}();
	return t;
}
// Where an impact parameter falls in the table.
inline int rowOf(double b) { return std::clamp(static_cast<int>(std::lround(std::log(b / kFirst) / std::log(kLast / kFirst) * (kImpacts - 1))), 0, kImpacts - 1); }
// The radius at an angle along a path.
inline double radiusAt(const Table &t, int row, double phi) {
	const double at = phi / kMostAngle * (kAngles - 1);
	const int a = std::clamp(static_cast<int>(at), 0, kAngles - 2);
	const float r0 = t.r[static_cast<std::size_t>(row) * kAngles + a], r1 = t.r[static_cast<std::size_t>(row) * kAngles + a + 1];
	if (r0 <= 0.0f || r1 <= 0.0f) return 0.0;
	return r0 + (r1 - r0) * (at - a);
}
inline double smooth(double a, double b, double x) {
	const double u = std::clamp((x - a) / (b - a), 0.0, 1.0);
	return u * u * (3.0 - 2.0 * u);
}
// The picture at (x, y), in pixels, with what falls outside it folded back in.
inline void read(const std::vector<float> &from, int w, int h, double x, double y, float *out) {
	const auto fold = [](double v, int n) {
		const double m = std::fmod(std::abs(v), 2.0 * (n - 1));
		return m > n - 1 ? 2.0 * (n - 1) - m : m;
	};
	const double fx = fold(x, w), fy = fold(y, h);
	const int x0 = std::min(static_cast<int>(fx), w - 1), y0 = std::min(static_cast<int>(fy), h - 1), x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
	const float u = static_cast<float>(fx - x0), v = static_cast<float>(fy - y0);
	for (int c = 0; c < 3; ++c) {
		const float a = from[(static_cast<std::size_t>(y0) * w + x0) * 3 + c], b = from[(static_cast<std::size_t>(y0) * w + x1) * 3 + c], d = from[(static_cast<std::size_t>(y1) * w + x0) * 3 + c], e = from[(static_cast<std::size_t>(y1) * w + x1) * 3 + c];
		out[c] = (a + (b - a) * u) * (1.0f - v) + (d + (e - d) * u) * v;
	}
}
}  // namespace detail

// How much a ray of impact parameter b is turned aside (radians), when it gets away; the weak field's series past the table's end. Negative when it falls in.
inline double bendOf(double b) {
	if (b >= detail::kLast) return 2.0 / b + 3.7 / (b * b);
	if (b < 2.6) return -1.0;
	const float end = detail::table().end[static_cast<std::size_t>(detail::rowOf(b))];
	return end > 0.0f ? static_cast<double>(end) - detail::kPi : -1.0;
}

inline void apply(float *rgb, int w, int h, const Params &p) {
	using namespace detail;
	const Table &tab = table();
	const std::vector<float> before(rgb, rgb + static_cast<std::size_t>(w) * h * 3);
	const double half = 0.5 * h, midX = 0.5 * w + p.x * half, midY = 0.5 * h - p.y * half, size = std::max(1e-5, p.size);
	const double perRadian = 1.0 / std::tan(0.5 * p.fov * kPi / 180.0);   // (the picture's units to a small angle)
	const double nl = std::sqrt(p.nx * p.nx + p.ny * p.ny + p.nz * p.nz) + 1e-12, nx = p.nx / nl, ny = p.ny / nl, nz = p.nz / nl;
#pragma omp parallel for schedule(dynamic, 16)
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const double dx = (x + 0.5 - midX) / half, dy = -(y + 0.5 - midY) / half, rho = std::sqrt(dx * dx + dy * dy), b = rho / size;
			float *px = &rgb[(static_cast<std::size_t>(y) * w + x) * 3];
			if (b > 400.0) continue;   // (far from it: as it was)
			if (rho < 1e-9) {
				px[0] = px[1] = px[2] = 0.0f;
				continue;
			}
			const double ex = dx / rho, ey = dy / rho, bent = bendOf(b);
			// the sky behind it, read from where the light really came from
			float colour[3] = {0.0f, 0.0f, 0.0f};
			if (bent >= 0.0) {
				const double back = rho - bent * perRadian;
				read(before, w, h, midX + ex * back * half, midY - ey * back * half, colour);
				if (b < 7.0) {   // (a ray near the edge of the shadow has gone round: its light is spread thin)
					const float fade = static_cast<float>(smooth(2.6, 3.4, b));
					for (float &c : colour) c *= fade;
				}
			}
			// the disc: where the path crosses its plane, nearest the camera first
			if (p.disc >= 3.0 && b >= kFirst && b < kLast) {
				const int row = rowOf(b);
				const double end = tab.end[static_cast<std::size_t>(row)], first = std::atan2(nz, nx * ex + ny * ey);
				double see = 1.0;
				for (int k = 0; k < 4; ++k) {
					double phi = first + k * kPi;
					if (phi <= 0.0) phi += kPi;
					if (phi >= kMostAngle || (end > 0.0 && phi >= end)) break;
					const double r = radiusAt(tab, row, phi);
					if (r <= 0.0) break;
					if (r < 3.0 || r > p.disc) continue;
					// the gas there, and how fast it comes towards the light's way
					const double c = std::cos(phi), s = std::sin(phi);
					const double px3 = ex * s, py3 = ey * s, pz3 = -c;                                    // (where it is, unit, in the camera's frame)
					const double tx = ny * pz3 - nz * py3, ty = nz * px3 - nx * pz3, tz = nx * py3 - ny * px3;   // (the way it goes round)
					const double speed = std::sqrt(1.0 / (2.0 * (r - 1.0))), lx = ex * c, ly = ey * c, lz = s;   // (the way the light goes on from it)
					const double towards = std::copysign(1.0, p.spin) * (tx * lx + ty * ly + tz * lz) * speed;
					const double g = std::sqrt(std::max(0.05, 1.0 - 1.5 / r)) * std::sqrt(1.0 - speed * speed) / std::max(0.1, 1.0 - towards);
					const double heat = std::pow(3.0 / r, 0.75) * std::pow(std::max(0.0, 1.0 - std::sqrt(3.0 / r)), 0.25) * 2.2;
					// streaks in it, wound up by the time they have gone round
					const double e1l = std::sqrt(nx * nx + ny * ny) + 1e-9, e1x = ny / e1l, e1y = -nx / e1l, e2x = ny * 0.0 - nz * e1y, e2y = nz * e1x, e2z = nx * e1y - ny * e1x;
					const double around = std::atan2(px3 * e2x + py3 * e2y + pz3 * e2z, px3 * e1x + py3 * e1y) + p.time * p.spin * 1.2 * std::pow(3.0 / r, 1.5);
					const double grain = 0.72 + 0.2 * std::sin(around * 3.0 + r * 2.1) + 0.14 * std::sin(around * 7.0 - r * 3.7) + 0.08 * std::sin(r * 11.0);
					const double edge = smooth(3.0, 3.5, r) * (1.0 - smooth(0.72 * p.disc, p.disc, r));
					const double level = p.brightness * edge * grain * heat * std::pow(g, 4.0) * 0.85;
					const double warm = std::clamp(heat * g * 1.1, 0.0, 1.0);
					colour[0] += static_cast<float>(see * level * (0.86 + 0.5 * warm));
					colour[1] += static_cast<float>(see * level * (0.3 + 0.62 * warm));
					colour[2] += static_cast<float>(see * level * (0.08 + 0.72 * warm * warm));
					see *= 0.55;
				}
			}
			px[0] = colour[0], px[1] = colour[1], px[2] = colour[2];
		}
}

}  // namespace black_hole_lens
