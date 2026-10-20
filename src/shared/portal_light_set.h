#pragma once
// portal_light_set.h -- several pbrt portal infinite lights as one light.
//
// pbrt sums every light of a file; the pbrt-v4-scenes Watercolor has two "infinite" lights with a portal, one per window. The renderers' NEE and escaped-ray code is written for ONE
// portal light (eval_Le_rgb / sample_li / pdf_li), so this set has those three methods and the same signatures, and the camera holds a set where it held a single light.
//
//   Le          : the sum of every light's Le (an escaped ray sees each window's sky).
//   sample_li   : pick one light uniformly (reusing the sample's first random number, rescaled), sample it, and report the MIXTURE density at the direction it chose.
//   pdf_li      : the mixture density, (1/N) * the sum of every light's pdf_li_in_window (a light counts only for directions it shines from), which is what a BSDF-sampled escape
//                 needs for its MIS weight.
// With one light this is that light, exactly.

#include "portal_image_infinite_light.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

template <typename T>
class PortalLightSet {
public:
	using Light = PortalImageInfiniteLightData<T>;

	explicit PortalLightSet(std::vector<std::shared_ptr<Light>> lights) : lights_(std::move(lights)) {}

	std::size_t size() const { return lights_.size(); }
	const Light &light(std::size_t i) const { return *lights_[i]; }

	void eval_Le_rgb(T ox, T oy, T oz, T dx, T dy, T dz, T &out_r, T &out_g, T &out_b) const {
		out_r = out_g = out_b = T(0);
		for (const auto &l : lights_) {
			T r, g, b;
			l->eval_Le_rgb(ox, oy, oz, dx, dy, dz, r, g, b);
			out_r += r; out_g += g; out_b += b;
		}
	}

	T pdf_li(T px, T py, T pz, T dx, T dy, T dz) const {
		if (lights_.empty()) return T(0);
		T sum = T(0);
		for (const auto &l : lights_) sum += l->pdf_li_in_window(px, py, pz, dx, dy, dz);
		return sum / static_cast<T>(lights_.size());
	}

	bool sample_li(double ru, double rv, T px, T py, T pz, T &wx, T &wy, T &wz, T &pdf) const {
		const std::size_t n = lights_.size();
		if (n == 0) { wx = wy = wz = T(0); pdf = T(0); return false; }
		const double scaled = ru * static_cast<double>(n);
		const std::size_t index = std::min(n - 1, static_cast<std::size_t>(scaled));
		const double reused = std::min(0.9999999999, scaled - static_cast<double>(index));
		T chosenPdf;
		if (!lights_[index]->sample_li(reused, rv, px, py, pz, wx, wy, wz, chosenPdf)) { pdf = T(0); return false; }
		if (n == 1) {
			pdf = chosenPdf;
		} else {   // the mixture's density at the direction taken: the chosen light's own sampled density, plus what each other light gives that direction
			T sum = chosenPdf;
			for (std::size_t i = 0; i < n; ++i)
				if (i != index) sum += lights_[i]->pdf_li_in_window(px, py, pz, wx, wy, wz);
			pdf = sum / static_cast<T>(n);
		}
		return pdf > T(0);
	}

private:
	std::vector<std::shared_ptr<Light>> lights_;
};
