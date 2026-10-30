#pragma once
// film_explain.h -- the drawings of an explainer, laid over a space film (film_file.h `label`, `orbitline`, `scalebar`, `compare`; round 11):
//
//   label      a name on a card, held over a craft, a planet or moon, a character or a place by a thin line - the card always facing the camera and always the same size in
//              the frame however near or far what it names is; it pops up at `from` (its line drawing out from what it names) and away at `to`
//   orbitline  the path a craft (on its `orbit`) or a moon will take, drawn ahead of it as a thin line all the way round - drawing itself out at `from`; `dashed`
//   scalebar   a bar of a given length lying across the frame at a place, ticks at its ends, its length written under it
//   compare    planets side by side at their true sizes (the Sun's, the planets', some moons', or the film's own planets), standing on one line across the frame, each
//              named under it with its size - outlined, as a drawing is
//
// What these draw glows without lighting anything ("bool sampled" false) and is thin and flat, as a drawing laid over the picture is. Lines and cards keep their size in
// the frame: they are worked out for where the camera is in each frame (a share of the frame's height). Coordinates are pbrt's; times are the story's (seconds into the
// film), as the drawings are laid over the story rather than being part of what happens in it.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_orbits.h"
#include "film_screens.h"

namespace cartoon {
namespace explain {

struct Label {
	std::string text, on;   // what it says; what it names (a craft, planet, moon or character; "" for a place)
	Vec3 at;                // (a place) where
	double size = 0.04;     // its card's height, a share of the frame's
	Vec3 colour{1.0, 0.82, 0.3};
	double from = 0.0, to = 1e9;
};
struct OrbitLine {
	std::string of;   // a craft on an orbit, or a moon
	Vec3 colour{0.45, 0.8, 1.0};
	bool dashed = false;
	double from = 0.0, to = 1e9;
};
struct ScaleBar {
	double length = 1000.0;
	Vec3 at;
	std::string text;   // ("" says its length)
	Vec3 colour{1.0, 1.0, 1.0};
	double from = 0.0, to = 1e9;
};
struct Compare {
	std::vector<std::string> bodies;   // known bodies (knownBodies) or the film's own planets, left to right
	Vec3 at;                           // the middle of the line they stand on
	double height = 100.0;             // the biggest one's diameter
	Vec3 colour{1.0, 1.0, 1.0};
	double from = 0.0, to = 1e9;
};
struct Overlays {
	std::vector<Label> labels;
	std::vector<OrbitLine> orbits;
	std::vector<ScaleBar> bars;
	std::vector<Compare> compares;
	bool any() const { return !labels.empty() || !orbits.empty() || !bars.empty() || !compares.empty(); }
};

// A body of the solar system a `compare` knows: its name as written, its radius (km) and its colour.
struct Body {
	const char *name;
	double radiusKm;
	Vec3 colour;
};
inline const std::vector<Body> &knownBodies() {
	static const std::vector<Body> b = {
		{"Sun", 696340.0, {1.0, 0.85, 0.4}},       {"Mercury", 2439.7, {0.55, 0.53, 0.5}}, {"Venus", 6051.8, {0.9, 0.8, 0.55}},    {"Earth", 6371.0, {0.25, 0.45, 0.85}},
		{"Moon", 1737.4, {0.6, 0.6, 0.6}},         {"Mars", 3389.5, {0.8, 0.4, 0.22}},    {"Jupiter", 69911.0, {0.85, 0.72, 0.55}}, {"Saturn", 58232.0, {0.9, 0.8, 0.55}},
		{"Uranus", 25362.0, {0.6, 0.85, 0.9}},     {"Neptune", 24622.0, {0.3, 0.45, 0.9}}, {"Pluto", 1188.3, {0.8, 0.7, 0.6}},     {"Ceres", 469.7, {0.5, 0.5, 0.5}},
		{"Io", 1821.6, {0.9, 0.8, 0.35}},          {"Europa", 1560.8, {0.85, 0.8, 0.72}}, {"Ganymede", 2634.1, {0.6, 0.57, 0.52}}, {"Titan", 2574.7, {0.85, 0.65, 0.3}}};
	return b;
}
inline const Body *knownBody(const std::string &name) {
	for (const Body &b : knownBodies()) {
		const std::string n = b.name;
		if (n.size() == name.size() && std::equal(n.begin(), n.end(), name.begin(), [](char a, char c) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(c)); }))
			return &b;
	}
	return nullptr;
}

