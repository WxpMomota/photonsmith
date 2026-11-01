#pragma once
// cosmic_event.h -- great things that happen in the sky, worked out and not painted, drawn on the finished picture (lens_look.h `event ...`, from a film's `event KIND ...`: cartoon/film_cosmic.h).
//
//   collision  two spiral galaxies falling through each other: thousands of stars round two heavy centres that swing past one another on an open orbit; the stars are followed step by step
//              (Toomre and Toomre's way, 1972: the stars feel the centres and not each other), so the tidal tails and the bridge between them come of themselves.
//   disc       a star being born with its planets: dust going round at each radius's own pace, so its spirals wind up, rings swept clean by growing planets, the star brightening in the middle.
//   merger     two neutron stars spiralling in faster and faster, the flash, and the shell of what was thrown out growing and reddening.
//   web        the cosmic web: galaxies along filaments between great knots, seen from far outside.
//   stars      a ball of suns, as the neighbourhood of ours: the camera can go in among them, the near ones sliding past the far ones.
//
// Why on the finished picture and not as shapes in the scene: light from thousands of stars adds up, and shapes do not: an opaque shape hides what is behind it, so a galaxy of them is a heap of
// white pebbles. Here each star is added to the light of the picture, in a fine layer and in a soft one, so it has no noise, cannot flicker from frame to frame, and costs a few milliseconds. The
// price: nothing in the scene can pass in front of it. It is drawn over everything, so a ship or a planet in the same patch of sky shows it through them.
//
// An event's `size` is the width of the whole thing, in the units of the scene, and its surface brightness is the same from any distance (as a real galaxy's is).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <mutex>
#include <vector>

