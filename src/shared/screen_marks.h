#pragma once
// screen_marks.h -- cartoon marks drawn over a finished picture, as a 2D layer the way Blender's Grease Pencil draws over a render: the signs a cartoon puts by a character
// (an exclamation mark, a question mark, a drop of sweat, an anger mark, hearts, zzz, sparkles), speed lines behind something that moves, and the jagged star of an impact.
// Each is a flat shape of colour with a dark ink outline, popping in with an overshoot and shrinking away, and each kind moves in its own way (a heart floats up and beats,
// the z's rise one after another, sparkles twinkle, speed lines flicker). Drawn last, on the display picture (after the tone map, the look and the toon look), so nothing
// shades, blurs or grades them, and they are never lit or cast shadows.
//
// A picture's marks come from its scene file's `# @rt-mark KIND X Y SIZE AGE LIFE ANGLE R G B SEED` lines (before WorldBegin), which a film writes for each frame
// (src/shared/cartoon/film_marks.h, the film file's `mark` line): X, Y the mark's centre from the picture's centre in half the picture's shorter side (+x right, +y up),
// SIZE in the same units, AGE and LIFE in seconds (how long it has been up, how long it stays), ANGLE the way speed lines trail (radians, on the screen, anticlockwise from
// the right), R G B its colour (linear), SEED its own number (the jitter of an impact's points and of speed lines). The launcher draws them (launcher/main.cpp) on any
// picture but an EXR (which stays as rendered).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace screen_marks {

struct Mark {
	std::string kind;                   // one of kinds()
	double x = 0.0, y = 0.0;            // its centre (see above)
	double size = 0.1;
	double age = 0.0, life = 1.0;
	double angle = 0.0;                 // (speed lines)
	double colour[3] = {1.0, 1.0, 1.0}; // linear
	int seed = 0;
};

inline const std::vector<std::string> &kinds() {
	static const std::vector<std::string> k = {"exclaim", "question", "sweat", "anger", "heart", "zzz", "sparkle", "impact", "speed"};
	return k;
}

// Its colour when a film does not give one (linear).
inline void defaultColour(const std::string &kind, double out[3]) {
	const double *c = nullptr;
	static const double yellow[3] = {1.0, 0.72, 0.06}, white[3] = {0.95, 0.95, 0.95}, blue[3] = {0.25, 0.6, 1.0}, red[3] = {0.85, 0.03, 0.02}, pink[3] = {0.95, 0.08, 0.2},
						pale[3] = {1.0, 0.9, 0.35};
	if (kind == "exclaim" || kind == "impact") c = yellow;
	else if (kind == "sweat") c = blue;
	else if (kind == "anger") c = red;
	else if (kind == "heart") c = pink;
	else if (kind == "sparkle") c = pale;
	else c = white;
	for (int k = 0; k < 3; ++k) out[k] = c[k];
}

// The header line of a mark, and back.
inline std::string lineOf(const Mark &m) {
	char buf[256];
	std::snprintf(buf, sizeof buf, "# @rt-mark %s %.5f %.5f %.5f %.4f %.4f %.4f %.4f %.4f %.4f %d\n", m.kind.c_str(), m.x, m.y, m.size, m.age, m.life, m.angle, m.colour[0], m.colour[1],
				  m.colour[2], m.seed);
	return buf;
}
inline bool parse(const std::string &words, Mark &m) {
	std::istringstream in(words);
	if (!(in >> m.kind >> m.x >> m.y >> m.size >> m.age >> m.life >> m.angle >> m.colour[0] >> m.colour[1] >> m.colour[2] >> m.seed)) return false;
	return std::find(kinds().begin(), kinds().end(), m.kind) != kinds().end() && std::isfinite(m.x) && std::isfinite(m.y) && m.size > 0.0 && m.life > 0.0;
}
// A scene file's marks (its `# @rt-mark` lines before WorldBegin); a line that cannot be read is left out.
inline std::vector<Mark> fromSceneFile(const std::string &path) {
	std::vector<Mark> out;
	std::ifstream f(path);
	std::string line;
	const std::string tag = "# @rt-mark ";
	while (std::getline(f, line)) {
		if (line.rfind("WorldBegin", 0) == 0) break;
		Mark m;
		if (line.rfind(tag, 0) == 0 && parse(line.substr(tag.size()), m)) out.push_back(m);
	}
	return out;
}

// ---- the shapes: fills (polygons) and strokes (polylines of a width), in the mark's own units (its size 1, y up), with an ink outline ----