// Where the camera is in this frame, and how tall the frame is at a distance.
struct View {
	Vec3 eye, target;
	double fov = 40.0;
	double frameHeightAt(Vec3 p) const { return 2.0 * std::max(1e-6, length(p - eye)) * std::tan(0.5 * fov * kPi / 180.0); }
	Vec3 forward() const { return normalize(target - eye + Vec3{0.0, 0.0, 1e-12}); }
	Vec3 right() const {   // (across the frame to the right: pbrt's +x looking along +z)
		const Vec3 r = cross(Vec3{0.0, 1.0, 0.0}, forward());
		return length(r) > 1e-9 ? normalize(r) : Vec3{1.0, 0.0, 0.0};
	}
	Vec3 up() const { return cross(forward(), right()); }
};

// The card a label's words are on: its picture (painted once into the film's cache: dark, edged in its colour, the words white) and its width over its height. Defined
// in film_world.cpp (it sets the words in a font with stb_truetype). Returns "" when no font can be found.
std::string cardPicture(const std::string &film, const std::string &text, Vec3 colour, double &aspect);

namespace detail {
inline Vec3 story(Vec3 p) { return {-p.x, p.y, p.z}; }
// How far a drawing has popped up at story time t (0 .. 1): in over a third of a second from `from`, out over a quarter before `to`.
inline double shown(double t, double from, double to) { return std::clamp(std::min((t - from) / 0.35, (to - t) / 0.25), 0.0, 1.0); }
// A line from a to b (pbrt's), `width` across (world units), glowing.
inline std::string line(Vec3 a, Vec3 b, double width, Vec3 colour) {
	if (length(b - a) < 1e-9) return std::string();
	return attr(screens::detail::glowMaterial(colour), bar(story(a), story(b), 0.5 * width, 4));
}
// A card facing the camera, its middle at `c` (pbrt's), `h` high: the picture on it glowing (as a screen's does).
inline std::string card(const std::string &picture, double aspect, Vec3 c, double h, const View &v, double bright) {
	const Vec3 r = v.right() * (0.5 * h * aspect), u = v.up() * (0.5 * h);
	const Vec3 p[4] = {c - r - u, c + r - u, c + r + u, c - r + u};
	char buf[512];
	std::snprintf(buf, sizeof buf, "  Shape \"trianglemesh\" \"integer indices\" [ 0 1 2 0 2 3 ] \"point3 P\" [ %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g ] \"point2 uv\" [ 0 0 1 0 1 1 0 1 ]\n",
				  p[0].x, p[0].y, p[0].z, p[1].x, p[1].y, p[1].z, p[2].x, p[2].y, p[2].z, p[3].x, p[3].y, p[3].z);
	char scale[48];
	std::snprintf(scale, sizeof scale, "%.4f", bright);
	return attr("AreaLightSource \"diffuse\" \"string filename\" [ \"" + picture + "\" ] \"float scale\" [ " + scale + " ] \"bool twosided\" true \"bool sampled\" false\n  Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", buf);
}
// A number with its thousands apart ("12,742").
inline std::string grouped(double x) {
	char buf[64];
	std::snprintf(buf, sizeof buf, "%.0f", std::round(x));
	std::string s = buf, out;
	const std::size_t start = s[0] == '-' ? 1 : 0;
	for (std::size_t i = 0; i < s.size(); ++i) {
		out += s[i];
		const std::size_t left = s.size() - i - 1;
		if (i >= start && left > 0 && left % 3 == 0) out += ',';
	}
	return out;
}
}  // namespace detail

