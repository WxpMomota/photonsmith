#pragma once
// film_subjects.h -- where a film's subjects fall in its frame (`ray_tracer --film-subjects <key> <t> <width> <height>`), for the film doctor's framing checks
// (scripts/anim/film.py): each subject (Film::subjectsAt, story space) is taken through the camera of the moment's own scene header (its LookAt and fov) to a pixel and a
// distance. The doctor then warns when a subject is out of the frame or behind the camera, and - comparing the distance with the depth the renderer found at that pixel - when
// something nearer hides it.

#include <cmath>
#include <regex>
#include <string>
#include <vector>

#include "cartoon_lib.h"

namespace film_subjects {

struct View {
	double x = 0.0, y = 0.0;   // the pixel (from the top left)
	double distance = 0.0;     // from the camera
	bool inFront = false;      // not behind the camera
	bool inFrame = false;      // in front and inside the picture
};

// The camera of a scene header: its eye and target in story space (the header's LookAt is pbrt's: x negated) and its field of view (pbrt's: across the shorter side). False when
// the header has no LookAt.
inline bool cameraOf(const std::string &header, cartoon::Vec3 &eye, cartoon::Vec3 &target, double &fov) {
	static const std::regex look(R"(LookAt\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+))");
	static const std::regex fovRe(R"("float fov"\s*\[\s*(\S+))");
	std::smatch m;
	if (!std::regex_search(header, m, look)) return false;
	eye = {-std::stod(m[1].str()), std::stod(m[2].str()), std::stod(m[3].str())};
	target = {-std::stod(m[4].str()), std::stod(m[5].str()), std::stod(m[6].str())};
	fov = std::regex_search(header, m, fovRe) ? std::stod(m[1].str()) : 90.0;
	return true;
}

inline View project(cartoon::Vec3 eye, cartoon::Vec3 target, double fov, cartoon::Vec3 p, int width, int height) {
	using cartoon::Vec3;
	View v;
	const Vec3 f = cartoon::normalize(target - eye);
	const Vec3 right = cartoon::normalize(cartoon::cross(f, Vec3{0.0, 1.0, 0.0}));
	const Vec3 up = cartoon::cross(right, f);
	const Vec3 d = p - eye;
	v.distance = cartoon::length(d);
	const double depth = cartoon::dot(d, f);
	v.inFront = depth > 1e-6;
	if (!v.inFront) return v;
	const double tanHalf = std::tan(fov * cartoon::kPi / 360.0);
	const double aspect = static_cast<double>(width) / std::max(1, height);
	// (pbrt's fov is across the shorter side of the picture)
	const double sx = aspect >= 1.0 ? tanHalf * aspect : tanHalf, sy = aspect >= 1.0 ? tanHalf : tanHalf / aspect;
	const double nx = dot(d, right) / (depth * sx), ny = dot(d, up) / (depth * sy);
	v.x = (nx + 1.0) * 0.5 * width;
	v.y = (1.0 - ny) * 0.5 * height;
	v.inFrame = std::abs(nx) <= 1.0 && std::abs(ny) <= 1.0;
	return v;
}

}  // namespace film_subjects
