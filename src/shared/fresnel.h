#pragma once

// ---------------------------------------------------------------------------
// Shared Fresnel functions — usable on both CPU and GPU.
//
// Mirrors pbrt-v4's FrDielectric and FrComplex (src/pbrt/util/scattering.h),
// both annotated CPU_GPU so the same code runs on CPU and GPU.
//
// CPU usage  (MSVC / plain C++):  CPU_GPU is empty, T = double
// GPU usage  (NVCC / CUDA):       CPU_GPU = __host__ __device__, T = float
//
// FrComplex overloads:
//   FrComplex(cosTheta, eta_r, eta_k)      -- explicit real+imag (CPU+GPU)
//   FrComplex(cosTheta, complex<T> eta)    -- std::complex convenience (CPU)
// ---------------------------------------------------------------------------

#include "cpu_gpu.h"

#if defined(__CUDACC__)
#   include <cuda_runtime.h>   // fmaxf, fminf, sqrtf (device)
#else
#   include <cmath>
#   include <complex>
#endif

// ---------------------------------------------------------------------------
// CauchyEta -- two-term Cauchy dispersion formula: n(lambda) = A + B/lambda^2
// (lambda in micrometers). The simplest physically-reasonable wavelength-
// dependent IOR model that doesn't need a measured spectral data table -
// see dielectric's own dispersive constructor (material_simple.h) for how
// A/B are derived from an artist-facing (eta_d, Abbe number) pair instead
// of being authored directly.
// ---------------------------------------------------------------------------
template <typename T>
CPU_GPU T CauchyEta(T lambda_nm, T A, T B) {
	T lambda_um = lambda_nm * T(0.001);
	return A + B / (lambda_um * lambda_um);
}

// ---------------------------------------------------------------------------
// CauchyCoefficientsFromAbbe -- derives CauchyEta()'s (A, B) pair from the
// artist-facing (eta_d, Abbe number) pair real glass catalogs quote (e.g.
// crown glass ~=1.52/59, flint glass ~=1.62/36), via the standard closed
// form at the F/C/D spectral lines (486.1/656.3/589.3nm, lambda in
// micrometers):
//   B = (eta_d - 1) / (abbe * (1/lambda_F^2 - 1/lambda_C^2))
//   A = eta_d - B / lambda_D^2
// CPU-only (construction-time, not per-ray, unlike CauchyEta() above) -
// shared by every dispersive material's make_dispersive() factory
// (dielectric in material_simple.h, rough_dielectric in material_pbrt.h) so
// the derivation can't silently drift between them the way two hand-copied
// inline versions could.
// ---------------------------------------------------------------------------
inline void CauchyCoefficientsFromAbbe(double eta_d, double abbe_number, double& A, double& B) {
	constexpr double lambda_F = 0.4861, lambda_C = 0.6563, lambda_D = 0.5893;
	B = (eta_d - 1.0) / (abbe_number * (1.0 / (lambda_F * lambda_F) - 1.0 / (lambda_C * lambda_C)));
	A = eta_d - B / (lambda_D * lambda_D);
}

// ---------------------------------------------------------------------------
// FrDielectric -- real-valued Fresnel for dielectric interfaces (pbrt-v4)
// eta = eta_t / eta_i  (transmitted / incident IOR)
// ---------------------------------------------------------------------------
template <typename T>
CPU_GPU T FrDielectric(T cos_theta_i, T eta) {
	// Clamp to [-1, 1]
#if defined(__CUDACC__)
	cos_theta_i = fmaxf(-T(1), fminf(T(1), cos_theta_i));
#else
	cos_theta_i = std::fmax(T(-1), std::fmin(T(1), cos_theta_i));
#endif

	// If ray is inside the medium, flip the interface
	if (cos_theta_i < T(0)) {
		eta = T(1) / eta;
		cos_theta_i = -cos_theta_i;
	}

	T sin2_theta_i = T(1) - cos_theta_i * cos_theta_i;
	T sin2_theta_t = sin2_theta_i / (eta * eta);

	// Total internal reflection
	if (sin2_theta_t >= T(1))
		return T(1);

	// SafeSqrt: clamp argument to avoid NaN from floating-point rounding
#if defined(__CUDACC__)
	T cos_theta_t = sqrtf(fmaxf(T(0), T(1) - sin2_theta_t));
#else
	T cos_theta_t = std::sqrt(std::fmax(T(0), T(1) - sin2_theta_t));
#endif

	T r_parl = (eta * cos_theta_i - cos_theta_t)
			 / (eta * cos_theta_i + cos_theta_t);
	T r_perp = (cos_theta_i - eta * cos_theta_t)
			 / (cos_theta_i + eta * cos_theta_t);

	return (r_parl * r_parl + r_perp * r_perp) / T(2);
}

