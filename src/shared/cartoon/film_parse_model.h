#pragma once
// film_parse_model.h -- the film file's model lines (the modeling round; the block's own lines: film_model.h modelLine). natureLine hands them here:
//
//     model NAME                                   starts a model block, read up to its `end`
//     place NAME at X Y Z [scale S] [turn A]       the model there (pbrt's x y z: its origin, as a tree's foot), sized and turned about its upright axis
//     scatter NAME N in X0 Z0 X1 Z1 [y Y] [scale A B] [seed K]    N of it over a rectangle, each turned at random and sized between A and B

#include "film_parse_space.h"   // (and film_spec.h)

namespace cartoon {
namespace film_file {

// Whether `line` places or scatters one of the film's models (its second word a model's name).
inline bool modelNamed(const Spec &s, const std::string &line) {
	const std::vector<std::string> t = detail::tokens(line);
	return t.size() > 1 && std::any_of(s.models.begin(), s.models.end(), [&](const model::Model &m) { return m.name == t[1]; });
}

inline std::string modelLines(Spec &s, const std::string &verb, const std::string &line) {
	using detail::number;
	const std::vector<std::string> t = detail::tokens(line);
	if (verb == "model") {
		if (t.size() != 2 || !detail::validKey(t[1])) return "`model NAME` (its name lower-case letters, digits and '-'), then its lines, then `end`";
		if (std::find(dressing::kinds().begin(), dressing::kinds().end(), t[1]) != dressing::kinds().end()) return "`model " + t[1] + "`: that is a kind `place` already has (tree, rock, fern, bush, grass)";
		if (std::any_of(s.models.begin(), s.models.end(), [&](const model::Model &m) { return m.name == t[1]; })) return "there is already a model called " + t[1];
		model::Model m;
		m.name = t[1];
		s.models.push_back(m);
		return std::string();
	}
	std::vector<double> v;
	std::size_t i = 2;
	const auto numbers = [&](std::size_t n) {
		v.assign(n, 0.0);
		for (std::size_t k = 0; k < n; ++k)
			if (i + k >= t.size() || !number(t[i + k], v[k])) return false;
		i += n;
		return true;
	};
	if (verb == "place") {
		const char *usage = "`place MODEL at X Y Z [scale S] [turn A]`";
		model::Placed p;
		p.model = t[1];
		if (i >= t.size() || t[i++] != "at" || !numbers(3)) return usage;
		p.at = {v[0], v[1], v[2]};
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "scale" && numbers(1) && v[0] > 0.0) p.scale = v[0];
			else if (w == "turn" && numbers(1)) p.turn = v[0];
			else return usage;
		}
		s.modelPlaces.push_back(p);
		return std::string();
	}
	const char *usage = "`scatter MODEL N in X0 Z0 X1 Z1 [y Y] [scale A B] [seed K]` (1 to 5000 of it)";
	if (!numbers(1) || v[0] < 1 || v[0] > 5000) return usage;
	const int n = static_cast<int>(v[0]);
	if (i >= t.size() || t[i++] != "in" || !numbers(4)) return usage;
	const double x0 = std::min(v[0], v[2]), z0 = std::min(v[1], v[3]), x1 = std::max(v[0], v[2]), z1 = std::max(v[1], v[3]);
	double y = 0.0, s0 = 1.0, s1 = 1.0;
	int seed = 1;
	while (i < t.size()) {
		const std::string w = t[i++];
		if (w == "y" && numbers(1)) y = v[0];
		else if (w == "scale" && numbers(2) && v[0] > 0 && v[1] > 0) s0 = v[0], s1 = v[1];
		else if (w == "seed" && numbers(1)) seed = detail::seedOf(v[0]);
		else return usage;
	}
	for (int k = 0; k < n; ++k) {
		model::Placed p;
		p.model = t[1];
		p.at = {x0 + (x1 - x0) * hash01(seed * 7919 + k, 1), y, z0 + (z1 - z0) * hash01(seed * 7919 + k, 2)};
		p.turn = 360.0 * hash01(seed * 7919 + k, 3), p.scale = lerp(s0, s1, hash01(seed * 7919 + k, 4));
		s.modelPlaces.push_back(p);
	}
	return std::string();
}

}  // namespace film_file
}  // namespace cartoon