namespace cosmic_event {

enum Kind { Collision = 0, Disc = 1, Merger = 2, Web = 3, Stars = 4 };

struct Params {
	int kind = Collision, count = 12000, seed = 1;
	double x = 0.0, y = 0.0, z = 0.0, size = 20000.0;   // (pbrt's)
	double u = 0.0;                                     // (how far through it is, 0 .. 1)
	double tilt = 25.0, brightness = 1.0;
	double eye[3] = {0.0, 0.0, 0.0}, target[3] = {0.0, 0.0, -1.0};   // (pbrt's)
	double fov = 40.0;
};

struct V {
	double x = 0.0, y = 0.0, z = 0.0;
};
inline V operator+(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V operator-(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V operator*(V a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline V unit(V a) { return a * (1.0 / std::max(1e-300, std::sqrt(dot(a, a)))); }

// One light: where (in units of the event's size, about its middle), its colour, how much of the event's light it has, and how wide it is. `blob`: a wide glow of its own, drawn as itself;
// otherwise it is one of the many, a little swarm of fine stars and a share of the soft layer under them.
struct Light {
	V p;
	V colour{1.0, 1.0, 1.0};
	double weight = 1.0, radius = 0.001;
	bool blob = false;
};

namespace detail {
constexpr double kPi = 3.14159265358979323846;
inline double hash(int a, int b) {
	unsigned v = static_cast<unsigned>(a) * 2654435761u + static_cast<unsigned>(b) * 40503u + 2166136261u;
	v ^= v >> 15;
	v *= 2246822519u;
	v ^= v >> 13;
	v *= 3266489917u;
	v ^= v >> 16;
	return (v & 0xFFFFFFu) / 16777216.0;
}
inline double smooth(double a, double b, double x) {
	const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
	return t * t * (3.0 - 2.0 * t);
}
inline V mix(V a, V b, double t) { return a + (b - a) * t; }

// The collision, kept from frame to frame: a film goes forward, so the next frame goes on from the last. Lengths in units where a galaxy's mass and G are 1.
constexpr double kMass = 1.0, kSoft = 0.1, kStep = 0.008, kRadius = 0.55, kApproach = 0.9, kStart = 3.0, kAcross = 7.0;   // (kApproach: how near they would pass; kAcross: the event's size)
constexpr int kSteps = 1800;
struct Collision_ {
	int count = 0, seed = 0, step = -1;
	std::vector<V> pos, vel, colour;
	std::vector<double> weight;
	V ca, cb, va, vb;
};
inline V pullOf(V p, V c) {
	const V d = c - p;
	const double r2 = dot(d, d) + kSoft * kSoft;
	return d * (kMass / (r2 * std::sqrt(r2)));
}
// Two discs on a parabola about each other, each with its spin the same way as the orbit (that is what throws out long tails) and tipped out of its plane. Their stars start in two spiral arms.
inline void startCollision(Collision_ &c, int count, int seed) {
	c.count = count, c.seed = seed, c.step = 0;
	const std::size_t n = static_cast<std::size_t>(count);
	c.pos.assign(n, V{}), c.vel.assign(n, V{}), c.colour.assign(n, V{}), c.weight.assign(n, 1.0);
	const double mu = 2.0 * kMass, f = -std::acos(2.0 * kApproach / kStart - 1.0), h = std::sqrt(2.0 * mu * kApproach);
	const V at{kStart * std::cos(f), 0.0, kStart * std::sin(f)}, way{-mu / h * std::sin(f), 0.0, mu / h * (1.0 + std::cos(f))};
	c.ca = at * -0.5, c.cb = at * 0.5, c.va = way * -0.5, c.vb = way * 0.5;
	for (int i = 0; i < count; ++i) {
		const int s = seed * 7919 + i;
		const bool first = i % 2 == 0;
		const double r = 0.04 + kRadius * std::pow(hash(s, 1), 0.7), a = 2.0 * kPi * hash(s, 2), tilt = first ? 0.35 : -1.0;
		const double v = std::sqrt(kMass * r * r / std::pow(r * r + kSoft * kSoft, 1.5));
		const V flat{r * std::cos(a), 0.01 * (hash(s, 3) - 0.5), r * std::sin(a)}, round{-std::sin(a) * v, 0.0, std::cos(a) * v};
		const auto tipped = [&](V p) { return V{p.x, p.y * std::cos(tilt) - p.z * std::sin(tilt), p.y * std::sin(tilt) + p.z * std::cos(tilt)}; };
		c.pos[static_cast<std::size_t>(i)] = (first ? c.ca : c.cb) + tipped(flat);
		c.vel[static_cast<std::size_t>(i)] = (first ? c.va : c.vb) + tipped(round);
		const double arm = std::pow(0.5 + 0.5 * std::cos(2.0 * (a - 3.0 * std::log(r / 0.04)) + (first ? 0.0 : 1.3)), 2.0);   // (the arms: where the young blue stars are)
		c.weight[static_cast<std::size_t>(i)] = (0.35 + 1.6 * arm) * (0.45 + 0.55 * (1.0 - r / (0.04 + kRadius)));
		c.colour[static_cast<std::size_t>(i)] = r < 0.15 ? mix(V{1.0, 0.8, 0.52}, V{1.0, 0.9, 0.76}, r / 0.15) : mix(V{1.0, 0.82, 0.62}, V{0.55, 0.72, 1.0}, std::min(1.0, 1.3 * arm + 0.2));
	}
}
inline void stepCollision(Collision_ &c) {
	const double apart = std::sqrt(dot(c.ca - c.cb, c.ca - c.cb)), drag = apart < 1.2 ? 0.999 : 1.0;   // (close in, the centres lose speed to the halos they plough through)
	const V aa = pullOf(c.ca, c.cb), ab = pullOf(c.cb, c.ca);
	c.va = (c.va + aa * kStep) * drag, c.vb = (c.vb + ab * kStep) * drag;
	c.ca = c.ca + c.va * kStep, c.cb = c.cb + c.vb * kStep;
	for (std::size_t i = 0; i < c.pos.size(); ++i) {
		c.vel[i] = c.vel[i] + (pullOf(c.pos[i], c.ca) + pullOf(c.pos[i], c.cb)) * kStep;
		c.pos[i] = c.pos[i] + c.vel[i] * kStep;
	}
	++c.step;
}
inline std::vector<Light> collisionLights(const Params &e) {
	static std::mutex lock;
	static Collision_ c;
	const std::lock_guard<std::mutex> hold(lock);
	const double at = std::clamp(e.u, 0.0, 1.0) * kSteps;
	const int want = std::min(static_cast<int>(at), kSteps);
	if (c.count != e.count || c.seed != e.seed || c.step > want || c.step < 0) startCollision(c, e.count, e.seed);
	while (c.step < want) stepCollision(c);
	const double w = (at - want) * kStep, unit = 1.0 / kAcross;
	std::vector<Light> out;
	out.reserve(c.pos.size() + 4);
	for (std::size_t i = 0; i < c.pos.size(); ++i) {
		const int s = c.seed * 7919 + static_cast<int>(i);
		Light l;
		l.p = (c.pos[i] + c.vel[i] * w) * unit, l.colour = c.colour[i], l.weight = c.weight[i], l.radius = 0.0022;
		if (hash(s, 7) < 0.012) l.colour = V{1.0, 0.36, 0.52}, l.weight *= 6.0, l.radius = 0.0012;   // (a knot of glowing gas: the collision sets off a burst of them)
		out.push_back(l);
	}
	for (const V &centre : {c.ca + c.va * w, c.cb + c.vb * w}) {   // (the old yellow bulges)
		Light b;
		b.p = centre * unit, b.colour = V{1.0, 0.8, 0.56}, b.weight = 0.02, b.radius = 0.005, b.blob = true;
		out.push_back(b);
		b.weight = 0.03, b.radius = 0.014;
		out.push_back(b);
	}
	return out;
}

inline std::vector<Light> discLights(const Params &e, double u) {
	std::vector<Light> out;
	out.reserve(static_cast<std::size_t>(e.count) + 8);
	const double clearing = smooth(0.1, 0.8, u), ringsAt[3] = {0.2, 0.33, 0.47};
	for (int i = 0; i < e.count; ++i) {
		const int s = e.seed * 6101 + i;
		const double r = 0.05 + 0.5 * std::pow(hash(s, 1), 0.62), speed = 16.0 * std::pow(0.2 / r, 1.5), start = 2.0 * kPi * hash(s, 2), a = start + speed * u;
		double there = 1.0;
		for (const double ring : ringsAt) there *= 1.0 - clearing * (1.0 - smooth(0.012, 0.03, std::abs(r - ring)));   // (a growing planet's lane, swept clear as it grows)
		const double wave = 0.72 + 0.5 * std::cos(2.0 * (start - 5.0 * std::log(r / 0.05))) + 0.4 * (hash(static_cast<int>(start * 3.0) + static_cast<int>(r * 40.0) * 57, e.seed) - 0.5);   // (spirals and clumps: going round at their radius's pace they wind up)
		Light l;
		l.p = V{r * std::cos(a), 0.05 * r * (hash(s, 4) - 0.5) * (1.0 - 0.7 * clearing), r * std::sin(a)};
		l.weight = std::max(0.05, wave) * there * std::pow(0.12 / r, 0.9), l.radius = 0.0028;
		l.colour = mix(mix(V{0.62, 0.2, 0.1}, V{1.0, 0.5, 0.24}, smooth(0.5, 0.22, r)), V{1.0, 0.86, 0.62}, smooth(0.24, 0.07, r));
		out.push_back(l);
	}
	const double burning = 0.35 + 0.65 * smooth(0.0, 0.9, u);
	Light star;
	star.colour = V{1.0, 0.9, 0.72}, star.weight = 0.5 * burning, star.radius = 0.01, star.blob = true;
	out.push_back(star);
	star.weight = 0.5 * burning, star.radius = 0.04;
	out.push_back(star);
	for (int k = 0; k < 3; ++k) {   // (the planets in their lanes, growing)
		const double a = 2.0 * kPi * 0.3 * k + 16.0 * std::pow(0.2 / ringsAt[k], 1.5) * u;
		Light p;
		p.p = V{ringsAt[k] * std::cos(a), 0.0, ringsAt[k] * std::sin(a)}, p.colour = V{1.0, 0.72, 0.5}, p.weight = 0.004 * clearing, p.radius = 0.0022 + 0.0024 * clearing, p.blob = true;
		if (clearing > 0.02) out.push_back(p);
	}
	return out;
}

inline std::vector<Light> mergerLights(const Params &e, double u) {
	std::vector<Light> out;
	const double meet = 0.55;
	if (u < meet) {   // (closer and closer, faster and faster: the orbit shrinks as (1 - t / meet)^(1/4), as gravitational waves carry it off)
		const double left = 1.0 - u / meet, r = 0.14 * std::pow(left, 0.25), a = 90.0 * (1.0 - std::pow(left, 0.625));
		for (const double sign : {1.0, -1.0}) {
			Light l;
			l.p = V{sign * r * std::cos(a), 0.0, sign * r * std::sin(a)}, l.colour = V{0.7, 0.84, 1.0}, l.weight = 0.012, l.radius = 0.0035, l.blob = true;
			out.push_back(l);
			l.weight = 0.03, l.radius = 0.016;
			out.push_back(l);
		}
		return out;
	}
	const double since = (u - meet) / (1.0 - meet), flash = std::exp(-since / 0.05);
	Light middle;
	middle.colour = V{1.0, 0.95, 0.88}, middle.weight = 0.03 + 3.0 * flash, middle.radius = 0.012 + 0.05 * flash, middle.blob = true;
	out.push_back(middle);
	for (int i = 0; i < e.count; ++i) {   // (the thrown-out shell, blue at first and red as it thins and cools)
		const int s = e.seed * 9109 + i;
		const double z = 2.0 * hash(s, 1) - 1.0, a = 2.0 * kPi * hash(s, 2), ring = std::sqrt(1.0 - z * z), v = 0.55 * (0.45 + 0.55 * hash(s, 3)) * (1.0 + 0.5 * std::abs(z));
		Light l;
		l.p = V{ring * std::cos(a), z, ring * std::sin(a)} * (v * std::pow(since, 0.7));
		l.colour = mix(V{0.6, 0.72, 1.0}, V{1.0, 0.25, 0.1}, smooth(0.1, 0.7, since)), l.weight = (0.6 + hash(s, 4)) * (1.0 - 0.75 * since), l.radius = 0.003 + 0.012 * since;
		out.push_back(l);
	}
	return out;
}

inline std::vector<Light> webLights(const Params &e) {   // (galaxies thick at the knots and along the strands between them, void between)
	std::vector<Light> out;
	out.reserve(static_cast<std::size_t>(e.count));
	const int knots = 44;
	std::vector<V> at;
	for (int k = 0; k < knots; ++k) at.push_back(V{hash(e.seed + k, 1) - 0.5, 0.6 * (hash(e.seed + k, 2) - 0.5), hash(e.seed + k, 3) - 0.5});
	for (int i = 0; i < e.count; ++i) {
		const int s = e.seed * 3331 + i, a = static_cast<int>(hash(s, 1) * knots) % knots;
		int b = (a + 1) % knots;   // (the near knots are what a strand joins)
		double best = 1e30;
		const int pick = static_cast<int>(hash(s, 2) * 3.0);
		for (int k = 0; k < knots; ++k) {
			const V d = at[static_cast<std::size_t>(k)] - at[static_cast<std::size_t>(a)];
			const double far_ = std::sqrt(dot(d, d)) * (0.7 + 0.8 * hash(a * 41 + k + pick, 3));
			if (k != a && far_ < best) best = far_, b = k;
		}
		const bool inKnot = hash(s, 4) < 0.35;
		const double w = inKnot ? 0.0 : hash(s, 5), spread = (inKnot ? 0.035 : 0.011) * std::pow(hash(s, 6), 0.5), th = 2.0 * kPi * hash(s, 7), z = 2.0 * hash(s, 8) - 1.0, q = std::sqrt(1.0 - z * z);
		Light l;
		l.p = mix(at[static_cast<std::size_t>(a)], at[static_cast<std::size_t>(b)], w) + V{q * std::cos(th), z, q * std::sin(th)} * spread;
		l.colour = mix(V{0.62, 0.74, 1.0}, V{1.0, 0.8, 0.6}, hash(s, 9)), l.weight = 0.6 + 0.9 * hash(s, 10), l.radius = 0.0035;
		out.push_back(l);
	}
	return out;
}

inline std::vector<Light> starLights(const Params &e) {
	std::vector<Light> out;
	out.reserve(static_cast<std::size_t>(e.count));
	for (int i = 0; i < e.count; ++i) {
		const int s = e.seed * 4441 + i;
		const double z = 2.0 * hash(s, 1) - 1.0, a = 2.0 * kPi * hash(s, 2), q = std::sqrt(1.0 - z * z), r = 0.5 * std::cbrt(hash(s, 3)), pick = hash(s, 4);
		Light l;
		l.p = V{q * std::cos(a), z, q * std::sin(a)} * r;
		l.colour = pick < 0.6 ? V{1.0, 0.62, 0.38} : pick < 0.85 ? V{1.0, 0.86, 0.68} : pick < 0.96 ? V{0.96, 0.96, 1.0} : V{0.62, 0.74, 1.0};   // (most suns small, red and dim; a few hot and blue)
		l.weight = (pick < 0.6 ? 0.2 : pick < 0.85 ? 0.7 : pick < 0.96 ? 2.0 : 6.0) * 0.00012 * (1.0 - smooth(0.2, 0.5, r)), l.radius = 0.0, l.blob = true;   // (points, however near: the ball thins away at its edge)
		out.push_back(l);
	}
	return out;
}

// One box blur along rows and along columns, of half-width r, of a picture of three channels.
inline void boxBlur(std::vector<float> &img, int w, int h, int r) {
	std::vector<float> line(static_cast<std::size_t>(std::max(w, h)));
	const double inv = 1.0 / (2 * r + 1);
	for (int c = 0; c < 3; ++c) {
		for (int pass = 0; pass < 2; ++pass) {
			const int lines = pass == 0 ? h : w, len = pass == 0 ? w : h;
			const std::size_t along = pass == 0 ? 3 : static_cast<std::size_t>(w) * 3, across = pass == 0 ? static_cast<std::size_t>(w) * 3 : 3;
			for (int l = 0; l < lines; ++l) {
				const std::size_t base = static_cast<std::size_t>(l) * across + c;
				for (int i = 0; i < len; ++i) line[static_cast<std::size_t>(i)] = img[base + static_cast<std::size_t>(i) * along];
				const auto at = [&](int i) { return line[static_cast<std::size_t>(std::clamp(i, 0, len - 1))]; };
				double sum = 0.0;
				for (int i = -r; i <= r; ++i) sum += at(i);
				for (int i = 0; i < len; ++i) {
					img[base + static_cast<std::size_t>(i) * along] = static_cast<float>(sum * inv);
					sum += at(i + r + 1) - at(i - r);
				}
			}
		}
	}
}
}  // namespace detail

// What is there, `e.u` of the way through: each light's place in units of the event's size.
inline std::vector<Light> lightsAt(const Params &e) {
	const double u = std::clamp(e.u, 0.0, 1.0);
	switch (e.kind) {
		case Collision: return detail::collisionLights(e);
		case Disc: return detail::discLights(e, u);
		case Merger: return detail::mergerLights(e, u);
		case Stars: return detail::starLights(e);
		default: return detail::webLights(e);
	}
}

// Adds the event's light to the linear picture (w x h x 3 floats, rows from the top).
inline void apply(float *rgb, int w, int h, const Params &e) {
	if (w <= 0 || h <= 0 || e.count <= 0) return;
	const V eye{e.eye[0], e.eye[1], e.eye[2]}, ahead = unit(V{e.target[0], e.target[1], e.target[2]} - eye), right = unit(cross(V{0.0, 1.0, 0.0}, ahead)), up = cross(ahead, right), middle{e.x, e.y, e.z};   // (a scene's left-handed: looking down -z, +x is on the left)
	const double focal = 0.5 * h / std::tan(0.5 * e.fov * detail::kPi / 180.0), c = std::cos(e.tilt * detail::kPi / 180.0), s = std::sin(e.tilt * detail::kPi / 180.0), onePixel = h / 1080.0;
	const int swarm = e.kind == Merger ? 3 : e.kind == Web ? 6 : 10, gw = std::max(4, w / 3), gh = std::max(4, h / 3);
	const double level = e.brightness * (e.kind == Collision ? 0.03 : e.kind == Disc ? 0.2 : e.kind == Merger ? 0.5 : 0.07) / (std::max(1, e.count) * swarm);
	std::vector<float> soft(static_cast<std::size_t>(gw) * gh * 3, 0.0f);
	const double acrossMiddle = focal * e.size / std::max(0.02 * e.size, dot(middle - eye, ahead));   // (pixels to one of the event's units, there)
	const auto toScreen = [&](V p, double &x, double &y, double &scale) {   // (p in the event's units: false when it is behind the camera)
		const V world = middle + V{-p.x, p.y * c - p.z * s, p.y * s + p.z * c} * e.size, to = world - eye;
		const double depth = dot(to, ahead);
		if (depth <= 1e-3 * e.size) return false;
		scale = focal / depth;
		x = 0.5 * w + dot(to, right) * scale, y = 0.5 * h - dot(to, up) * scale;
		scale *= e.size;   // (pixels to one of the event's units)
		return true;
	};
	const auto addAt = [&](int x, int y, V light) {
		if (x < 0 || y < 0 || x >= w || y >= h) return;
		float *o = rgb + (static_cast<std::size_t>(y) * w + x) * 3;
		o[0] += static_cast<float>(light.x), o[1] += static_cast<float>(light.y), o[2] += static_cast<float>(light.z);
	};
	const auto spot = [&](double x, double y, double sigma, V flux) {   // (a round glow of that total light, sigma pixels wide)
		const int reach = static_cast<int>(std::ceil(3.2 * sigma)), x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
		if (x0 + reach < 0 || y0 + reach < 0 || x0 - reach >= w || y0 - reach >= h) return;
		const V height = flux * (1.0 / (2.0 * detail::kPi * sigma * sigma));
		for (int j = std::max(-reach, -y0 - 0); j <= reach && y0 + j < h; ++j)
			for (int i = std::max(-reach, -x0 - 0); i <= reach && x0 + i < w; ++i) {
				const double dx = x0 + i + 0.5 - x, dy = y0 + j + 0.5 - y;
				addAt(x0 + i, y0 + j, height * std::exp(-0.5 * (dx * dx + dy * dy) / (sigma * sigma)));
			}
	};
	const std::vector<Light> lights = lightsAt(e);
	for (std::size_t n = 0; n < lights.size(); ++n) {
		const Light &l = lights[n];
		double x = 0.0, y = 0.0, scale = 0.0;
		if (!toScreen(l.p, x, y, scale)) continue;

		const double area = scale * scale;
		if (l.blob) {
			spot(x, y, std::max(0.8 * onePixel, l.radius * scale), l.colour * std::min(e.kind == Stars ? 60.0 * onePixel * onePixel : 1e30, e.brightness * l.weight * area * 0.03));   // (a star passing close is no brighter than a glare)
			continue;
		}
		for (int k = 0; k < swarm; ++k) {   // a few fine stars round each one that was followed, and the soft light of all the ones too faint to see
			const int sd = static_cast<int>(n) * 13 + k * 7919 + e.seed;
			const double a = 2.0 * detail::kPi * detail::hash(sd, 1), z = 2.0 * detail::hash(sd, 2) - 1.0, q = std::sqrt(1.0 - z * z), r = l.radius * std::sqrt(-2.0 * std::log(std::max(1e-6, detail::hash(sd, 3))));
			double px = 0.0, py = 0.0, sc = 0.0;
			if (!toScreen(l.p + V{q * std::cos(a), z, q * std::sin(a)} * r, px, py, sc)) continue;
			const V flux = l.colour * (level * l.weight * area);
			const double gx = px / w * gw - 0.5, gy = py / h * gh - 0.5, fx = gx - std::floor(gx), fy = gy - std::floor(gy);
			const int ix = static_cast<int>(std::floor(gx)), iy = static_cast<int>(std::floor(gy));
			const V share = flux * 0.75;
			for (int j = 0; j < 2; ++j)
				for (int i = 0; i < 2; ++i) {
					if (ix + i < 0 || iy + j < 0 || ix + i >= gw || iy + j >= gh) continue;
					float *o = &soft[(static_cast<std::size_t>(iy + j) * gw + ix + i) * 3];
					const double f = (i ? fx : 1.0 - fx) * (j ? fy : 1.0 - fy);
					o[0] += static_cast<float>(share.x * f), o[1] += static_cast<float>(share.y * f), o[2] += static_cast<float>(share.z * f);
				}
			const double pick = detail::hash(sd, 4), bright = 0.1 + 2.4 * pick * pick * pick * pick * pick;   // (most stars faint, a few bright)
			spot(px, py, std::max(0.62 * onePixel, 0.08 * l.radius * sc), flux * (0.25 * bright));
		}
	}
	// the soft layer: blurred so that it has no grain however near the camera is, and spread over the picture
	const int blur = std::clamp(static_cast<int>(std::lround(0.0009 * acrossMiddle)), 1, e.kind == Web ? 2 : 6);
	for (int pass = 0; pass < 3; ++pass) detail::boxBlur(soft, gw, gh, blur);
	const double perPixel = (static_cast<double>(gw) * gh) / (static_cast<double>(w) * h);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const double gx = (x + 0.5) / w * gw - 0.5, gy = (y + 0.5) / h * gh - 0.5;
			const int ix = std::clamp(static_cast<int>(std::floor(gx)), 0, gw - 1), iy = std::clamp(static_cast<int>(std::floor(gy)), 0, gh - 1), ix1 = std::min(gw - 1, ix + 1), iy1 = std::min(gh - 1, iy + 1);
			const double fx = std::clamp(gx - ix, 0.0, 1.0), fy = std::clamp(gy - iy, 0.0, 1.0);
			for (int k = 0; k < 3; ++k) {
				const auto at = [&](int i, int j) { return static_cast<double>(soft[(static_cast<std::size_t>(j) * gw + i) * 3 + k]); };
				const double v = (at(ix, iy) * (1.0 - fx) + at(ix1, iy) * fx) * (1.0 - fy) + (at(ix, iy1) * (1.0 - fx) + at(ix1, iy1) * fx) * fy;
				rgb[(static_cast<std::size_t>(y) * w + x) * 3 + k] += static_cast<float>(v * perPixel);
			}
		}
}

}  // namespace cosmic_event