// A label over what it names (`target`: its middle, pbrt's; `radius`: how far its top is above that) at story time t.
inline std::string labelText(const Label &l, double t, Vec3 target, double radius, const View &v, const std::string &film) {
	using namespace detail;
	const double a = shown(t, l.from, l.to);
	if (a <= 0.0) return std::string();
	double aspect = 1.0;
	const std::string pic = cardPicture(film, l.text, l.colour, aspect);
	const double H = v.frameHeightAt(target), h = l.size * H * (0.6 + 0.4 * a);
	const Vec3 top = target + v.up() * (1.05 * radius);
	const Vec3 cardAt = top + v.up() * (2.2 * l.size * H + 0.5 * h);
	std::string out = "# a label (film_explain.h): " + l.text + "\n";
	out += line(top, top + (cardAt - v.up() * (0.5 * h) - top) * a, 0.0016 * H, l.colour * 1.5);
	if (!pic.empty()) out += card(pic, aspect, cardAt, h, v, a);
	return out;
}

// An orbit line through `path` (pbrt's points, in order round the loop), drawn out as far as it has popped up.
inline std::string orbitLineText(const OrbitLine &o, double t, const std::vector<Vec3> &path, const View &v) {
	using namespace detail;
	const double a = shown(t, o.from, o.to);
	if (a <= 0.0 || path.size() < 2) return std::string();
	std::string out = "# an orbit line (film_explain.h): " + o.of + "\n";
	const std::size_t n = static_cast<std::size_t>(std::ceil((path.size() - 1) * a));
	for (std::size_t k = 0; k < n; ++k) {
		if (o.dashed && k % 2) continue;
		out += line(path[k], path[k + 1], 0.0013 * v.frameHeightAt((path[k] + path[k + 1]) * 0.5), o.colour * 1.4);
	}
	return out;
}

// The path a craft on its orbit will take from nature's time nt (a period of its orbit ahead; a transfer it has yet to make is on it).
inline std::vector<Vec3> orbitPath(const cosmos::Orbit &o, double nt, int segments = 160) {
	std::vector<Vec3> out;
	for (int k = 0; k <= segments; ++k) out.push_back(cosmos::orbitPoint(o, nt + o.period * k / segments));
	return out;
}

inline std::string scaleBarText(const ScaleBar &b, double t, const View &v, const std::string &film) {
	using namespace detail;
	const double a = shown(t, b.from, b.to);
	if (a <= 0.0) return std::string();
	const double H = v.frameHeightAt(b.at), w = 0.0018 * H;
	const Vec3 r = v.right() * (0.5 * b.length * a), u = v.up() * (0.02 * H);
	std::string out = "# a scale bar (film_explain.h)\n";
	out += line(b.at - r, b.at + r, w, b.colour * 1.5) + line(b.at - r - u, b.at - r + u, w, b.colour * 1.5) + line(b.at + r - u, b.at + r + u, w, b.colour * 1.5);
	std::string text = b.text;
	if (text.empty()) text = b.length >= 1000.0 ? grouped(b.length / 1000.0) + " km" : grouped(b.length) + " m";
	double aspect = 1.0;
	if (const std::string pic = cardPicture(film, text, b.colour, aspect); !pic.empty()) out += card(pic, aspect, b.at - v.up() * (0.05 * H), 0.035 * H, v, a);
	return out;
}