// ---------------------------------------------------------------------------
// FrComplex -- complex-valued Fresnel for conductor interfaces (pbrt-v4)
// Mirrors pbrt-v4 FrComplex(Float cosTheta_i, pstd::complex<Float> eta)
// in src/pbrt/util/scattering.h.
//
// eta_r, eta_k = real and imaginary parts of complex IOR (η + i·k)
// Returns scalar reflectance in [0,1] for one wavelength channel.
//
// Implementation avoids std::complex<> for GPU compatibility:
// all complex arithmetic is expanded manually.
// ---------------------------------------------------------------------------
template <typename T>
CPU_GPU T FrComplex(T cos_theta_i, T eta_r, T eta_k) {
	// Clamp cosine to [0,1] (conductor: ray always from outside)
#if defined(__CUDACC__)
	cos_theta_i = fmaxf(T(0), fminf(T(1), cos_theta_i));
#else
	cos_theta_i = std::fmax(T(0), std::fmin(T(1), cos_theta_i));
#endif

	T sin2_i = T(1) - cos_theta_i * cos_theta_i;

	// Complex Snell's law: sin2_t = sin2_i / (eta_r + i*eta_k)^2
	// (eta_r + i*eta_k)^2 = (eta_r^2 - eta_k^2) + i*(2*eta_r*eta_k)
	T eta_r2 = eta_r * eta_r;
	T eta_k2 = eta_k * eta_k;
	T denom_r = eta_r2 - eta_k2;
	T denom_i = T(2) * eta_r * eta_k;
	// divide sin2_i (real) by complex denom: result = (a*re - b*im, -a*im + b*re) / |d|^2
	// where a = sin2_i, b = 0 (purely real numerator)
	T denom_sq = denom_r * denom_r + denom_i * denom_i;
	T sin2_t_r = sin2_i * denom_r / denom_sq;
	T sin2_t_i = -sin2_i * denom_i / denom_sq;

	// cos_t = sqrt(1 - sin2_t)  (complex sqrt)
	// 1 - sin2_t = (1 - sin2_t_r, -sin2_t_i)
	T c_r = T(1) - sin2_t_r;
	T c_i = -sin2_t_i;

	// Complex sqrt: sqrt(a + ib) = sqrt((|z|+a)/2) + i*sign(b)*sqrt((|z|-a)/2)
#if defined(__CUDACC__)
	T mag = sqrtf(c_r * c_r + c_i * c_i);
	T cos_t_r = sqrtf(fmaxf(T(0), (mag + c_r) / T(2)));
	T cos_t_i = (c_i >= T(0) ? T(1) : T(-1)) * sqrtf(fmaxf(T(0), (mag - c_r) / T(2)));
#else
	T mag = std::sqrt(c_r * c_r + c_i * c_i);
	T cos_t_r = std::sqrt(std::fmax(T(0), (mag + c_r) / T(2)));
	T cos_t_i = (c_i >= T(0) ? T(1) : T(-1)) * std::sqrt(std::fmax(T(0), (mag - c_r) / T(2)));
#endif

	// r_parl = (eta * cos_i - cos_t) / (eta * cos_i + cos_t)
	// eta * cos_i = (eta_r * cos_i, eta_k * cos_i)  (cos_i is real)
	T ec_r = eta_r * cos_theta_i - cos_t_r;
	T ec_i = eta_k * cos_theta_i - cos_t_i;
	T ed_r = eta_r * cos_theta_i + cos_t_r;
	T ed_i = eta_k * cos_theta_i + cos_t_i;
	T ed_sq = ed_r * ed_r + ed_i * ed_i;
	// complex divide: (a+ib)/(c+id) = ((ac+bd) + i(bc-ad)) / (c^2+d^2)
	T rp_r = (ec_r * ed_r + ec_i * ed_i) / ed_sq;
	T rp_i = (ec_i * ed_r - ec_r * ed_i) / ed_sq;
	T r_parl_sq = rp_r * rp_r + rp_i * rp_i;  // |r_parl|^2

	// r_perp = (cos_i - eta * cos_t) / (cos_i + eta * cos_t)
	// eta * cos_t = (eta_r * cos_t_r - eta_k * cos_t_i, eta_r * cos_t_i + eta_k * cos_t_r)
	T etc_r = eta_r * cos_t_r - eta_k * cos_t_i;
	T etc_i = eta_r * cos_t_i + eta_k * cos_t_r;
	T nc_r = cos_theta_i - etc_r;
	T nc_i = -etc_i;
	T nd_r = cos_theta_i + etc_r;
	T nd_i = etc_i;
	T nd_sq = nd_r * nd_r + nd_i * nd_i;
	T rs_r = (nc_r * nd_r + nc_i * nd_i) / nd_sq;
	T rs_i = (nc_i * nd_r - nc_r * nd_i) / nd_sq;
	T r_perp_sq = rs_r * rs_r + rs_i * rs_i;  // |r_perp|^2

	return (r_parl_sq + r_perp_sq) / T(2);
}

