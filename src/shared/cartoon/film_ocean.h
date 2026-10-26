#pragma once
// film_ocean.h -- the open sea as an ocean spectrum (Tessendorf's FFT ocean): a tile of waves of every length the wind raises, each running at the speed deep water gives it,
// summed by an inverse FFT each frame - heights, the sideways pull that sharpens the crests (`choppy`), the slopes for the normals, and where the surface folds over (foam).
// It replaces the twenty summed Gerstner waves of a film's sea (film_water.h): a real sea's mix of swell and chop, no two crests alike.
//
//   spectrum   Phillips: A exp(-1 / (k L)^2) / k^4 |k . wind|^2, the waves against the wind damped, the shortest cut off; L = V^2 / g for the wind V that, fully risen,
//              makes waves of the sea's significant height (Hs = 0.21 V^2 / g); scaled so the tile's significant height is the sea's
//   tile       128 x 128 over 30 L metres (at least 40), repeating; a second tile 0.27 times as big (a fifth of the height), turned, adds chop that does not line up with the first
//   time       each wave's phase turns at omega = sqrt(g k); a tile at a time is a function of the time alone (fixed seeds), made once a frame
//
// Coordinates are pbrt's (x, z); heights are about 0 (the sea's level is added by the caller).

#include <algorithm>
#include <cmath>
#include <complex>
#include <mutex>
#include <vector>

#include "cartoon_lib.h"

