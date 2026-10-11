#pragma once
// medium_clip.h -- the part of a ray that is inside the sphere a medium is bounded by.
//
// In pbrt-v4 a medium fills the SHAPE its MediumInterface is attached to; the box its density is declared in (a cloud's unit cube, a grid's bounds) only says where the
// density is defined. A cloud or grid whose box sticks out of its interface sphere is therefore cut round by the sphere. The CPU's heterogeneous media (cloud, RGB grid,
// uniform grid) were added to the scene as independent boxes and ignored the sphere; with this each can be given the sphere it sits on and shortens every segment it walks (a
// free flight, a shadow ray's transmittance) to the chord inside it, as both OptiX backends and Metal do.
//
// std-only, header-only.

#include <algorithm>
#include <cmath>

struct MediumSphereClip {
	bool on = false;
	double cx = 0.0, cy = 0.0, cz = 0.0, radius = 0.0;

	void set(double x, double y, double z, double r) {
		cx = x; cy = y; cz = z; radius = r;
		on = r > 0.0;
	}

	// [t0, t1] shortened to where the ray o + t d (d a UNIT vector, t in world distance) is inside the sphere. False if nothing is left (or the ray misses the sphere).
	// Without a sphere (on == false) the segment is left alone.
	bool apply(double ox, double oy, double oz, double dx, double dy, double dz, double& t0, double& t1) const {
		if (!on) return true;
		const double px = ox - cx, py = oy - cy, pz = oz - cz;
		const double b = px * dx + py * dy + pz * dz;
		// a (r + |v|) (r - |v|) with v the closest approach: the same improved-precision form the sphere's own intersection uses (pbrt-v4 Sphere::BasicIntersect)
		const double vx = px - b * dx, vy = py - b * dy, vz = pz - b * dz;
		const double vlen = std::sqrt(vx * vx + vy * vy + vz * vz);
		const double disc = (radius + vlen) * (radius - vlen);
		if (!(disc > 0.0)) return false;
		const double root = std::sqrt(disc);
		t0 = std::max(t0, -b - root);
		t1 = std::min(t1, -b + root);
		return t0 < t1;
	}
};
