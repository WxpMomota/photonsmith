#pragma once
// film_parse_model.h -- the film file's model lines (the modeling rounds; the block's own lines: film_model.h modelLine). natureLine hands them here:
//
//     model NAME [params A 1 B 2 ...]               starts a model block, read up to its `end` (`$A` in its lines: a parameter)
//     model NAME from OTHER [A v] ... [variants N] [A lo hi] ...   a model made from another with other parameters - OTHER a model of the film, or
//                                                   library/OTHER.model (beside the film file, or under film_assets): `variants N` makes N of them, each with its
//                                                   parameters drawn between lo and hi, one picked for each placement
//     place NAME at X Y Z [scale S] [turn A] [as ID]   the model there (pbrt's x y z: its origin, as a tree's foot), sized and turned about its upright axis; `as ID`
//                                                   names this placement for `move`, `turn`, `grow`, `show` and `shatter` (and ID.PIECE a piece of it)
//     scatter NAME N in X0 Z0 X1 Z1 [y Y] [scale A B] [seed K]    N of it over a rectangle, each turned at random and sized between A and B
//     shatter ID at T [pieces N] [force F] [seed K]   the placement breaks at T (model_fracture.h)

#include "film_parse_space.h"   // (and film_spec.h)

namespace cartoon {
namespace film_file {

// Whether `line` places or scatters one of the film's models (its second word a model's name).
inline bool modelNamed(const Spec &s, const std::string &line) {
	const std::vector<std::string> t = detail::tokens(line);
	return t.size() > 1 && std::any_of(s.models.begin(), s.models.end(), [&](const model::Model &m) { return m.name == t[1]; });
}

// (modeling round 3) the lines that change a placed model's shape or move its pieces (model_anim.h) and put it in the rigid-body world: "" or what is wrong.
inline bool modelAnimVerb(const std::string &verb) {
	static const std::vector<std::string> k = {"flex", "morph", "build", "vanish", "wobble", "spin", "gear", "piston", "roll", "route", "aim", "attach", "rigidbody"};
	return std::find(k.begin(), k.end(), verb) != k.end();
}
inline std::string modelAnimLine(Spec &s, const std::string &verb, const std::string &line) {
	using detail::number;
	const std::vector<std::string> t = detail::tokens(line);
	std::vector<double> v;
	std::size_t i = 2;
	const auto numbers = [&](std::size_t n) {
		v.assign(n, 0.0);
		for (std::size_t k = 0; k < n; ++k)
			if (i + k >= t.size() || !number(t[i + k], v[k])) return false;
		i += n;
		return true;
	};
	if (t.size() < 2) return "`" + verb + " ID ...`";
	// the name: a placement `as ID`, or ID.PIECE a piece of its model
	const std::string name = t[1], id = name.substr(0, name.find('.'));
	const auto placed = std::find_if(s.modelPlaces.begin(), s.modelPlaces.end(), [&](const model::Placed &pl) { return pl.id == id; });
	if (placed == s.modelPlaces.end()) return "`" + verb + " " + name + "`: no model is placed `as " + id + "` (before this line)";
	const bool piece = name.find('.') != std::string::npos;
	const bool wantsPiece = verb == "flex" || verb == "wobble" || verb == "spin" || verb == "gear" || verb == "piston" || verb == "roll" || verb == "aim";
	if (wantsPiece != piece) return "`" + verb + "` names " + (wantsPiece ? "a piece: ID.PIECE" : "a placement: ID");
	model::Animated &a = s.animated;
	const auto timeKeys = [&](std::vector<std::pair<double, double>> &keys) {
		while (i + 1 < t.size()) {
			if (!numbers(2)) return false;
			keys.push_back({v[0], v[1]});
		}
		return i == t.size() && !keys.empty();
	};
	const auto fromTo = [&](double &t0, double &t1) {
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "from" && numbers(1)) t0 = v[0];
			else if (w == "to" && numbers(1)) t1 = v[0];
			else return false;
		}
		return t1 > t0;
	};
	if (verb == "flex") {
		model::Flex f;
		f.name = name;
		static const std::vector<std::string> kinds = {"bend", "twist", "taper", "stretch", "squash"};
		if (t.size() < 5 || std::find(kinds.begin(), kinds.end(), t[2]) == kinds.end()) return "`flex ID.PIECE bend|twist|taper|stretch|squash T V  T V ...`";
		f.kind = t[2], i = 3;
		if (!timeKeys(f.keys)) return "`flex ID.PIECE KIND T V  T V ...` (pairs of time and amount)";
		a.flexes.push_back(f);
		return std::string();
	}
	if (verb == "morph") {
		model::Morph mo;
		mo.id = name;
		if (t.size() < 4 || t[2] != "to") return "`morph ID to MODEL from T0 to T1`";
		mo.to = t[3], i = 4;
		if (std::none_of(s.models.begin(), s.models.end(), [&](const model::Model &m) { return m.name == mo.to; })) return "`morph ... to " + mo.to + "`: no model of that name";
		if (!fromTo(mo.t0, mo.t1)) return "`morph ID to MODEL from T0 to T1`";
		a.morphs.push_back(mo);
		return std::string();
	}
	if (verb == "build" || verb == "vanish") {
		model::Build b;
		model::Vanish van;
		b.id = van.id = name;
		double t0 = 0.0, t1 = 0.0;
		for (i = 2; i < t.size();) {
			const std::string w = t[i];
			if (verb == "build" && (w == "rise" || w == "pop" || w == "pieces")) b.style = w, ++i;
			else if ((w == "from" || w == "to") && i + 1 < t.size() && number(t[i + 1], (w == "from" ? t0 : t1))) i += 2;
			else return "`" + verb + " ID from T0 to T1`" + (verb == "build" ? std::string(" [rise|pop|pieces]") : std::string());
		}
		if (t1 <= t0) return "`" + verb + " ID from T0 to T1` (T1 after T0)";
		if (verb == "build") b.t0 = t0, b.t1 = t1, a.builds.push_back(b);
		else van.t0 = t0, van.t1 = t1, a.vanishes.push_back(van);
		return std::string();
	}
	if (verb == "wobble") {
		model::Wobble w;
		w.name = name;
		if (t.size() == 3) {
			if (!numbers(1) || v[0] <= 0) return "`wobble ID.PIECE [AMOUNT]`";
			w.amount = v[0];
		} else if (t.size() != 2) return "`wobble ID.PIECE [AMOUNT]`";
		a.wobbles.push_back(w);
		return std::string();
	}
	if (verb == "rigidbody") {
		Spec::ModelBody b;
		b.id = name;
		for (i = 2; i < t.size();) {
			const std::string w = t[i++];
			if (w == "mass" && numbers(1) && v[0] > 0) b.mass = v[0];
			else if (w == "bounce" && numbers(1) && v[0] >= 0 && v[0] <= 1) b.bounce = v[0];
			else if (w == "friction" && numbers(1) && v[0] >= 0) b.friction = v[0];
			else if (w == "fixed") b.fixed = true;
			else if (w == "push" && numbers(3)) {
				b.push = {v[0], v[1], v[2]};
				if (i < t.size() && t[i] == "at") {
					++i;
					if (!numbers(1)) return "`push VX VY VZ at T`";
					b.at = v[0];
				} else b.at = 0.0;
			} else if (w == "spin" && numbers(3)) b.spin = {v[0], v[1], v[2]};
			else return "`rigidbody ID [mass M] [bounce B] [friction F] [push VX VY VZ at T] [spin X Y Z] [fixed]`";
		}
		s.modelBodies.push_back(b);
		return std::string();
	}
	// mechanisms
	model::Mech m;
	m.kind = verb, m.name = name;
	const std::string usage = verb == "spin"	? "`spin ID.PIECE about X Y Z axis X Y Z rpm R [from T0] [to T1]`"
							  : verb == "gear"	? "`gear ID.PIECE driven-by ID.PIECE ratio K about X Y Z axis X Y Z`"
							  : verb == "piston" ? "`piston ID.PIECE axis X Y Z stroke S rpm R`"
							  : verb == "roll"	? "`roll ID.PIECE about X Y Z axis X Y Z radius R`"
							  : verb == "route" ? "`route ID through X Y Z X Y Z ... from T0 to T1`"
							  : verb == "aim"	? "`aim ID.PIECE at X Y Z | at NAME [about X Y Z]`"
												: "`attach ID to NAME [head|hand|chest] [offset X Y Z]`";
	for (i = 2; i < t.size();) {
		const std::string w = t[i++];
		if (w == "about" && numbers(3)) m.about = {v[0], v[1], v[2]};
		else if (w == "axis" && numbers(3)) m.axis = normalize(Vec3{v[0], v[1], v[2]} + Vec3{1e-12, 0, 0});
		else if (w == "rpm" && numbers(1)) m.rpm = v[0];
		else if (w == "ratio" && numbers(1)) m.ratio = v[0];
		else if (w == "stroke" && numbers(1)) m.stroke = v[0];
		else if (w == "radius" && numbers(1) && v[0] > 0) m.radius = v[0];
		else if (w == "from" && numbers(1)) m.t0 = v[0];
		else if (w == "to" && verb != "attach" && numbers(1)) m.t1 = v[0];
		else if (w == "driven-by" && i < t.size()) m.driver = t[i++];
		else if (w == "offset" && numbers(3)) m.offset = {v[0], v[1], v[2]};
		else if (w == "through") {
			double x = 0.0;
			while (i + 2 < t.size() && number(t[i], x) && numbers(3)) m.path.push_back({v[0], v[1], v[2]});
		} else if (w == "at" && verb == "aim") {
			double x = 0.0;
			if (i < t.size() && number(t[i], x)) {
				if (!numbers(3)) return usage;
				m.at = {v[0], v[1], v[2]};
			} else if (i < t.size()) m.target = t[i++];
			else return usage;
		} else if (w == "to" && verb == "attach" && i < t.size()) {
			m.target = t[i++];
			if (i < t.size() && (t[i] == "head" || t[i] == "hand" || t[i] == "chest")) m.where = t[i++];
		} else return usage;
	}
	if (verb == "route" && (m.path.size() < 2 || m.t1 >= 1e29 || m.t0 <= -1e29)) return usage;
	if (verb == "gear" && m.driver.empty()) return usage;
	if (verb == "attach" && m.target.empty()) return usage;
	a.mechs.push_back(m);
	return std::string();
}