// One body of a line-up: its name as shown, its radius (as the line-up compares them) and colour, and whether it shines.
struct LinedUp {
	std::string name, size;
	double radius = 1.0;
	Vec3 colour{0.6, 0.6, 0.6};
	bool shines = false;
};
inline std::string compareText(const Compare &c, double t, const std::vector<LinedUp> &bodies, const View &v, const std::string &film) {
	using namespace detail;
	const double a = shown(t, c.from, c.to);
	if (a <= 0.0 || bodies.empty()) return std::string();
	double biggest = 0.0;
	for (const LinedUp &b : bodies) biggest = std::max(biggest, b.radius);
	const double k = 0.5 * c.height / std::max(1e-9, biggest), gap = 0.12 * c.height;
	double across = 0.0;   // (the whole line-up's width)
	for (const LinedUp &b : bodies) across += 2.0 * b.radius * k;
	across += gap * (bodies.size() - 1);
	const Vec3 r = v.right(), u = v.up();
	const double H = v.frameHeightAt(c.at), ink = 0.0016 * H;
	std::string out = "# planets side by side (film_explain.h)\n";
	out += line(c.at - r * (0.5 * across + gap), c.at + r * (0.5 * across + gap), ink, c.colour * 1.2);
	double x = -0.5 * across;
	std::vector<double> rowEnds;   // (where each row of name cards ends so far: a card that would overlap the one before it goes down a row)
	for (const LinedUp &b : bodies) {
		const double R = b.radius * k * (0.3 + 0.7 * a);
		const Vec3 mid = c.at + r * (x + b.radius * k) + u * R;
		const Vec3 s = story(mid);
		// its own colour all round (a drawing's flat colour, so its night side is not lost in space) and the sun's light on it; the Sun shines
		const Vec3 flat = b.colour * (b.shines ? 3.0 : 0.25);
		out += attr("AreaLightSource \"diffuse\" \"rgb L\" [ " + fmt3(flat) + " ] \"bool sampled\" false\n  " + diffuseMaterial(b.shines ? Vec3{0.0, 0.0, 0.0} : b.colour), emitMesh(uvSphere(R, 48, 24), translate(s.x, s.y, s.z)));
		// its outline, as a drawing's: a ring round it facing the camera
		const Vec3 toEye = story(v.eye) - s;
		out += attr(screens::detail::glowMaterial(c.colour * 1.2), emitMesh(torus(R + 0.5 * ink, 0.5 * ink, 96, 4), alignY(s, toEye)));
		double aspect = 1.0;
		if (const std::string pic = cardPicture(film, b.name + "  " + b.size, c.colour, aspect); !pic.empty()) {
			const double h = 0.03 * H, half = 0.5 * h * aspect, centre = x + b.radius * k;
			std::size_t row = 0;
			while (row < rowEnds.size() && centre - half < rowEnds[row] + 0.3 * h) ++row;
			if (row == rowEnds.size()) rowEnds.push_back(-1e300);
			rowEnds[row] = centre + half;
			out += card(pic, aspect, c.at + r * centre - u * ((0.045 + 0.04 * row) * H), h, v, a);
			if (row > 0) out += line(c.at + r * centre - u * (0.004 * H), c.at + r * centre - u * ((0.03 + 0.04 * row) * H), 0.6 * ink, c.colour);   // (a line up to its body)
		}
		x += 2.0 * b.radius * k + gap;
	}
	return out;
}

inline std::string labelUsage() { return "`label \"TEXT\" on NAME|at X Y Z [size S] [colour R G B] [from T] [to T]` (NAME: a craft, planet, moon or character; size: a share of the frame's height, 0.01 to 0.2)"; }
inline std::string orbitLineUsage() { return "`orbitline NAME [colour R G B] [dashed] [from T] [to T]` (NAME: a craft on an `orbit`, or a moon)"; }
inline std::string scaleBarUsage() { return "`scalebar LENGTH at X Y Z [text \"WORDS\"] [colour R G B] [from T] [to T]`"; }
inline std::string compareUsage() { return "`compare BODY BODY ... at X Y Z [height H] [colour R G B] [from T] [to T]` (BODY: Sun, Mercury ... Neptune, Pluto, Moon, Io, Europa, Ganymede, Titan, Ceres, or a planet of the film)"; }

}  // namespace explain
}  // namespace cartoon