struct P2 {
	double x = 0.0, y = 0.0;
};
struct Stroke {
	std::vector<P2> pts;
	double width = 0.1;
};
struct Drawing {
	std::vector<std::vector<P2>> fills;
	std::vector<Stroke> strokes;
	double ink = 0.06;      // the outline's width (0: none)
	double opacity = 1.0;
};

namespace detail {

constexpr double kPi = 3.14159265358979323846;
inline double smooth(double a, double b, double x) {
	const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
	return t * t * (3.0 - 2.0 * t);
}
inline double hash01(int a, int b) {
	unsigned h = static_cast<unsigned>(a) * 0x9E3779B1u ^ (static_cast<unsigned>(b) + 0x7F4A7C15u) * 0x85EBCA77u;
	h ^= h >> 15, h *= 0x2C1B3C6Du, h ^= h >> 12, h *= 0x297A2D39u, h ^= h >> 15;
	return (h & 0xFFFFFF) / 16777216.0;
}
inline std::vector<P2> circle(double cx, double cy, double r, int n = 20) {
	std::vector<P2> p;
	for (int i = 0; i < n; ++i) p.push_back({cx + r * std::cos(2.0 * kPi * i / n), cy + r * std::sin(2.0 * kPi * i / n)});
	return p;
}
inline std::vector<P2> star(double cx, double cy, int points, double outer, double inner, double turn, int seed = -1) {
	std::vector<P2> p;
	for (int i = 0; i < 2 * points; ++i) {
		double r = i % 2 == 0 ? outer : inner;
		if (seed >= 0) r *= 0.8 + 0.4 * hash01(seed, i);
		const double a = turn + kPi * i / points;
		p.push_back({cx + r * std::sin(a), cy + r * std::cos(a)});
	}
	return p;
}
inline void move(std::vector<P2> &pts, double dx, double dy, double s = 1.0) {
	for (P2 &p : pts) p = {p.x * s + dx, p.y * s + dy};
}

}  // namespace detail

