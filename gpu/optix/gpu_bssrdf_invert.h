#pragma once
// gpu_bssrdf_invert.h -- pbrt-v4's SubsurfaceFromDiffuse on the device: the single-scattering albedo whose effective albedo through a beam-diffusion table is a
// given reflectance (src/shared/bssrdf.h's invert_catmull_rom, in float). A Subsurface material whose "reflectance" is an image needs it at every hit
// (SubsurfaceMaterial::GetBSSRDF inverts the table for the texel's value there); a constant reflectance is inverted once when the scene is built.
//
// Pointer-based so the recursive backend (params.*) and the wavefront backend (wf_params.*) share one copy. No lambdas: this backend's device code is compiled
// without --extended-lambda (see optix_bssrdf.h).
//
// The table's effective albedo of row i (rho = rho_samples[i]) is not uploaded on its own: it is the last column of that row's profile CDF
// (GpuBssrdfTable's own comment), so `profile_cdf` here is the table's base into the flat bssrdfProfileCdf buffer and row i's value is
// profile_cdf[i * n_radius + n_radius - 1].

__device__ __forceinline__ float gpu_bssrdf_rho_eff_at(const float* profile_cdf, int n_radius, int row) {
	return profile_cdf[row * n_radius + (n_radius - 1)];
}

// The albedo rho (a value in [rho_samples[0], rho_samples[n_rho-1]]) whose effective albedo is `target`; the ends clamp, as pbrt's does.
__device__ __forceinline__ float gpu_bssrdf_invert_rho_eff(const float* rho_samples, const float* profile_cdf, int n_rho, int n_radius, float target) {
	if (!(target > gpu_bssrdf_rho_eff_at(profile_cdf, n_radius, 0))) return rho_samples[0];
	if (!(target < gpu_bssrdf_rho_eff_at(profile_cdf, n_radius, n_rho - 1))) return rho_samples[n_rho - 1];

	// the segment i with f[i] <= target < f[i+1]
	int lo = 0, hi = n_rho - 1;
	while (hi - lo > 1) {
		const int mid = (lo + hi) / 2;
		if (gpu_bssrdf_rho_eff_at(profile_cdf, n_radius, mid) <= target) lo = mid; else hi = mid;
	}
	const int i = lo;
	const float x0 = rho_samples[i], x1 = rho_samples[i + 1];
	const float f0 = gpu_bssrdf_rho_eff_at(profile_cdf, n_radius, i), f1 = gpu_bssrdf_rho_eff_at(profile_cdf, n_radius, i + 1);
	const float width = x1 - x0;
	// pbrt's finite-difference tangents at the segment's ends
	const float d0 = (i > 0) ? width * (f1 - gpu_bssrdf_rho_eff_at(profile_cdf, n_radius, i - 1)) / (x1 - rho_samples[i - 1]) : (f1 - f0);
	const float d1 = (i + 2 < n_rho) ? width * (gpu_bssrdf_rho_eff_at(profile_cdf, n_radius, i + 2) - f0) / (rho_samples[i + 2] - x0) : (f1 - f0);

	// the cubic Hermite through (f0, f1, d0, d1) is monotone on the segment: bisect on t in [0, 1]
	float t_lo = 0.0f, t_hi = 1.0f;
	for (int it = 0; it < 30; ++it) {
		const float t = 0.5f * (t_lo + t_hi);
		const float t2 = t * t, t3 = t2 * t;
		const float F = (2.0f * t3 - 3.0f * t2 + 1.0f) * f0 + (-2.0f * t3 + 3.0f * t2) * f1 + (t3 - 2.0f * t2 + t) * d0 + (t3 - t2) * d1;
		if (F < target) t_lo = t; else t_hi = t;
	}
	return x0 + 0.5f * (t_lo + t_hi) * width;
}

// sigma_a/sigma_s of one channel for a reflectance and a mean free path (pbrt-v4 SubsurfaceFromDiffuse).
__device__ __forceinline__ void gpu_bssrdf_sigma_from_diffuse(const float* rho_samples, const float* profile_cdf, int n_rho, int n_radius,
                                                              float reflectance, float mfp, float& sigma_a, float& sigma_s) {
	const float rho = gpu_bssrdf_invert_rho_eff(rho_samples, profile_cdf, n_rho, n_radius, fminf(fmaxf(reflectance, 0.0f), 1.0f));
	const float length = fmaxf(mfp, 1e-9f);
	sigma_s = rho / length;
	sigma_a = (1.0f - rho) / length;
}
