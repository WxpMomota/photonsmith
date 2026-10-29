#pragma once
// bevel_normal.h -- the bevel shader (src/shared/bevel.h: Cycles' bevel) on the CPU path tracer: at a hit on a material with a bevel radius, the shading normal becomes the
// average of the surface's outward normals found by probe rays through a disk round the point (each probe walks its segment through every surface of the same material),
// weighted by the cubic falloff and the three axes' multiple importance sampling. camera_path.h calls it right after a path's hit.

#include "../shared/bevel.h"
#include "../shared/proxy_normal.h"   // proxy_shading_normal_of
#include "hittable.h"
#include "rtweekend.h"

namespace bevel_normal {

// The bevelled normal for `rec` (on the side the ray came from, as rec.normal is), or rec.normal when no surface was found.
inline vec3 at(const hittable &world, const hit_record &rec, double radius, double time) {
	const vec3 N = unit_vector(rec.normal);
	const vec3 T = unit_vector(std::fabs(N.x()) > 0.9 ? cross(vec3(0, 1, 0), N) : cross(vec3(1, 0, 0), N));
	const vec3 B = cross(N, T);
	const vec3 axes[3] = {N, T, B};
	const material *self = rec.mat.get();
	vec3 sum(0, 0, 0);
	for (int s = 0; s < bevel::kSamples; ++s) {
		double u = random_double(), pdf[3];
		const int ax = bevel::pickAxis(u, pdf);
		const vec3 diskN = axes[ax], diskT = axes[(ax + 1) % 3], diskB = axes[(ax + 2) % 3];
		double r = 0.0, h = 0.0;
		bevel::cubicSample(radius, random_double(), r, h);
		if (h <= 0.0) continue;
		const double phi = 2.0 * pi * u;
		point3 base = rec.p + h * diskN + r * (std::cos(phi) * diskT + std::sin(phi) * diskB);
		const vec3 dir = -diskN;
		double remaining = 2.0 * h;
		for (int k = 0; k < 8 && remaining > 1e-9; ++k) {   // (every surface along the probe, as Cycles' local intersection)
			hit_record hit;
			if (!world.hit(ray(base, dir, time), interval(1e-6, remaining), hit)) break;
			if (hit.mat.get() == self) {
				const vec3 outward = hit.front_face ? hit.normal : -hit.normal;   // (the shape's own outward normal: hit.normal faces the probe)
				const double dots[3] = {dot(N, outward), dot(T, outward), dot(B, outward)};
				sum += bevel::weight(dots, pdf, ax, radius, (hit.p - rec.p).length(), r) * outward;
			}
			const double step = hit.t + 1e-4;
			base = base + step * dir;
			remaining -= step;
		}
	}
	if (!(sum.length_squared() > 1e-24)) return rec.normal;
	const vec3 out = unit_vector(sum);
	return dot(out, rec.normal) < 0.0 ? -out : out;   // (on the side the ray came from)
}

}  // namespace bevel_normal

// The shading normal of `rec` taken towards its material's proxy sphere (src/shared/proxy_normal.h: a cartoon face's clean cel shadows); rec.normal when it has none.
// (rec.mat's material is known to be material_base's: the caller has read proxy_blend through it.)
template <class Hit>
inline vec3 proxy_shading_normal_of(const Hit &rec) {
	const auto &m = *rec.mat;
	const double p[3] = {rec.p.x(), rec.p.y(), rec.p.z()}, n[3] = {rec.normal.x(), rec.normal.y(), rec.normal.z()};
	double out[3];
	proxy_normal::apply(p, n, m.proxy_center, m.proxy_radius, m.proxy_blend, out);
	return vec3(out[0], out[1], out[2]);
}
