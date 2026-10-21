#pragma once
// path_probe.h -- "why is this pixel this colour?": a sink the CPU path tracer reports into while it renders ONE pixel (`ray_tracer --cpu --probe X Y`).
//
// The path tracer (camera_path.h's ray_color) tells the sink, at the places where it adds light to a path's radiance, how much it added, where in the path (the bounce number) and
// from what: an emitter seen directly or by a bounce ("emission"), the sky or background a ray escapes to, and the next-event estimates toward area lights, the sky and portal
// lights, and point/spot/distant lights. It also names each area light (by its material) and what each contributed, and what the camera ray hits first (material, place, normal).
// A report prints the mean radiance of the pixel split along those lines, so a pixel that is too bright or too dark against another renderer shows which term differs.
//
// Nothing here costs a normal render anything beyond one relaxed atomic load of a null pointer per call site: the sink is installed only for the probe run.

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace path_probe {

struct Rgb {
	double r = 0.0, g = 0.0, b = 0.0;
	Rgb &operator+=(const Rgb &o) {
		r += o.r;
		g += o.g;
		b += o.b;
		return *this;
	}
	double luminance() const { return 0.2126 * r + 0.7152 * g + 0.0722 * b; }
};

enum Component { kEmission = 0, kEscaped, kNeeArea, kNeeSky, kNeePunctual, kComponentCount };
constexpr int kMaxBounce = 24;
inline const char *componentName(int c) {
	static const char *const names[kComponentCount] = {"emitters hit by the path (MIS-weighted)", "sky / background the path escapes to", "next-event: area lights",
													   "next-event: sky and portal lights", "next-event: point, spot and distant lights"};
	return names[c];
}

struct LightStat {
	Rgb sum;
	double px = 0.0, py = 0.0, pz = 0.0;   // summed hit positions (divide by `count`)
	long count = 0;
	std::string material;
};
struct HitStat {
	long count = 0;
	double px = 0.0, py = 0.0, pz = 0.0, nx = 0.0, ny = 0.0, nz = 0.0, u = 0.0, v = 0.0;
	long frontFacing = 0;
};

struct Sink {
	std::mutex mutex;
	Rgb comp[kComponentCount][kMaxBounce];
	long samples = 0;
	long primaryMisses = 0;
	std::map<const void *, LightStat> lights;
	std::map<std::string, HitStat> firstHits;   // by the camera ray's first material
};

inline std::atomic<Sink *> &sinkPointer() {
	static std::atomic<Sink *> p{nullptr};
	return p;
}
inline bool active() { return sinkPointer().load(std::memory_order_relaxed) != nullptr; }
inline void install(Sink *s) { sinkPointer().store(s); }

// Where the current path is, and what its radiance was at the last mark (per thread: only one thread renders a one-pixel probe, but nothing here assumes it).
struct ThreadState {
	int bounce = 0;
	Rgb last;
};
inline ThreadState &threadState() {
	thread_local ThreadState s;
	return s;
}

inline void beginSample() {
	if (Sink *s = sinkPointer().load(std::memory_order_relaxed)) {
		std::lock_guard<std::mutex> lock(s->mutex);
		++s->samples;
	}
}
inline void setBounce(int b) { threadState().bounce = std::min(std::max(b, 0), kMaxBounce - 1); }

inline void add(Component c, double r, double g, double b) {
	if (Sink *s = sinkPointer().load(std::memory_order_relaxed)) {
		std::lock_guard<std::mutex> lock(s->mutex);
		s->comp[c][threadState().bounce] += Rgb{r, g, b};
	}
}
// Radiance deltas between marks: mark(L) after setting a baseline, cut(c, L) books everything since the last mark to component c.
inline void mark(double r, double g, double b) { threadState().last = Rgb{r, g, b}; }
inline void cut(Component c, double r, double g, double b) {
	ThreadState &t = threadState();
	add(c, r - t.last.r, g - t.last.g, b - t.last.b);
	t.last = Rgb{r, g, b};
}
inline void primaryMiss() {
	if (Sink *s = sinkPointer().load(std::memory_order_relaxed)) {
		std::lock_guard<std::mutex> lock(s->mutex);
		++s->primaryMisses;
	}
}
inline void light(const void *key, const std::string &material, double px, double py, double pz, double r, double g, double b) {
	if (Sink *s = sinkPointer().load(std::memory_order_relaxed)) {
		std::lock_guard<std::mutex> lock(s->mutex);
		LightStat &l = s->lights[key];
		if (l.material.empty()) l.material = material;
		l.sum += Rgb{r, g, b};
		l.px += px;
		l.py += py;
		l.pz += pz;
		++l.count;
	}
}
inline void firstHit(const std::string &material, double px, double py, double pz, double nx, double ny, double nz, double u, double v, bool frontFacing) {
	if (Sink *s = sinkPointer().load(std::memory_order_relaxed)) {
		std::lock_guard<std::mutex> lock(s->mutex);
		HitStat &h = s->firstHits[material];
		++h.count;
		h.px += px;
		h.py += py;
		h.pz += pz;
		h.nx += nx;
		h.ny += ny;
		h.nz += nz;
		h.u += u;
		h.v += v;
		if (frontFacing) ++h.frontFacing;
	}
}

// ---- the report ---------------------------------------------------------------------------------------------------------------------------------------------

inline std::string rgbText(const Rgb &c) {
	char b[96];
	std::snprintf(b, sizeof(b), "%8.4f %8.4f %8.4f", c.r, c.g, c.b);
	return b;
}

// "class lambertian" (the compiler's name for the type) -> "lambertian".
inline std::string friendlyName(std::string n) {
	for (const char *prefix : {"class ", "struct "})
		if (n.rfind(prefix, 0) == 0) n = n.substr(std::string(prefix).size());
	return n;
}

// Plain text: the pixel's mean radiance (linear, before exposure and tone mapping) and where it came from.
inline std::string report(const Sink &s, int x, int y, int width, int height) {
	std::string out;
	char b[256];
	const double n = static_cast<double>(std::max(1L, s.samples));
	std::snprintf(b, sizeof(b), "Pixel (%d, %d) of %d x %d, %ld samples\n", x, y, width, height, s.samples);
	out += b;
	Rgb total;
	Rgb byComponent[kComponentCount];
	for (int c = 0; c < kComponentCount; ++c)
		for (int k = 0; k < kMaxBounce; ++k) {
			byComponent[c] += s.comp[c][k];
			total += s.comp[c][k];
		}
	out += "Mean radiance (linear, before exposure and tone mapping):  " + rgbText(Rgb{total.r / n, total.g / n, total.b / n}) + "\n";
	if (s.primaryMisses > 0) {
		std::snprintf(b, sizeof(b), "Camera rays that left the scene without hitting anything: %.1f%%\n", 100.0 * s.primaryMisses / n);
		out += b;
	}
	out += "\nBy source (share of the pixel's luminance):\n";
	const double lum = std::max(1e-12, total.luminance());
	for (int c = 0; c < kComponentCount; ++c) {
		const Rgb m{byComponent[c].r / n, byComponent[c].g / n, byComponent[c].b / n};
		std::snprintf(b, sizeof(b), "  %-48s %s   %5.1f%%\n", componentName(c), rgbText(m).c_str(), 100.0 * byComponent[c].luminance() / lum);
		out += b;
	}
	out += "\nBy bounce (0 = lit directly at the first hit; the sky a camera ray sees is bounce 0 too):\n";
	int lastBounce = 0;
	for (int k = 0; k < kMaxBounce; ++k) {
		Rgb t;
		for (int c = 0; c < kComponentCount; ++c) t += s.comp[c][k];
		if (t.luminance() > 1e-9 * lum) lastBounce = k;
	}
	for (int k = 0; k <= lastBounce; ++k) {
		Rgb t;
		for (int c = 0; c < kComponentCount; ++c) t += s.comp[c][k];
		std::snprintf(b, sizeof(b), "  bounce %2d   %s   %5.1f%%\n", k, rgbText(Rgb{t.r / n, t.g / n, t.b / n}).c_str(), 100.0 * t.luminance() / lum);
		out += b;
	}
	if (!s.lights.empty()) {
		out += "\nArea lights that lit it (mean contribution; where the paths found each):\n";
		std::vector<std::pair<double, const LightStat *>> sorted;
		for (const auto &kv : s.lights) sorted.push_back({kv.second.sum.luminance(), &kv.second});
		std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b2) { return a.first > b2.first; });
		int index = 1;
		for (const auto &p : sorted) {
			const LightStat &l = *p.second;
			const double c = std::max(1L, l.count);
			std::snprintf(b, sizeof(b), "  light %d: %s   %5.1f%%   around (%.2f, %.2f, %.2f)   [%s]\n", index++, rgbText(Rgb{l.sum.r / n, l.sum.g / n, l.sum.b / n}).c_str(),
						  100.0 * l.sum.luminance() / lum, l.px / c, l.py / c, l.pz / c, friendlyName(l.material).c_str());
			out += b;
			if (index > 12) break;
		}
	}
	if (!s.firstHits.empty()) {
		out += "\nWhat the camera rays hit first:\n";
		std::vector<std::pair<long, const std::pair<const std::string, HitStat> *>> sorted;
		for (const auto &kv : s.firstHits) sorted.push_back({kv.second.count, &kv});
		std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b2) { return a.first > b2.first; });
		for (const auto &p : sorted) {
			const HitStat &h = p.second->second;
			const double c = static_cast<double>(std::max(1L, h.count));
			std::snprintf(b, sizeof(b), "  %5.1f%%  %s: at (%.3f, %.3f, %.3f), normal (%.2f, %.2f, %.2f), uv (%.3f, %.3f), %s\n", 100.0 * h.count / n, friendlyName(p.second->first).c_str(), h.px / c, h.py / c,
						  h.pz / c, h.nx / c, h.ny / c, h.nz / c, h.u / c, h.v / c, h.frontFacing * 2 >= h.count ? "front face" : "back face");
			out += b;
		}
	}
	return out;
}

}  // namespace path_probe
