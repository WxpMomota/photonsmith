#pragma once
// film_checks.h -- checks of a film's motion that need no render: sliding feet, and screen direction across a cut (below). Sliding feet: a foot the walk means to be on the ground (Film::feetAt, Foot::planted) should stay where it
// came down; one that moves while planted skates. Each shot is sampled at the film's frame rate, and the fastest a planted foot moves between two frames is reported
// (`ray_tracer --film-feet <key>`, and the film doctor warns on it).

#include <algorithm>
#include <cstdlib>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_subjects.h"

namespace cartoon {
namespace film_checks {

// What the sliding-feet check found in one shot.
struct Slide {
	int shot = 0;            // from 0
	double worst = 0.0;      // the fastest a planted foot moved, units a second
	double at = 0.0;         // when
	double mean = 0.0;       // its mean over the planted frames
	int planted = 0;         // how many (foot, frame) pairs were planted on both frames
};

// A planted foot faster than this (units a second) skates visibly: 10 cm a second, a fifth of a slow walk.
constexpr double kSlideLimit = 0.10;

// Every shot in which a foot is planted at some frame, with how fast the planted feet moved. Shots with no planted feet are left out.
inline std::vector<Slide> slidingFeet(const Film &film, int fps = 0) {
	std::vector<Slide> out;
	if (!film.feetAt) return out;
	const double dt = 1.0 / std::max(1, fps > 0 ? fps : film.fps);
	const std::vector<double> starts = film.shotStarts ? film.shotStarts() : std::vector<double>{0.0};
	for (std::size_t s = 0; s < starts.size(); ++s) {
		const double end = s + 1 < starts.size() ? starts[s + 1] : film.duration;
		Slide slide;
		slide.shot = static_cast<int>(s);
		double sum = 0.0;
		std::vector<Foot> before = film.feetAt(starts[s]);
		for (double t = starts[s] + dt; t < end - 1e-9; t += dt) {
			const std::vector<Foot> now = film.feetAt(t);
			for (std::size_t i = 0; i < std::min(before.size(), now.size()); ++i) {
				if (!before[i].planted || !now[i].planted) continue;
				const double speed = length(now[i].at - before[i].at) / dt;
				sum += speed;
				++slide.planted;
				if (speed > slide.worst) {
					slide.worst = speed;
					slide.at = t;
				}
			}
			before = now;
		}
		if (slide.planted == 0) continue;
		slide.mean = sum / slide.planted;
		out.push_back(slide);
	}
	return out;
}

// ---- screen direction across a cut ----
// At each cut, what the shots on either side are about (Film::subjectsAt: subject 0, and subject 1 when both shots name two) is taken through each side's own camera
// (film_subjects.h). Two things confuse an audience: the same moving thing going left-to-right before the cut and right-to-left after it (a "flip": for one thing in one place a
// warning; for a traveller cut to another place on the journey a note - a journey should keep its direction across the screen), and two subjects that swap sides on the
// screen (the camera crossed the line between them).
struct CutIssue {
	int shot = 0;          // the shot after the cut, from 0
	bool samePlace = false;// the subject is where it was (one thing, one place) rather than somewhere else on its way
	bool crossedLine = false;   // two subjects swapped sides (else: a flip of the direction of motion)
	double before = 0.0, after = 0.0;   // the screen motion (picture widths a second, + to the right) or the subjects' order (x of 0 minus x of 1, in widths)
};
constexpr double kScreenSpeed = 0.02;   // screen motion slower than this (picture widths a second) has no direction to keep
constexpr double kSamePlace = 1.0;      // a subject this near (story units) to the one before the cut is the same thing in the same place

inline std::vector<CutIssue> screenDirection(const Film &film) {
	std::vector<CutIssue> out;
	if (!film.subjectsAt || !film.headerAt || !film.shotStarts) return out;
	const int width = film.meta.width, height = film.meta.height;
	const std::vector<double> starts = film.shotStarts();
	const double dt = 1.0 / std::max(1, film.fps), eps = 1e-4;
	// where subject i is on the screen at time t (the camera of `at`'s moment), and whether it is in front
	const auto screen = [&](double at, double t, std::size_t i, double &x, Vec3 &world) {
		Vec3 eye, target;
		double fov = 90.0;
		if (!film_subjects::cameraOf(film.headerAt(at), eye, target, fov)) return false;
		const std::vector<Vec3> s = film.subjectsAt(t);
		if (s.size() <= i) return false;
		world = s[i];
		const film_subjects::View v = film_subjects::project(eye, target, fov, s[i], width, height);
		x = v.x / std::max(1, width);
		return v.inFront;
	};
	for (std::size_t k = 1; k < starts.size(); ++k) {
		const double cut = starts[k];
		const double a0 = cut - eps - 4.0 * dt, a1 = cut - eps, b0 = cut, b1 = cut + 4.0 * dt;
		if (a0 <= starts[k - 1] || (k + 1 < starts.size() && b1 >= starts[k + 1])) continue;   // (a shot too short to show a direction)
		// subject 0's motion on each side, each in its own camera (the camera of the moment held for both instants, so a camera move is not taken for the subject's)
		double xa0 = 0, xa1 = 0, xb0 = 0, xb1 = 0;
		Vec3 wa0, wa1, wb0, wb1;
		if (!screen(a1, a0, 0, xa0, wa0) || !screen(a1, a1, 0, xa1, wa1) || !screen(b0, b0, 0, xb0, wb0) || !screen(b0, b1, 0, xb1, wb1)) continue;
		const double before = (xa1 - xa0) / (4.0 * dt), after = (xb1 - xb0) / (4.0 * dt);
		// (a direction across the screen only when the motion is more across the view than into it or out of it: a walk away from the camera has none to keep)
		const auto across = [&](double at, Vec3 motion) {
			Vec3 eye, target;
			double fov = 90.0;
			if (!film_subjects::cameraOf(film.headerAt(at), eye, target, fov) || length(motion) < 1e-9) return false;
			const Vec3 f = normalize(target - eye), right = normalize(cross(f, Vec3{0.0, 1.0, 0.0}));
			return std::abs(dot(motion, right)) > std::abs(dot(motion, f));
		};
		if (across(a1, wa1 - wa0) && across(b0, wb1 - wb0) && std::abs(before) > kScreenSpeed && std::abs(after) > kScreenSpeed && (before > 0) != (after > 0)) {
			CutIssue c;
			c.shot = static_cast<int>(k);
			c.samePlace = length(wb0 - wa1) < kSamePlace;
			c.before = before;
			c.after = after;
			out.push_back(c);
		}
		// two subjects on both sides, the same two (each near its own across the cut): do they keep their sides?
		double ya = 0, yb = 0;
		Vec3 va, vb;
		if (screen(a1, a1, 1, ya, va) && screen(b0, b0, 1, yb, vb) && length(vb - va) < kSamePlace && length(wb0 - wa1) < kSamePlace) {
			const double orderA = xa1 - ya, orderB = xb0 - yb;
			if (std::abs(orderA) > 0.02 && std::abs(orderB) > 0.02 && (orderA > 0) != (orderB > 0)) {
				CutIssue c;
				c.shot = static_cast<int>(k);
				c.samePlace = true;
				c.crossedLine = true;
				c.before = orderA;
				c.after = orderB;
				out.push_back(c);
			}
		}
	}
	return out;
}

// ---- the camera inside a medium's sphere ----
// Smoke, flames, a volcano's plume and clouds are media filling a sphere round their box (`Material "interface"`, `MediumInterface "NAME" ""`), the outside being the air.
// The CPU and the wavefront GPU renderer (which the default one hands a frame with anything inside a volume to) sample a volume along each ray up to the nearest surface, so a
// camera inside the sphere is in the smoke, as it should be. The Mac's Metal renderer still samples a volume only where a ray meets its sphere: from inside, the smoke in
// front of anything else inside the sphere is missing. (Mist and the sea round the camera are spheres of the camera's own medium: not reported.) `ray_tracer --film-media
// <key>`; the film doctor warns on it on a Mac.
struct MediumSphere {
	std::string name;
	Vec3 centre;   // pbrt's
	double radius = 0.0;
};

// The spheres of the media in a scene's text (pbrt's space): the top-level blocks that translate, are of the interface material, have a medium inside and nothing outside,
// and are a sphere. A block that turns or scales is left out (where its sphere is is not read here).
inline std::vector<MediumSphere> mediumSpheres(const std::string &scene) {
	std::vector<MediumSphere> out;
	std::istringstream in(scene);
	int depth = 0;
	MediumSphere s;
	bool interface = false, sphere = false, plain = true;
	std::string inside, outside;
	const auto quoted = [](const std::string &line, std::size_t from, std::string &a, std::string &b) {
		std::vector<std::string> q;
		for (std::size_t i = line.find('"', from); i != std::string::npos && q.size() < 2;) {
			const std::size_t j = line.find('"', i + 1);
			if (j == std::string::npos) break;
			q.push_back(line.substr(i + 1, j - i - 1));
			i = line.find('"', j + 1);
		}
		a = q.size() > 0 ? q[0] : std::string();
		b = q.size() > 1 ? q[1] : a;
	};
	for (std::string line; std::getline(in, line);) {
		const std::size_t at = line.find_first_not_of(" \t");
		if (at == std::string::npos) continue;
		const std::string l = line.substr(at);
		if (l.rfind("AttributeBegin", 0) == 0) {
			if (++depth == 1) s = MediumSphere{}, interface = sphere = false, plain = true, inside.clear(), outside.clear();
		} else if (l.rfind("AttributeEnd", 0) == 0) {
			if (depth-- == 1 && interface && sphere && plain && !inside.empty() && outside.empty()) {
				s.name = inside;
				out.push_back(s);
			}
		} else if (depth == 1) {
			if (l.rfind("Translate", 0) == 0) {
				std::istringstream v(l.substr(9));
				Vec3 d{};
				if (v >> d.x >> d.y >> d.z) s.centre = s.centre + d;
			} else if (l.rfind("Scale", 0) == 0 || l.rfind("Rotate", 0) == 0 || l.rfind("ConcatTransform", 0) == 0 || l.rfind("Transform", 0) == 0) {
				plain = false;
			} else if (l.rfind("Material", 0) == 0) {
				interface = l.find("\"interface\"") != std::string::npos;
			} else if (l.rfind("MediumInterface", 0) == 0) {
				quoted(l, 15, inside, outside);
			} else if (l.rfind("Shape", 0) == 0 && l.find("\"sphere\"") != std::string::npos) {
				sphere = true;
				const std::size_t r = l.find("\"float radius\"");
				const std::size_t b = r == std::string::npos ? std::string::npos : l.find('[', r);
				s.radius = 1.0;   // (pbrt's default)
				if (b != std::string::npos) s.radius = std::atof(l.c_str() + b + 1);
			}
		}
	}
	return out;
}

struct InMedium {
	int shot = 0;          // from 0
	double at = 0.0;       // when (film time)
	std::string name;      // the medium
	double radius = 0.0;   // its sphere's
	double inside = 0.0;   // how far inside it the camera is
};

// Each shot sampled `samples` times (its start, its end and evenly between): the first moment of each shot whose camera is inside the sphere of a medium that is not the
// camera's own, one entry a medium and shot.
inline std::vector<InMedium> cameraInMedia(const Film &film, int samples = 5, int *spheresSeen = nullptr) {   // (`spheresSeen`: the most in one frame)
	std::vector<InMedium> out;
	if (spheresSeen) *spheresSeen = 0;
	if (!film.sceneAt) return out;
	static const std::regex look(R"(LookAt\s+(\S+)\s+(\S+)\s+(\S+))");
	const std::vector<double> starts = film.shotStarts ? film.shotStarts() : std::vector<double>{0.0};
	for (std::size_t s = 0; s < starts.size(); ++s) {
		const double end = s + 1 < starts.size() ? starts[s + 1] : film.duration;
		std::set<std::string> said;
		for (int k = 0; k < std::max(2, samples); ++k) {
			const double t = std::min(starts[s] + (end - starts[s]) * k / (std::max(2, samples) - 1), end - 1e-3);
			const std::string scene = film.sceneAt(t);
			const std::size_t world = scene.find("WorldBegin");
			const std::string header = scene.substr(0, world);
			std::smatch m;
			if (!std::regex_search(header, m, look)) continue;
			const Vec3 eye{std::stod(m[1].str()), std::stod(m[2].str()), std::stod(m[3].str())};
			// the camera's own medium: the outside of the last MediumInterface before the Camera line
			std::string own, a, b;
			const std::size_t camera = header.find("Camera");
			for (std::size_t i = header.find("MediumInterface"); i != std::string::npos && i < camera; i = header.find("MediumInterface", i + 1)) {
				const std::size_t q1 = header.find('"', i), q2 = q1 == std::string::npos ? q1 : header.find('"', q1 + 1);
				const std::size_t q3 = q2 == std::string::npos ? q2 : header.find('"', q2 + 1), q4 = q3 == std::string::npos ? q3 : header.find('"', q3 + 1);
				const std::size_t lineEnd = header.find('\n', i);
				if (q2 == std::string::npos || q2 > lineEnd) continue;
				a = header.substr(q1 + 1, q2 - q1 - 1);
				b = q4 != std::string::npos && q4 < lineEnd ? header.substr(q3 + 1, q4 - q3 - 1) : a;
				own = b;
			}
			const std::vector<MediumSphere> spheres = mediumSpheres(world == std::string::npos ? std::string() : scene.substr(world));
			if (spheresSeen) *spheresSeen = std::max(*spheresSeen, static_cast<int>(spheres.size()));
			for (const MediumSphere &sp : spheres) {
				const double d = length(eye - sp.centre);
				if (d >= sp.radius || sp.name == own || !said.insert(sp.name).second) continue;
				out.push_back({static_cast<int>(s), t, sp.name, sp.radius, sp.radius - d});
			}
		}
	}
	return out;
}

}  // namespace film_checks
}  // namespace cartoon