inline std::string modelLines(Spec &s, const std::string &verb, const std::string &line) {
	using detail::number;
	const std::vector<std::string> t = detail::tokens(line);
	std::vector<double> v;
	std::size_t i = 2;
	const auto numbers = [&](std::size_t n) {
		v.assign(n, 0.0);
		for (std::size_t k = 0; k < n; ++k)
			if (i + k >= t.size() || !number(t[i + k], v[k])) return false;
		i += n;
		return true;
	};
	const auto known = [&](const std::string &name) { return std::any_of(s.models.begin(), s.models.end(), [&](const model::Model &m) { return m.name == name; }); };
	if (verb == "model") {
		if (t.size() < 2 || !detail::validKey(t[1])) return "`model NAME [params A 1 ...]` (its name lower-case letters, digits and '-'), then its lines, then `end`";
		if (std::find(dressing::kinds().begin(), dressing::kinds().end(), t[1]) != dressing::kinds().end()) return "`model " + t[1] + "`: that is a kind `place` already has (tree, rock, fern, bush, grass)";
		if (known(t[1])) return "there is already a model called " + t[1];
		model::Model m;
		m.name = t[1];
		if (t.size() > 2 && t[2] == "params") {
			for (i = 3; i + 1 < t.size(); i += 2) {
				double x = 0.0;
				if (!number(t[i + 1], x)) return "`model NAME params A 1 B 2 ...`: each parameter a name and a number";
				m.params[t[i]] = x;
			}
			if (i != t.size()) return "`model NAME params A 1 B 2 ...`: each parameter a name and a number";
		} else if (t.size() > 3 && t[2] == "from") {
			const char *usage = "`model NAME from OTHER [A v] ... [variants N] [A lo hi] ...`";
			std::map<std::string, double> fixed;
			std::map<std::string, std::pair<double, double>> ranges;
			int variants = 0;
			for (i = 4; i < t.size();) {
				const std::string w = t[i++];
				if (w == "variants") {
					if (!numbers(1) || v[0] < 2 || v[0] > 40) return std::string(usage) + " (2 to 40 variants)";
					variants = static_cast<int>(v[0]);
					continue;
				}
				double a = 0.0, b = 0.0;
				if (i >= t.size() || !number(t[i], a)) return usage;
				++i;
				if (variants > 0 && i < t.size() && number(t[i], b)) ranges[w] = {a, b}, ++i;
				else fixed[w] = a;
			}
			const auto it = std::find_if(s.models.begin(), s.models.end(), [&](const model::Model &x) { return x.name == t[3]; });
			if (it == s.models.end()) {   // (a model of the library: read when the film is loaded, DataFilm::load)
				m.library = t[3], m.params = fixed, m.closed = true;
				s.models.push_back(m);
				return std::string();
			}
			const model::Model from = *it;
			if (variants == 0) {
				if (const std::string e = model::remade(from, m, fixed); !e.empty()) return e;
				s.models.push_back(m);
				return std::string();
			}
			for (int k = 0; k < variants; ++k) {   // the variants, each a model of its own; NAME stands for them all
				model::Model one;
				one.name = m.name + "-" + std::to_string(k + 1);
				std::map<std::string, double> p = fixed;
				for (const auto &[name, r] : ranges) p[name] = lerp(r.first, r.second, hash01(k * 7919 + static_cast<int>(name.size()) * 31, 11 + static_cast<int>(name[0])));
				if (const std::string e = model::remade(from, one, p); !e.empty()) return e;
				m.variants.push_back(one.name);
				s.models.push_back(one);
			}
			m.closed = true;
			m.parts = s.models.back().parts;   // (its own pieces: the last variant's, for what measures it)
			s.models.push_back(m);
			return std::string();
		} else if (t.size() != 2) {
			return "`model NAME`, `model NAME params A 1 ...` or `model NAME from OTHER ...`";
		}
		s.models.push_back(m);
		return std::string();
	}
	if (verb == "shatter") {
		const char *usage = "`shatter ID at T [pieces N] [force F] [seed K]` (ID a placement's `as ID`; 2 to 60 pieces)";
		model::Shatter sh;
		if (t.size() < 4 || t[2] != "at") return usage;
		sh.id = t[1];
		i = 3;
		if (!numbers(1)) return usage;
		sh.t = v[0];
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "pieces" && numbers(1) && v[0] >= 2 && v[0] <= 60) sh.pieces = static_cast<int>(v[0]);
			else if (w == "force" && numbers(1) && v[0] >= 0 && v[0] <= 40) sh.force = v[0];
			else if (w == "seed" && numbers(1)) sh.seed = detail::seedOf(v[0]);
			else return usage;
		}
		if (std::none_of(s.modelPlaces.begin(), s.modelPlaces.end(), [&](const model::Placed &p) { return p.id == sh.id; }))
			return "`shatter " + sh.id + "`: no model is placed `as " + sh.id + "` (before this line)";
		s.shatters.push_back(sh);
		return std::string();
	}
	if (verb == "place") {
		const char *usage = "`place MODEL at X Y Z [scale S] [turn A] [as ID]`";
		model::Placed p;
		p.model = t[1];
		if (i >= t.size() || t[i++] != "at" || !numbers(3)) return usage;
		p.at = {v[0], v[1], v[2]};
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "scale" && numbers(1) && v[0] > 0.0) p.scale = v[0];
			else if (w == "turn" && numbers(1)) p.turn = v[0];
			else if (w == "as" && i < t.size() && detail::validKey(t[i])) p.id = t[i++];
			else return usage;
		}
		if (!p.id.empty() && std::any_of(s.modelPlaces.begin(), s.modelPlaces.end(), [&](const model::Placed &q) { return q.id == p.id; }))
			return "a model is already placed `as " + p.id + "`";
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