// The mark's shapes now (its age), already moved, turned and scaled by its own motion.
inline Drawing shapeOf(const Mark &m) {
	using namespace detail;
	Drawing d;
	const double a = m.age, life = m.life;
	const double u = std::clamp(a / 0.16, 0.0, 1.0) - 1.0, in = 1.0 + 2.70158 * u * u * u + 1.70158 * u * u;   // (pops in past its size and settles: an ease-out-back)
	const double out = 1.0 - smooth(life - 0.2, life, a);                                                                     // (shrinks away at the end)
	double scale = std::max(0.0, in) * out, turn = 0.0, dx = 0.0, dy = 0.0;
	if (m.kind == "exclaim") {
		turn = 0.25 * std::sin(a * 22.0) * std::exp(-a * 5.0);   // (a jolt that dies down)
		d.fills.push_back({{-0.13, 0.5}, {0.13, 0.5}, {0.065, -0.18}, {-0.065, -0.18}});
		d.fills.push_back(circle(0.0, -0.38, 0.1));
	} else if (m.kind == "question") {
		turn = 0.14 * std::sin(a * 3.0);   // (rocks, puzzled)
		Stroke hook;
		for (int i = 0; i <= 14; ++i) {
			const double th = (160.0 - 250.0 * i / 14.0) * kPi / 180.0;
			hook.pts.push_back({0.2 * std::cos(th), 0.22 + 0.2 * std::sin(th)});
		}
		hook.pts.push_back({0.0, -0.16});
		hook.width = 0.13;
		d.strokes.push_back(hook);
		d.fills.push_back(circle(0.0, -0.38, 0.09));
	} else if (m.kind == "sweat") {
		dy = -0.12 * a;   // (runs slowly down)
		std::vector<P2> drop = {{0.0, 0.45}};
		for (int i = 0; i <= 16; ++i) {
			const double th = (60.0 - 300.0 * i / 16.0) * kPi / 180.0;
			drop.push_back({0.24 * std::cos(th), -0.1 + 0.24 * std::sin(th)});
		}
		d.fills.push_back(drop);
	} else if (m.kind == "anger") {
		scale *= 1.0 + 0.12 * std::sin(a * 13.0);   // (throbs)
		for (int q = 0; q < 4; ++q) {
			const double sx = q % 2 == 0 ? 1.0 : -1.0, sy = q < 2 ? 1.0 : -1.0, phi = std::atan2(-sy, -sx);
			Stroke arc;
			for (int i = 0; i <= 8; ++i) {
				const double th = phi + (-55.0 + 110.0 * i / 8.0) * kPi / 180.0;
				arc.pts.push_back({0.42 * sx + 0.26 * std::cos(th), 0.42 * sy + 0.26 * std::sin(th)});
			}
			arc.width = 0.12;
			d.strokes.push_back(arc);
		}
	} else if (m.kind == "heart") {
		dy = 0.25 * a;   // (floats up, beating)
		scale *= 1.0 + 0.1 * std::max(0.0, std::sin(a * 9.0));
		std::vector<P2> h;
		for (int i = 0; i < 36; ++i) {
			const double t = 2.0 * kPi * i / 36.0, s = std::sin(t);
			h.push_back({16.0 * s * s * s / 34.0, (13.0 * std::cos(t) - 5.0 * std::cos(2.0 * t) - 2.0 * std::cos(3.0 * t) - std::cos(4.0 * t)) / 34.0 + 0.05});
		}
		d.fills.push_back(h);
	} else if (m.kind == "zzz") {
		// three z's up and to the side, each appearing in turn and fading as it rises (a cycle of 1.8 s)
		for (int k = 0; k < 3; ++k) {
			const double phase = std::fmod(a / 1.8 + k / 3.0, 1.0), s = 0.45 + 0.55 * phase;
			Stroke z;
			z.pts = {{-0.17, 0.17}, {0.17, 0.17}, {-0.17, -0.17}, {0.17, -0.17}};
			detail::move(z.pts, -0.25 + 0.55 * phase, -0.3 + 0.75 * phase, s);
			z.width = 0.075 * s;
			if (phase < 0.92) d.strokes.push_back(z);
		}
	} else if (m.kind == "sparkle") {
		const double where[3][3] = {{-0.35, 0.15, 0.0}, {0.3, 0.35, 2.1}, {0.15, -0.3, 4.2}};
		for (const auto &w : where) {
			const double s = 0.35 + 0.65 * std::fabs(std::sin(a * 4.5 + w[2]));
			d.fills.push_back(star(w[0], w[1], 4, 0.32 * s, 0.07 * s, 0.0));
		}
	} else if (m.kind == "impact") {
		scale = out * (a < 0.08 ? smooth(0.0, 0.08, a) * 1.25 : 1.25 - 0.25 * smooth(0.08, 0.3, a));   // (a flash: there at once, settling)
		d.fills.push_back(star(0.0, 0.0, 9, 0.55, 0.3, 0.17 * m.seed, m.seed));
	} else if (m.kind == "speed") {
		// lines streaming back from behind the mover, drawn afresh twelve times a second (as hand-drawn ones would be)
		d.ink = 0.0;
		d.opacity = 0.85 * out * std::min(1.0, a / 0.12);
		scale = 1.0;
		const int frame = static_cast<int>(std::floor(a * 12.0));
		const double c = std::cos(m.angle), s = std::sin(m.angle);
		for (int i = 0; i < 7; ++i) {
			const double across = -0.5 + (i + 0.5) / 7.0 + 0.06 * (hash01(m.seed + frame, 2 * i) - 0.5), from = 0.35 + 0.25 * hash01(m.seed + frame, 2 * i + 1),
						 len = 0.5 + 0.8 * hash01(m.seed * 7 + frame, i);
			Stroke l;
			l.pts = {{c * from - s * across, s * from + c * across}, {c * (from + len) - s * across, s * (from + len) + c * across}};
			l.width = 0.022;
			d.strokes.push_back(l);
		}
	}
	const double ct = std::cos(turn), st = std::sin(turn);
	const auto place = [&](std::vector<P2> &pts) {
		for (P2 &p : pts) p = {scale * (ct * p.x - st * p.y) + dx, scale * (st * p.x + ct * p.y) + dy};
	};
	for (std::vector<P2> &f : d.fills) place(f);
	for (Stroke &s : d.strokes) place(s.pts), s.width *= scale;
	d.ink *= scale;
	if (!(scale > 0.0)) d.fills.clear(), d.strokes.clear();
	return d;
}

// ---- drawing: antialiased (4 x 4 samples a pixel) onto an sRGB picture (w x h x 3, 0..1) ----