namespace cartoon {
namespace ocean {

constexpr int kN = 128;

struct Tile {
	double size = 100.0;
	int n = kN;                                    // cells each way
	std::vector<double> h, dx, dz, sx, sz, fold;   // per cell: height, sideways pull, slope (dh/dx, dh/dz), how far it folds (0 none)
	std::vector<Tile> coarser;                     // the same tile averaged down, 64 x 64 to 1 x 1 (sampleFiltered: what a mesh's far points, metres apart, should see)
};

namespace detail {

using Cx = std::complex<double>;

// An in-place radix-2 FFT of n (a power of two) values with a stride (`inverse`: the sign of the exponent; no scaling).
inline void fft(Cx *a, int n, int stride, bool inverse) {
	for (int i = 1, j = 0; i < n; ++i) {
		int bit = n >> 1;
		for (; j & bit; bit >>= 1) j ^= bit;
		j ^= bit;
		if (i < j) std::swap(a[i * stride], a[j * stride]);
	}
	for (int len = 2; len <= n; len <<= 1) {
		const double ang = 2.0 * kPi / len * (inverse ? 1.0 : -1.0);
		const Cx wl(std::cos(ang), std::sin(ang));
		for (int i = 0; i < n; i += len) {
			Cx w(1.0, 0.0);
			for (int k = 0; k < len / 2; ++k) {
				const Cx u = a[(i + k) * stride], v = a[(i + k + len / 2) * stride] * w;
				a[(i + k) * stride] = u + v;
				a[(i + k + len / 2) * stride] = u - v;
				w *= wl;
			}
		}
	}
}
inline void fft2(std::vector<Cx> &g, bool inverse) {
	for (int r = 0; r < kN; ++r) fft(&g[static_cast<std::size_t>(r) * kN], kN, 1, inverse);
	for (int c = 0; c < kN; ++c) fft(&g[static_cast<std::size_t>(c)], kN, kN, inverse);
}
// A standard normal number from two hashes (Box-Muller): the same every run.
inline double gauss(int i, int salt) {
	const double u1 = std::max(1e-12, hash01(i, salt)), u2 = hash01(i, salt + 7919);
	return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * kPi * u2);
}

// The spectrum's starting amplitudes h0(k) for a sea, with the tile's size: made once for a wave height, wind and size.
struct Spectrum {
	double size = 0.0, height = -1.0, wind = 0.0, scale = 1.0;
	std::vector<Cx> h0;
	std::vector<double> omega, kx, kz;
};
inline Spectrum spectrumFor(double height, double windDegrees, double size, int salt) {
	Spectrum s;
	s.size = size, s.height = height, s.wind = windDegrees;
	const double Hs = std::max(0.05, height), V = std::sqrt(Hs * 9.81 / 0.21), L = V * V / 9.81;
	const double wa = windDegrees * kPi / 180.0, wx = std::sin(wa), wz = std::cos(wa);
	const double cut = L / 1000.0;   // (the shortest waves damped away)
	s.h0.assign(static_cast<std::size_t>(kN) * kN, Cx(0.0, 0.0));
	s.omega.assign(s.h0.size(), 0.0);
	s.kx.assign(s.h0.size(), 0.0);
	s.kz.assign(s.h0.size(), 0.0);
	double energy = 0.0;
	for (int j = 0; j < kN; ++j)
		for (int i = 0; i < kN; ++i) {
			const std::size_t at = static_cast<std::size_t>(j) * kN + i;
			const int m = i < kN / 2 ? i : i - kN, n = j < kN / 2 ? j : j - kN;
			const double kx = 2.0 * kPi * m / size, kz = 2.0 * kPi * n / size, k = std::sqrt(kx * kx + kz * kz);
			s.kx[at] = kx, s.kz[at] = kz;
			if (k < 1e-9) continue;
			const double along = (kx * wx + kz * wz) / k;
			double P = std::exp(-1.0 / (k * L * k * L)) / (k * k * k * k) * along * along * std::exp(-k * k * cut * cut);
			if (along < 0.0) P *= 0.07;   // (waves running against the wind are few)
			s.h0[at] = Cx(gauss(static_cast<int>(at), salt), gauss(static_cast<int>(at), salt + 3)) * std::sqrt(P * 0.5);
			s.omega[at] = std::sqrt(9.81 * k);
			energy += std::norm(s.h0[at]);
		}
	// scaled so that the surface's rms (each h0 and its conjugate twin make a real wave of twice its energy) is Hs / 4
	s.scale = Hs / 4.0 / std::sqrt(std::max(1e-30, 2.0 * energy));
	for (Cx &c : s.h0) c *= s.scale;
	return s;
}

}  // namespace detail

// The tile of a sea (its significant height `height`, its wind) at time t, `size` metres across, `choppy` the crests' sharpness (0 to 1): its heights, sideways pull,
// slopes and folds. Made once a frame (cached by its numbers).
inline Tile tileAt(double height, double windDegrees, double choppy, double size, double t, int salt) {
	using detail::Cx;
	thread_local std::vector<detail::Spectrum> spectra;   // (a few: a sea's main tile and its chop)
	const detail::Spectrum *found = nullptr;
	for (const detail::Spectrum &s : spectra)
		if (s.size == size && s.height == height && s.wind == windDegrees) found = &s;
	if (!found) {
		if (spectra.size() >= 4) spectra.erase(spectra.begin());
		spectra.push_back(detail::spectrumFor(height, windDegrees, size, salt));
		found = &spectra.back();
	}
	const detail::Spectrum &spec = *found;
	const std::size_t count = static_cast<std::size_t>(kN) * kN;
	std::vector<Cx> H(count), DX(count), DZ(count), SX(count), SZ(count);
	for (int j = 0; j < kN; ++j)
		for (int i = 0; i < kN; ++i) {
			const std::size_t at = static_cast<std::size_t>(j) * kN + i;
			const int mi = (kN - i) % kN, mj = (kN - j) % kN;   // (-k)
			const std::size_t neg = static_cast<std::size_t>(mj) * kN + mi;
			const double w = spec.omega[at] * t;
			const Cx e(std::cos(w), std::sin(w));
			const Cx h = spec.h0[at] * e + std::conj(spec.h0[neg]) * std::conj(e);
			const double kx = spec.kx[at], kz = spec.kz[at], k = std::sqrt(kx * kx + kz * kz);
			H[at] = h;
			SX[at] = Cx(0.0, kx) * h;
			SZ[at] = Cx(0.0, kz) * h;
			DX[at] = k > 1e-9 ? Cx(0.0, -kx / k) * h : Cx(0.0, 0.0);
			DZ[at] = k > 1e-9 ? Cx(0.0, -kz / k) * h : Cx(0.0, 0.0);
		}
	for (std::vector<Cx> *g : {&H, &DX, &DZ, &SX, &SZ}) detail::fft2(*g, true);
	Tile tile;
	tile.size = size;
	tile.h.resize(count), tile.dx.resize(count), tile.dz.resize(count), tile.sx.resize(count), tile.sz.resize(count), tile.fold.resize(count);
	for (std::size_t at = 0; at < count; ++at) {
		tile.h[at] = H[at].real();
		tile.dx[at] = choppy * DX[at].real();
		tile.dz[at] = choppy * DZ[at].real();
		tile.sx[at] = SX[at].real();
		tile.sz[at] = SZ[at].real();
	}
	// the folds: where the sideways pull squeezes the surface past itself (the Jacobian below 1)
	const double cell = size / kN;
	for (int j = 0; j < kN; ++j)
		for (int i = 0; i < kN; ++i) {
			const auto idx = [](int a, int b) { return static_cast<std::size_t>((b + kN) % kN) * kN + static_cast<std::size_t>((a + kN) % kN); };
			const double dxdx = (tile.dx[idx(i + 1, j)] - tile.dx[idx(i - 1, j)]) / (2.0 * cell), dzdz = (tile.dz[idx(i, j + 1)] - tile.dz[idx(i, j - 1)]) / (2.0 * cell);
			const double dxdz = (tile.dx[idx(i, j + 1)] - tile.dx[idx(i, j - 1)]) / (2.0 * cell);
			const double jac = (1.0 + dxdx) * (1.0 + dzdz) - dxdz * dxdz;
			tile.fold[idx(i, j)] = std::max(0.0, 1.0 - jac);
		}
	// the coarser levels: each cell the mean of four
	const Tile *from = &tile;
	std::vector<Tile> levels;
	levels.reserve(8);
	for (int m = kN / 2; m >= 1; m /= 2) {
		Tile c;
		c.size = size, c.n = m;
		for (std::vector<double> Tile::*f : {&Tile::h, &Tile::dx, &Tile::dz, &Tile::sx, &Tile::sz, &Tile::fold}) {
			const std::vector<double> &a = from->*f;
			std::vector<double> &b = c.*f;
			b.assign(static_cast<std::size_t>(m) * m, 0.0);
			for (int j = 0; j < m; ++j)
				for (int i = 0; i < m; ++i) {
					const std::size_t q = static_cast<std::size_t>(2 * j) * (2 * m) + 2 * i;
					b[static_cast<std::size_t>(j) * m + i] = 0.25 * (a[q] + a[q + 1] + a[q + 2 * m] + a[q + 2 * m + 1]);
				}
		}
		levels.push_back(std::move(c));
		from = &levels.back();
	}
	tile.coarser = std::move(levels);
	return tile;
}

// The tile's values at pbrt's (x, z), bilinear and repeating: height, sideways pull, slopes, fold.
struct Sample {
	double h = 0.0, dx = 0.0, dz = 0.0, sx = 0.0, sz = 0.0, fold = 0.0;
};
inline Sample sample(const Tile &tile, double x, double z) {
	const int N = tile.n;
	const double u = x / tile.size * N - 0.5 * (kN / N - 1) / static_cast<double>(kN / N), v = z / tile.size * N - 0.5 * (kN / N - 1) / static_cast<double>(kN / N);   // (a coarse cell's value is at its centre)
	const double fu = std::floor(u), fv = std::floor(v), a = u - fu, b = v - fv;
	const int i0 = static_cast<int>(fu), j0 = static_cast<int>(fv);
	const auto at = [N](int i, int j) { return static_cast<std::size_t>(((j % N) + N) % N) * N + static_cast<std::size_t>(((i % N) + N) % N); };
	const std::size_t c00 = at(i0, j0), c10 = at(i0 + 1, j0), c01 = at(i0, j0 + 1), c11 = at(i0 + 1, j0 + 1);
	const auto mix = [&](const std::vector<double> &f) { return (f[c00] * (1 - a) + f[c10] * a) * (1 - b) + (f[c01] * (1 - a) + f[c11] * a) * b; };
	Sample s;
	s.h = mix(tile.h), s.dx = mix(tile.dx), s.dz = mix(tile.dz), s.sx = mix(tile.sx), s.sz = mix(tile.sz), s.fold = mix(tile.fold);
	return s;
}
// The tile seen by points `width` metres apart: blended from the levels whose cells are about that wide, so waves shorter than the spacing are averaged away (not aliased
// into noise, nor pulled past each other by their sideways pull into folds seen from below).
inline Sample sampleFiltered(const Tile &tile, double x, double z, double width) {
	const double level = std::log2(std::max(1.0, width / (tile.size / kN)));
	if (level <= 0.0 || tile.coarser.empty()) return sample(tile, x, z);
	const int lo = std::min(static_cast<int>(level), static_cast<int>(tile.coarser.size())), hi = std::min(lo + 1, static_cast<int>(tile.coarser.size()));
	const double w = std::min(1.0, level - lo);
	const Sample a = lo == 0 ? sample(tile, x, z) : sample(tile.coarser[lo - 1], x, z), b = sample(tile.coarser[hi - 1], x, z);
	Sample s;
	s.h = lerp(a.h, b.h, w), s.dx = lerp(a.dx, b.dx, w), s.dz = lerp(a.dz, b.dz, w), s.sx = lerp(a.sx, b.sx, w), s.sz = lerp(a.sz, b.sz, w), s.fold = lerp(a.fold, b.fold, w);
	return s;
}

}  // namespace ocean
}  // namespace cartoon
