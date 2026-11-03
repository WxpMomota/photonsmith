// film_overlays.cpp -- what an explainer draws over a frame of a film made from a file (film_explain.h): labels, orbit lines, scale bars, a comparison, a mission's route and its display.
// DataFilm's member. Compiled once, in cpu_renderer.

#include "film_file.h"

namespace cartoon {
namespace film_file {

std::string DataFilm::overlaysAt(const std::string &header, double t, double nt) const {
	explain::View v;
	film_subjects::cameraOf(header, v.eye, v.target, v.fov);
	v.eye.x = -v.eye.x, v.target.x = -v.target.x;   // (story space to pbrt's, as what is drawn is: a turned camera saw its cards turned the other way)
	const std::string film = spec.key.empty() ? std::string("film") : spec.key;
	std::string out;
	for (const explain::Label &l : spec.explain.labels) {   // what each names: where its middle is now and how far its top is above that (pbrt's)
		Vec3 at = l.at;
		double radius = 0.0;
		bool found = l.on.empty();
		for (const cosmos::Ship &s : spec.ships)
			if (!found && s.name == l.on) {
				if (s.goneAt >= 0.0 && nt > s.goneAt) break;
				at = screens::shipWhere(s, nt), radius = 0.35 * s.size, found = true;
			}
		for (std::size_t i = 0; i < spec.planets.size() && !found; ++i)
			if (spec.planets[i].name == l.on) at = planets::centreAt(spec.planets, i, nt), radius = spec.planets[i].radius, found = true;
		for (const Character &c : spec.characters)
			if (!found && c.name == l.on) {
				const Vec3 head = transformPoint(momentOf(c, t).frame, characterKinds().at(c.kind).head * c.scale);
				at = {-head.x, head.y, head.z}, radius = 0.35 * c.scale, found = true;
			}
		if (found) out += explain::labelText(l, t, at, radius, v, film);
	}
	for (const explain::OrbitLine &o : spec.explain.orbits) {
		std::vector<Vec3> path;
		for (const cosmos::Ship &s : spec.ships)
			if (s.name == o.of && s.orbit.on) path = explain::orbitPath(s.orbit, nt);
		for (std::size_t i = 0; i < spec.planets.size(); ++i) {   // a moon: its loop round where what it goes round is now
			const planets::Planet &p = spec.planets[i];
			if (p.name != o.of || p.around.empty()) continue;
			std::size_t parent = i;
			for (std::size_t k = 0; k < spec.planets.size(); ++k)
				if (spec.planets[k].name == p.around) parent = k;
			const Vec3 now = planets::centreAt(spec.planets, parent, nt);
			for (int k = 0; k <= 160; ++k) {
				const double at = nt + p.period * k / 160.0;
				path.push_back(now + planets::centreAt(spec.planets, i, at) - planets::centreAt(spec.planets, parent, at));
			}
		}
		out += explain::orbitLineText(o, t, path, v);
	}
	for (const explain::ScaleBar &b : spec.explain.bars) out += explain::scaleBarText(b, t, v, film);
	out += night::stampText(spec.night, t, v, film);   // (a trail camera's strip: film_night.h)
	const auto bodyAt = [&](const std::string &name, Vec3 &at) {
		for (std::size_t i = 0; i < spec.planets.size(); ++i)
			if (spec.planets[i].name == name) return at = planets::centreAt(spec.planets, i, nt), true;
		for (const cosmos::Ship &s : spec.ships)
			if (s.name == name) return at = screens::shipWhere(s, nt), true;
		return false;
	};
	for (const explain::Route &r : spec.explain.routes) {
		Vec3 a, b;
		if (t < r.from || !bodyAt(r.a, a) || !bodyAt(r.b, b)) continue;
		const Vec3 span = b - a, bow = normalize(cross(span, Vec3{0.0, 1.0, 0.0}) + Vec3{0.0, 0.0, 1e-6}) * (0.18 * length(span));
		const auto pointAt = [&](double u) { return a + span * u + bow * std::sin(kPi * u); };
		std::vector<Vec3> path;
		for (int k = 0; k <= 80; ++k) path.push_back(pointAt(k / 80.0));
		explain::OrbitLine line;
		line.colour = r.colour, line.dashed = true, line.from = r.from;
		out += explain::orbitLineText(line, t, path, v);
		explain::Label mark;
		mark.text = r.text.empty() ? std::string(">") : r.text, mark.colour = r.colour, mark.from = r.from;
		out += explain::labelText(mark, t, pointAt(std::min(1.0, (t - r.from) / std::max(1e-6, r.over))), 0.0, v, film);
	}
	// the frame's width over its height: 16:9, as a film is rendered (film.py's --height H W; the scene's own Film line is not the size a film is rendered at, and a 4:3 one
	// put the display wrong)
	const double frame = 16.0 / 9.0;
	// (held at a fixed place in the frame, just in front of the camera, its left edge at x; far from the world's middle, further in front: a renderer's floats are a few millimetres
	// apart 30 km out, and a card millimetres high collapsed)
	for (const explain::Hud &h : spec.explain.huds) {
		const double a = explain::detail::shown(t, h.from, h.to);
		if (a <= 0.0) continue;
		double aspect = 1.0;
		const std::string pic = explain::cardPicture(film, h.text, h.colour, aspect);
		const double half = std::tan(0.5 * v.fov * kPi / 180.0) * (frame < 1.0 ? 1.0 / frame : 1.0), d = std::max({0.2, 0.002 * length(v.target - v.eye) + 0.2, 2e-4 * length(v.eye)}), high = h.size * 2.0 * d * half;   // (pbrt's fov is the shorter side's)
		const Vec3 at = v.eye + v.forward() * d + v.right() * (h.x * d * half * frame + 0.5 * high * aspect) + v.up() * (h.y * d * half);
		if (!pic.empty()) out += "# a line of the display (film_explain.h): " + h.text + "\n" + explain::detail::card(pic, aspect, at, high, v, a);
	}
	for (const explain::Compare &c : spec.explain.compares) {
		std::vector<explain::LinedUp> bodies;
		for (const std::string &name : c.bodies) {
			explain::LinedUp b;
			b.name = name;
			if (const explain::Body *k = explain::knownBody(name)) {
				b.name = k->name, b.radius = k->radiusKm, b.colour = k->colour, b.shines = b.name == "Sun", b.size = explain::detail::grouped(2.0 * k->radiusKm) + " km";
			} else {
				for (const planets::Planet &p : spec.planets)
					if (p.name == name) {
						static const std::map<std::string, Vec3> tint = {{"rocky", {0.55, 0.5, 0.45}}, {"desert", {0.8, 0.6, 0.35}}, {"ice", {0.8, 0.88, 0.95}},
																		{"ocean", {0.25, 0.45, 0.85}}, {"gas", {0.85, 0.72, 0.55}}, {"lava", {0.6, 0.25, 0.1}}};
						b.radius = p.radius, b.colour = tint.count(p.kind) ? tint.at(p.kind) : Vec3{0.6, 0.6, 0.6}, b.size = explain::detail::grouped(2.0 * p.radius);
					}
			}
			bodies.push_back(b);
		}
		out += explain::compareText(c, t, bodies, v, film);
	}
	return out;
}

}  // namespace film_file
}  // namespace cartoon