namespace detail {

inline bool inside(const std::vector<P2> &poly, double x, double y) {   // (even-odd)
	bool in = false;
	for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
		if ((poly[i].y > y) != (poly[j].y > y) && x < (poly[j].x - poly[i].x) * (y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x) in = !in;
	return in;
}
inline double segmentDistance2(P2 p, P2 a, P2 b) {
	const double vx = b.x - a.x, vy = b.y - a.y, wx = p.x - a.x, wy = p.y - a.y, l2 = vx * vx + vy * vy;
	const double t = l2 > 0.0 ? std::clamp((wx * vx + wy * vy) / l2, 0.0, 1.0) : 0.0;
	const double dx = wx - t * vx, dy = wy - t * vy;
	return dx * dx + dy * dy;
}
inline bool onStroke(const std::vector<P2> &pts, double halfWidth, double x, double y, bool closed) {
	const double h2 = halfWidth * halfWidth;
	for (std::size_t i = 0; i + 1 < pts.size(); ++i)
		if (segmentDistance2({x, y}, pts[i], pts[i + 1]) <= h2) return true;
	return closed && pts.size() > 2 && segmentDistance2({x, y}, pts.back(), pts.front()) <= h2;
}
inline double linearToSrgb(double c) {
	c = std::clamp(c, 0.0, 1.0);
	return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}
// Covers the pixels `test` says (in pixel coordinates, y down) with `colour` at `opacity`, within the box.
template <typename Test>
inline void cover(float *srgb, int w, int h, double x0, double y0, double x1, double y1, const double colour[3], double opacity, Test test) {
	const int ix0 = std::max(0, static_cast<int>(std::floor(x0))), iy0 = std::max(0, static_cast<int>(std::floor(y0)));
	const int ix1 = std::min(w - 1, static_cast<int>(std::ceil(x1))), iy1 = std::min(h - 1, static_cast<int>(std::ceil(y1)));
	for (int y = iy0; y <= iy1; ++y)
		for (int x = ix0; x <= ix1; ++x) {
			int hits = 0;
			for (int sy = 0; sy < 4; ++sy)
				for (int sx = 0; sx < 4; ++sx) hits += test(x + (sx + 0.5) / 4.0, y + (sy + 0.5) / 4.0) ? 1 : 0;
			if (hits == 0) continue;
			const double a = opacity * hits / 16.0;
			float *p = &srgb[(static_cast<std::size_t>(y) * w + x) * 3];
			for (int k = 0; k < 3; ++k) p[k] = static_cast<float>((1.0 - a) * p[k] + a * colour[k]);
		}
}

}  // namespace detail

inline void draw(float *srgb, int w, int h, const std::vector<Mark> &marks) {
	const double half = 0.5 * std::min(w, h);
	static const double ink[3] = {0.06, 0.05, 0.08};
	for (const Mark &m : marks) {
		Drawing d = shapeOf(m);
		if (d.fills.empty() && d.strokes.empty()) continue;
		// into pixels
		const double px = 0.5 * w + m.x * half, py = 0.5 * h - m.y * half, s = m.size * half;
		const auto toPixels = [&](std::vector<P2> &pts) {
			for (P2 &p : pts) p = {px + p.x * s, py - p.y * s};
		};
		for (std::vector<P2> &f : d.fills) toPixels(f);
		for (Stroke &st : d.strokes) toPixels(st.pts), st.width *= s;
		const double inkPx = d.ink > 0.0 ? std::max(1.5, d.ink * s) : 0.0;
		double colour[3];
		for (int k = 0; k < 3; ++k) colour[k] = detail::linearToSrgb(m.colour[k]);
		const auto box = [](const std::vector<P2> &pts, double pad, double b[4]) {
			b[0] = b[1] = 1e30, b[2] = b[3] = -1e30;
			for (const P2 &p : pts) b[0] = std::min(b[0], p.x - pad), b[1] = std::min(b[1], p.y - pad), b[2] = std::max(b[2], p.x + pad), b[3] = std::max(b[3], p.y + pad);
		};
		double b[4];
		// the ink under everything (a fill's outline, a stroke's wider copy), then the colour over it
		for (int layer = inkPx > 0.0 ? 0 : 1; layer < 2; ++layer) {
			const double grow = layer == 0 ? inkPx : 0.0;
			const double *c = layer == 0 ? ink : colour;
			for (const std::vector<P2> &f : d.fills) {
				box(f, grow + 1.0, b);
				detail::cover(srgb, w, h, b[0], b[1], b[2], b[3], c, d.opacity, [&](double x, double y) { return detail::inside(f, x, y) || (grow > 0.0 && detail::onStroke(f, grow, x, y, true)); });
			}
			for (const Stroke &st : d.strokes) {
				const double hw = 0.5 * st.width + grow;
				box(st.pts, hw + 1.0, b);
				detail::cover(srgb, w, h, b[0], b[1], b[2], b[3], c, d.opacity, [&](double x, double y) { return detail::onStroke(st.pts, hw, x, y, false); });
			}
		}
	}
}

}  // namespace screen_marks
