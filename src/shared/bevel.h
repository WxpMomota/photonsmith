#pragma once
// bevel.h -- rounded edges without rounded geometry: the bevel shader of Blender's Cycles (intern/cycles/kernel/svm/bevel.h, Apache-2.0, ported with credit).
// A box's hard edge catches no light along it; a real one, slightly rounded, shows a thin bright line where it turns - the soft, solid look of toys, furniture and
// cartoon sets. At a hit on a material with a bevel radius, the shading normal becomes the average of the normals of the surface found round the point: probe rays
// through a disk of that radius about the point (along one of three axes: the normal half of the time, either tangent a quarter each, as for subsurface scattering:
// Christensen and King's "BSSRDF importance sampling", 2013) are traced against the same material, and every surface they cross adds its normal, weighted by
// a cubic falloff with distance and by multiple importance sampling between the three axes. Near an edge some of those surfaces are the other face, so the normal
// leans round the edge within the radius; on a flat face they all agree and nothing changes.
//
// In a pbrt scene (our extension): `"float bevel" [ 0.02 ]` on a Material (metres of rounding). The CPU path tracer and both OptiX renderers; the bidirectional,
// Metropolis and photon-mapping integrators and Metal shade the plain normal.

#include <cmath>

#include "cpu_gpu.h"

namespace bevel {

constexpr int kSamples = 8;   // probe rays a hit (Cycles' default)

// Cycles' planar cubic falloff: 10 (R - r)^3 / (pi R^5) for r < R.
CPU_GPU double cubicPdf(double radius, double r) {
	if (r >= radius || radius <= 0.0) return 0.0;
	const double R5 = radius * radius * radius * radius * radius, f = radius - r;
	return 10.0 * f * f * f / (R5 * 3.14159265358979323846);
}

// A radius from the falloff (Newton-Raphson on 10x^2 - 20x^3 + 15x^4 - 4x^5 = xi), and the half height of the probe segment there.
CPU_GPU void cubicSample(double radius, double xi, double &r, double &h) {
	double x = 0.25;
	for (int i = 0; i < 10; ++i) {
		const double x2 = x * x, x3 = x2 * x, nx = 1.0 - x;
		const double f = 10.0 * x2 - 20.0 * x3 + 15.0 * x2 * x2 - 4.0 * x2 * x3 - xi, fd = 20.0 * (x * nx) * (nx * nx);
		if (std::fabs(f) < 1e-6 || fd == 0.0) break;
		x = x - f / fd;
		x = x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
	}
	r = x * radius;
	const double hh = radius * radius - r * r;
	h = hh > 0.0 ? std::sqrt(hh) : 0.0;
}

// Which axis a probe goes along (0 the normal, 1 and 2 the tangents) from a random number in [0, 1), with its probabilities; `u` is rescaled to [0, 1) for reuse.
CPU_GPU int pickAxis(double &u, double pdf[3]) {
	if (u < 0.5) {
		pdf[0] = 0.5, pdf[1] = 0.25, pdf[2] = 0.25;
		u *= 2.0;
		return 0;
	}
	if (u < 0.75) {
		pdf[0] = 0.25, pdf[1] = 0.5, pdf[2] = 0.25;
		u = (u - 0.5) * 4.0;
		return 1;
	}
	pdf[0] = 0.25, pdf[1] = 0.25, pdf[2] = 0.5;
	u = (u - 0.75) * 4.0;
	return 2;
}

// The weight of a surface a probe found: `dots` its geometric normal against the three axes (normal, tangent 1, tangent 2: the axis frame, not the one picked),
// `picked` the axis the probe went along, `found` its distance from the shading point, `diskR` the radius sampled.
CPU_GPU double weight(const double dots[3], const double pickPdf[3], int picked, double radius, double found, double diskR) {
	double p[3];
	for (int k = 0; k < 3; ++k) p[k] = pickPdf[k] * std::fabs(dots[k]);
	const double denom = p[0] * p[0] + p[1] * p[1] + p[2] * p[2];
	if (denom <= 0.0) return 0.0;
	const double diskPdf = cubicPdf(radius, diskR);
	if (diskPdf <= 0.0) return 0.0;
	return p[picked] / denom * cubicPdf(radius, found) / diskPdf;
}

}  // namespace bevel
