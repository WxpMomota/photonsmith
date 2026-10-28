#pragma once
// pixel_convergence_rule.h -- when a pixel has enough samples (adaptive sampling, --adaptive): the one rule the CPU path tracer
// (adaptive_sampling.h's pixel_convergence::has_converged) and both OptiX renderers (optix_raygen.h, the wavefront renderer's
// adaptive_sampling_update kernel) apply to a running estimate of a pixel's brightness. Plain arithmetic, no containers, so device code can use it.
#include <cmath>
#include "cpu_gpu.h"

namespace pixel_convergence {

// True once the noise left in a pixel would be below `threshold` on the screen. The pixel's brightness estimate is `count` samples
// (count >= 2) with Welford running mean `mean` and sum of squared deviations `m2`, in linear radiance; `exposure` scales it to the
// picture. Its standard error is carried to display values through a gamma-2.2 response (d = L^(1/2.2), so dd = dL / 2.2 * L^(1/2.2 - 1)),
// so `threshold` is a noise in display units (0.005: a little over one level of 255) - a dark pixel may keep more noise relative to its
// mean than a bright one, as the eye sees it. (A relative error - standard error over the mean - asked a dark pixel in fog for
// thousands of samples, so on a dim scene nothing stopped early at 500.) A pixel darker than 1e-4 counts as converged black.
template <class T>
CPU_GPU bool converged_by_standard_error(T mean, T standardError, T threshold, T exposure) {
	const T L = mean * exposure;
	if (L < T(1e-4)) return true;
	const T displayNoise = standardError * exposure / T(2.2) * std::pow(L, T(1.0 / 2.2 - 1.0));
	return displayNoise < threshold;
}

template <class T>
CPU_GPU bool converged(T count, T mean, T m2, T threshold, T exposure) {
	if (count < T(2)) return false;
	return converged_by_standard_error(mean, std::sqrt(m2 / (count - T(1)) / count), threshold, exposure);
}

// One more sample `x` into a Welford running mean and sum of squared deviations (count already incremented to include it).
template <class T>
CPU_GPU void welford_add(T count, T x, T& mean, T& m2) {
	const T d = x - mean;
	mean += d / count;
	m2 += d * (x - mean);
}

// The same for the mean `x` of a batch of `k` samples (West's weighted form; `samples` already includes the batch). With q summed over b
// batches, q / (b - 1) estimates one sample's variance - a batch mean's is that over k - however the batch sizes vary.
template <class T>
CPU_GPU void weighted_add(T samples, T k, T x, T& mean, T& q) {
	const T d = x - mean;
	mean += d * k / samples;
	q += k * d * (x - mean);
}

}  // namespace pixel_convergence
