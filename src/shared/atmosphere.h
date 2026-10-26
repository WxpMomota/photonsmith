#pragma once
// atmosphere.h -- the sky the Earth's atmosphere makes: sunlight scattered by the air's molecules (Rayleigh: three times stronger in the blue than the red, so the sky is
// blue and the low sun red) and by its haze (Mie: grey, thrown forward round the sun), and absorbed by ozone (which keeps the twilight sky blue rather than grey), over a
// round Earth (radius 6360 km, the air ending 100 km up), seen from any height. Round sunset and in twilight what the eye sees falls out of it: the sun reddening as its
// light crosses more air, the glow round it, the pink band opposite it (the belt of Venus) above the blue-grey of the Earth's own shadow rising in the east, the sky
// still lit high up after the sun has set below the land.
//
//   single scattering   integrated along each view ray (48 steps, crowded near the eye), the sunlight at each step through the transmittance to the top of the air
//                       (a table over height and the sun's angle, 0 where the Earth is in the way: its shadow)
//   multiple scattering Hillaire's (2020) approximation: at each point the light scattered twice and more, from a table of what an isotropic point there receives
//                       (Psi_ms = L_2 / (1 - f_ms), the infinite series of further orders in one factor), with the ground's light (albedo 0.3) in it
//
// Units: radiance for a sun whose irradiance at the top of the air is 1 in each channel; the channels are 680, 550 and 440 nm. CPU only, std-only, header-only (the films'
// sky pictures: film_sky.h `physical`).

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace atmosphere {

struct RGB {
	double r = 0.0, g = 0.0, b = 0.0;
	RGB operator+(const RGB &o) const { return {r + o.r, g + o.g, b + o.b}; }
	RGB operator-(const RGB &o) const { return {r - o.r, g - o.g, b - o.b}; }
	RGB operator*(const RGB &o) const { return {r * o.r, g * o.g, b * o.b}; }
	RGB operator*(double k) const { return {r * k, g * k, b * k}; }
	double luminance() const { return 0.2126 * r + 0.7152 * g + 0.0722 * b; }
};
inline RGB expRGB(const RGB &a) { return {std::exp(a.r), std::exp(a.g), std::exp(a.b)}; }

constexpr double kGround = 6360e3, kTop = 6460e3, kPi = 3.14159265358979323846;

// The air: its scattering and absorption (per metre, at sea level) and how they thin with height. `haze` scales the haze (1 a clear day, 0.35 very clear, 4 hazy).
struct Air {
	RGB rayleigh{5.802e-6, 13.558e-6, 33.1e-6};
	double rayleighHeight = 8000.0;
	double mieScatter = 3.996e-6, mieAbsorb = 4.40e-6, mieHeight = 1200.0, mieG = 0.8;
	RGB ozone{0.650e-6, 1.881e-6, 0.085e-6};
	explicit Air(double haze = 1.0) : mieScatter(3.996e-6 * haze), mieAbsorb(4.40e-6 * haze) {}
	double rayleighDensity(double h) const { return std::exp(-std::max(0.0, h) / rayleighHeight); }
	double mieDensity(double h) const { return std::exp(-std::max(0.0, h) / mieHeight); }
	static double ozoneDensity(double h) { return std::max(0.0, 1.0 - std::abs(h - 25000.0) / 15000.0); }
	RGB extinction(double h) const {
		const double m = (mieScatter + mieAbsorb) * mieDensity(h);
		return rayleigh * rayleighDensity(h) + RGB{m, m, m} + ozone * ozoneDensity(h);
	}
};

inline double rayleighPhase(double c) { return 3.0 / (16.0 * kPi) * (1.0 + c * c); }
inline double miePhase(double c, double g) {   // Cornette-Shanks
	const double g2 = g * g;
	return 3.0 / (8.0 * kPi) * (1.0 - g2) * (1.0 + c * c) / ((2.0 + g2) * std::pow(std::max(1e-9, 1.0 + g2 - 2.0 * g * c), 1.5));
}

namespace detail {
// The distance along a ray from radius r with cosine mu (to the vertical) to the sphere of radius R, the far root (< 0 when it misses).
inline double toSphere(double r, double mu, double R) {
	const double disc = r * r * (mu * mu - 1.0) + R * R;
	return disc < 0.0 ? -1.0 : -r * mu + std::sqrt(disc);
}
// The near root to the ground, or < 0 when the ray does not hit it.
inline double toGround(double r, double mu) {
	if (mu >= 0.0) return -1.0;
	const double disc = r * r * (mu * mu - 1.0) + kGround * kGround;
	return disc < 0.0 ? -1.0 : -r * mu - std::sqrt(disc);
}
}  // namespace detail

class Model {
public:
	explicit Model(double haze = 1.0) : air_(haze) {
		buildTransmittance();
		buildMultiple();
	}
	const Air &air() const { return air_; }

	// The transmittance from height h (metres above the ground) towards the top of the air along a ray whose cosine to the vertical is mu: 0 when the Earth is in the way.
	RGB transmittance(double h, double mu) const {
		const double x = std::clamp(std::sqrt(std::max(0.0, h) / (kTop - kGround)) * (kH - 1), 0.0, kH - 1.0001), y = std::clamp((mu + 1.0) * 0.5 * (kMu - 1), 0.0, kMu - 1.0001);
		return bilinear(trans_, kMu, x, y);
	}
	// The sunlight that reaches a point h up with the sun at cosine mu: zero where the Earth hides it, its edge softened over the sun's own width (half a degree).
	RGB sunlight(double h, double mu) const {
		const double r = kGround + std::max(0.0, h), horizon = -std::sqrt(std::max(0.0, 1.0 - (kGround / r) * (kGround / r)));
		const double s = std::clamp((mu - horizon) / 0.0087 + 0.5, 0.0, 1.0);
		return s <= 0.0 ? RGB{} : transmittance(h, std::max(mu, horizon + 1e-5)) * s;
	}
	// Psi_ms at h with the sun at cosine mu (the light scattered twice and more, per unit of scattering coefficient)
	RGB multiple(double h, double mu) const {
		const double x = std::clamp(std::max(0.0, h) / (kTop - kGround) * (kM - 1), 0.0, kM - 1.0001), y = std::clamp((mu + 1.0) * 0.5 * (kM - 1), 0.0, kM - 1.0001);
		return bilinear(ms_, kM, x, y);
	}

	// The sky's radiance seen from `altitude` metres up looking along unit `view` (y up), the sun towards unit `sun`. A ray that meets the ground ends there (the air in
	// front of it; the ground itself is not added).
	RGB sky(const double view[3], const double sun[3], double altitude) const {
		const double r0 = kGround + std::max(1.0, altitude), mu = view[1];
		double end = detail::toSphere(r0, mu, kTop);
		const double g = detail::toGround(r0, mu);
		if (g > 0.0) end = g;
		if (!(end > 0.0)) return {};
		const double cosTheta = view[0] * sun[0] + view[1] * sun[1] + view[2] * sun[2];
		const double pr = rayleighPhase(cosTheta), pm = miePhase(cosTheta, air_.mieG);
		RGB L, T{1.0, 1.0, 1.0};
		const int n = 48;
		double prev = 0.0;
		for (int i = 1; i <= n; ++i) {
			const double t = end * (static_cast<double>(i) / n) * (static_cast<double>(i) / n), dt = t - prev, mid = 0.5 * (t + prev);
			prev = t;
			const double px = view[0] * mid, py = r0 + view[1] * mid, pz = view[2] * mid, r = std::sqrt(px * px + py * py + pz * pz), h = r - kGround;
			const double muS = (px * sun[0] + py * sun[1] + pz * sun[2]) / r;
			const RGB sR = air_.rayleigh * air_.rayleighDensity(h);
			const double sM = air_.mieScatter * air_.mieDensity(h);
			const RGB ext = air_.extinction(h);
			const RGB S = (sR * pr + RGB{sM * pm, sM * pm, sM * pm}) * sunlight(h, muS) + (sR + RGB{sM, sM, sM}) * multiple(h, muS);
			const RGB step = expRGB(ext * -dt);
			const auto integrate = [](double s, double e, double st) { return e > 1e-12 ? s * (1.0 - st) / e : 0.0; };   // (S over the step, attenuated within it)
			L = L + T * RGB{integrate(S.r, ext.r, step.r), integrate(S.g, ext.g, step.g), integrate(S.b, ext.b, step.b)};
			T = T * step;
		}
		return L;
	}
	// The sunlight that reaches `altitude` with the sun towards unit `sun` (y up): what a distant light of that sun gives.
	RGB sunAt(const double sun[3], double altitude) const { return sunlight(altitude, sun[1]); }

private:
	static constexpr int kH = 64, kMu = 512, kM = 32;
	Air air_;
	std::vector<RGB> trans_, ms_;

	static RGB bilinear(const std::vector<RGB> &f, int w, double x, double y) {
		const int i = static_cast<int>(x), j = static_cast<int>(y);
		const double u = x - i, v = y - j;
		const auto at = [&](int a, int b) { return f[static_cast<std::size_t>(a) * w + b]; };
		return (at(i, j) * (1 - v) + at(i, j + 1) * v) * (1 - u) + (at(i + 1, j) * (1 - v) + at(i + 1, j + 1) * v) * u;
	}
	// Optical depth along a ray from radius r with cosine mu over [0, d] (trapezoid, 64 steps)
	RGB depth(double r, double mu, double d) const {
		RGB sum;
		const int n = 64;
		for (int k = 0; k <= n; ++k) {
			const double t = d * k / n, rr = std::sqrt(r * r + t * t + 2.0 * r * t * mu);
			sum = sum + air_.extinction(rr - kGround) * ((k == 0 || k == n) ? 0.5 : 1.0);
		}
		return sum * (d / n);
	}
	void buildTransmittance() {
		trans_.assign(static_cast<std::size_t>(kH) * kMu, RGB{});
		for (int i = 0; i < kH; ++i) {
			const double q = static_cast<double>(i) / (kH - 1), h = q * q * (kTop - kGround), r = kGround + h;
			for (int j = 0; j < kMu; ++j) {
				const double mu = -1.0 + 2.0 * j / (kMu - 1);
				if (detail::toGround(r, mu) > 0.0) continue;   // (the Earth in the way)
				trans_[static_cast<std::size_t>(i) * kMu + j] = expRGB(depth(r, mu, std::max(0.0, detail::toSphere(r, mu, kTop))) * -1.0);
			}
			// below the horizon the table holds the value just above it, so a lookup that straddles the horizon is not dragged towards 0 (sunlight() cuts it)
			const double horizon = -std::sqrt(std::max(0.0, 1.0 - (kGround / r) * (kGround / r)));
			const int first = std::clamp(static_cast<int>(std::ceil((horizon + 1.0) * 0.5 * (kMu - 1))), 0, kMu - 1);
			for (int j = 0; j < first; ++j) trans_[static_cast<std::size_t>(i) * kMu + j] = trans_[static_cast<std::size_t>(i) * kMu + first];
		}
	}
	// Hillaire's multiple-scattering table: for each height and sun angle, the second-order light an isotropic point receives (L2) and the share of light it scatters back
	// to itself (f_ms), over 64 directions; Psi_ms = L2 / (1 - f_ms).
	void buildMultiple() {
		ms_.assign(static_cast<std::size_t>(kM) * kM, RGB{});
		const double albedo = 0.3, iso = 1.0 / (4.0 * kPi);
		const int nd = 8, steps = 20;
		for (int i = 0; i < kM; ++i) {
			const double h = std::max(1.0, static_cast<double>(i) / (kM - 1) * (kTop - kGround) - 1.0), r = kGround + h;
			for (int j = 0; j < kM; ++j) {
				const double muS = -1.0 + 2.0 * j / (kM - 1), sunX = std::sqrt(std::max(0.0, 1.0 - muS * muS));
				RGB L2, fms;
				for (int a = 0; a < nd; ++a)
					for (int b = 0; b < nd; ++b) {
						const double cosT = 1.0 - 2.0 * (a + 0.5) / nd, sinT = std::sqrt(std::max(0.0, 1.0 - cosT * cosT)), phi = 2.0 * kPi * (b + 0.5) / nd;
						const double d[3] = {sinT * std::cos(phi), cosT, sinT * std::sin(phi)};
						double end = detail::toSphere(r, cosT, kTop);
						const double gd = detail::toGround(r, cosT);
						const bool ground = gd > 0.0;
						if (ground) end = gd;
						RGB T{1.0, 1.0, 1.0}, l, f;
						const double dt = end / steps;
						for (int k = 0; k < steps; ++k) {
							const double t = (k + 0.5) * dt, px = d[0] * t, py = r + d[1] * t, pz = d[2] * t, rr = std::sqrt(px * px + py * py + pz * pz), hh = rr - kGround;
							const double mu2 = (px * sunX + py * muS) / rr;
							const RGB sR = air_.rayleigh * air_.rayleighDensity(hh);
							const double sM = air_.mieScatter * air_.mieDensity(hh);
							const RGB sc = sR + RGB{sM, sM, sM}, ext = air_.extinction(hh), step = expRGB(ext * -dt);
							const auto part = [](double s, double e, double st) { return e > 1e-12 ? s * (1.0 - st) / e : 0.0; };
							const RGB inSc{part(sc.r, ext.r, step.r), part(sc.g, ext.g, step.g), part(sc.b, ext.b, step.b)};
							l = l + T * inSc * sunlight(hh, mu2) * iso;
							f = f + T * inSc;
							T = T * step;
						}
						if (ground) {   // (the ground's light: lit by the sun, a Lambert surface)
							const double px = d[0] * end, py = r + d[1] * end, pz = d[2] * end, rr = std::sqrt(px * px + py * py + pz * pz);
							const double mu2 = (px * sunX + py * muS) / rr;
							l = l + T * sunlight(0.0, mu2) * (std::max(0.0, mu2) * albedo / kPi);
						}
						L2 = L2 + l * (1.0 / (nd * nd));
						fms = fms + f * (1.0 / (nd * nd));
					}
				// (L2 and f_ms: integrals over the sphere with the isotropic phase, 1/(4 pi) times 4 pi / N for each of the N directions)
				ms_[static_cast<std::size_t>(i) * kM + j] = RGB{L2.r / std::max(1e-6, 1.0 - fms.r), L2.g / std::max(1e-6, 1.0 - fms.g), L2.b / std::max(1e-6, 1.0 - fms.b)};
			}
		}
	}
};

// A model for a haze, made once (the tables take a fraction of a second).
inline std::shared_ptr<const Model> modelFor(double haze) {
	static std::mutex lock;
	static std::map<long, std::shared_ptr<const Model>> made;
	const long key = std::lround(haze * 1000.0);
	const std::lock_guard<std::mutex> hold(lock);
	auto &m = made[key];
	if (!m) m = std::make_shared<Model>(haze);
	return m;
}

}  // namespace atmosphere