// ---------------------------------------------------------------------------
// FrComplex(cosTheta, complex<T>) -- std::complex convenience overload (CPU)
// Matches the pbrt-v4 signature: FrComplex(Float cosTheta_i, complex<Float> eta)
// Reference: pbrt-v4 util/scattering.h
// ---------------------------------------------------------------------------
#if !defined(__CUDACC__)
template <typename T>
inline T FrComplex(T cos_theta_i, std::complex<T> eta) {
	return FrComplex(cos_theta_i, static_cast<T>(eta.real()), static_cast<T>(eta.imag()));
}
#endif

// ---------------------------------------------------------------------------
// FrThinFilm -- a thin transparent film (index film_eta, film_nm nanometres thick) on a conductor (eta_r + i eta_k), seen from the air: Airy's sum of
// the light reflected at the film's top and at its foot after any number of trips across it, which interfere - the colours of a soap film, an oil slick,
// a beetle's or a hummingbird's shell, a dragonfly's wing. r = (r01 + r12 e^(i delta)) / (1 + r01 r12 e^(i delta)) for each polarisation, delta =
// 4 pi film_eta film_nm cos(theta_film) / lambda; R = (|r_s|^2 + |r_p|^2) / 2. With no film (film_nm 0, or film_eta 1) it is FrComplex exactly. Not in
// pbrt-v4 (its ThinFilm is not a material): this renderer's own, on the conductor ("float filmthickness", "float filmeta").
// FrThinFilmRGB: each channel the mean of three wavelengths across its band (so a thick film greys out as the eye sees it, rather than ringing).
// ---------------------------------------------------------------------------
namespace thin_film_detail {
template <typename T>
struct Cx {
	T re, im;
};
template <typename T> CPU_GPU Cx<T> add(Cx<T> a, Cx<T> b) { return {a.re + b.re, a.im + b.im}; }
template <typename T> CPU_GPU Cx<T> sub(Cx<T> a, Cx<T> b) { return {a.re - b.re, a.im - b.im}; }
template <typename T> CPU_GPU Cx<T> mul(Cx<T> a, Cx<T> b) { return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re}; }
template <typename T> CPU_GPU Cx<T> div(Cx<T> a, Cx<T> b) {
	const T d = b.re * b.re + b.im * b.im;
	return {(a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d};
}
template <typename T> CPU_GPU Cx<T> csqrt(Cx<T> z) {   // (the root with a non-negative real part)
#if defined(__CUDACC__)
	const T m = sqrtf(z.re * z.re + z.im * z.im);
	return {sqrtf(fmaxf(T(0), (m + z.re) / T(2))), (z.im >= T(0) ? T(1) : T(-1)) * sqrtf(fmaxf(T(0), (m - z.re) / T(2)))};
#else
	const T m = std::sqrt(z.re * z.re + z.im * z.im);
	return {std::sqrt(std::fmax(T(0), (m + z.re) / T(2))), (z.im >= T(0) ? T(1) : T(-1)) * std::sqrt(std::fmax(T(0), (m - z.re) / T(2)))};
#endif
}
template <typename T> CPU_GPU T norm2(Cx<T> z) { return z.re * z.re + z.im * z.im; }
}  // namespace thin_film_detail

template <typename T>
CPU_GPU T FrThinFilm(T cos_theta_i, T lambda_nm, T film_nm, T film_eta, T eta_r, T eta_k) {
	using namespace thin_film_detail;
#if defined(__CUDACC__)
	cos_theta_i = fmaxf(T(0), fminf(T(1), cos_theta_i));
	const T sin2_i = T(1) - cos_theta_i * cos_theta_i, n1 = fmaxf(film_eta, T(1));
	const T cos1 = sqrtf(fmaxf(T(0), T(1) - sin2_i / (n1 * n1)));
	const T delta = T(4.0 * 3.14159265358979323846) * n1 * film_nm * cos1 / lambda_nm;
	const Cx<T> phase{cosf(delta), sinf(delta)};
#else
	cos_theta_i = std::fmax(T(0), std::fmin(T(1), cos_theta_i));
	const T sin2_i = T(1) - cos_theta_i * cos_theta_i, n1 = std::fmax(film_eta, T(1));
	const T cos1 = std::sqrt(std::fmax(T(0), T(1) - sin2_i / (n1 * n1)));
	const T delta = T(4.0 * 3.14159265358979323846) * n1 * film_nm * cos1 / lambda_nm;
	const Cx<T> phase{std::cos(delta), std::sin(delta)};
#endif
	const Cx<T> n2{eta_r, eta_k}, one{T(1), T(0)};
	const Cx<T> cos2 = csqrt(sub(one, div(Cx<T>{sin2_i, T(0)}, mul(n2, n2))));   // (Snell from the air: n1 sin1 = sin_i)
	// the film's top (air to film: real) and its foot (film to conductor: complex), s and p
	const T r01s = (cos_theta_i - n1 * cos1) / (cos_theta_i + n1 * cos1), r01p = (n1 * cos_theta_i - cos1) / (n1 * cos_theta_i + cos1);
	const Cx<T> a{n1 * cos1, T(0)}, b = mul(n2, cos2), c = mul(n2, Cx<T>{cos1, T(0)}), d{n1 * cos2.re, n1 * cos2.im};
	const Cx<T> r12s = div(sub(a, b), add(a, b)), r12p = div(sub(c, d), add(c, d));
	const auto airy = [&](T r01, Cx<T> r12) {
		const Cx<T> e = mul(r12, phase);
		return norm2(div(add(Cx<T>{r01, T(0)}, e), add(one, Cx<T>{r01 * e.re, r01 * e.im})));
	};
	return (airy(r01s, r12s) + airy(r01p, r12p)) / T(2);
}
template <typename T>
CPU_GPU void FrThinFilmRGB(T cos_theta_i, T film_nm, T film_eta, T eta_r, T eta_g, T eta_b, T k_r, T k_g, T k_b, T &fr, T &fg, T &fb) {
	fr = fg = fb = T(0);
	for (int i = 0; i < 3; ++i) {
		fr += FrThinFilm(cos_theta_i, T(590 + 30 * i), film_nm, film_eta, eta_r, k_r) / T(3);
		fg += FrThinFilm(cos_theta_i, T(510 + 30 * i), film_nm, film_eta, eta_g, k_g) / T(3);
		fb += FrThinFilm(cos_theta_i, T(430 + 30 * i), film_nm, film_eta, eta_b, k_b) / T(3);
	}
}

// ---------------------------------------------------------------------------
// FrConductorRGB -- evaluate FrComplex for R, G, B channels simultaneously
// Returns reflectance as (F_R, F_G, F_B).
// Mirrors pbrt-v4 SampledSpectrum FrComplex(cosTheta, eta[], k[]) but
// for exactly 3 RGB channels instead of NSpectrumSamples=4.
// ---------------------------------------------------------------------------
#if defined(__CUDACC__)
// GPU version: operates on float3
CPU_GPU float3 FrConductorRGB(float cos_theta_i,
									  float eta_r, float eta_g, float eta_b,
									  float k_r,   float k_g,   float k_b) {
	return make_float3(FrComplex(cos_theta_i, eta_r, k_r),
					   FrComplex(cos_theta_i, eta_g, k_g),
					   FrComplex(cos_theta_i, eta_b, k_b));
}
// The same with a thin film on the conductor (film_nm 0: none): MaterialData::filmThickness / filmEta.
CPU_GPU float3 FrConductorRGB(float cos_theta_i,
									  float eta_r, float eta_g, float eta_b,
									  float k_r,   float k_g,   float k_b, float film_nm, float film_eta) {
	if (film_nm <= 0.0f) return FrConductorRGB(cos_theta_i, eta_r, eta_g, eta_b, k_r, k_g, k_b);
	float3 f;
	FrThinFilmRGB(cos_theta_i, film_nm, film_eta, eta_r, eta_g, eta_b, k_r, k_g, k_b, f.x, f.y, f.z);
	return f;
}
#endif  // __CUDACC__

// ---------------------------------------------------------------------------
// FresnelMoment1 -- first Fresnel moment integral (pbrt-v4 scattering.cpp)
// Used by NormalizedFresnelBxDF: c = 1 - 2 * FresnelMoment1(1/eta)
// Polynomial fit in two branches (eta < 1 and eta >= 1).
// ---------------------------------------------------------------------------
template <typename T>
CPU_GPU T FresnelMoment1(T eta) {
	T eta2 = eta * eta, eta3 = eta2 * eta, eta4 = eta3 * eta, eta5 = eta4 * eta;
	if (eta < T(1))
		return T(0.45966f) - T(1.73965f)*eta + T(3.37668f)*eta2
			 - T(3.904945f)*eta3 + T(2.49277f)*eta4 - T(0.68441f)*eta5;
	else
		return T(-4.61686f) + T(11.1136f)*eta - T(10.4646f)*eta2
			 + T(5.11455f)*eta3 - T(1.27198f)*eta4 + T(0.12746f)*eta5;
}
