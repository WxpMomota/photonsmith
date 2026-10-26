#pragma once
// film_parts.h -- a film made of parts played one after another (film_file.h `part`): each part a film of its own, with its set, its length, its cast, its shots and its sound,
// and the whole a Film like any other. Its frames are each part's at the part's own time; its shots are the parts' shots in order; its sound is each part's soundtrack laid
// at the part's start (the cut between two parts crossfaded over a quarter of a second); its captions, events, feet, subjects and speech are the parts', moved to their start.
// A part can come in with a dissolve, a fade or a wipe (`part NAME dissolve 1.5`): the other side of a dissolve or a wipe across the cut is the other part's scene.

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "cartoon_audio.h"
#include "cartoon_lib.h"

namespace cartoon {
namespace film_parts {

// The part playing at t (the last one at the very end) and its own time then.
inline std::size_t partAt(const std::vector<double> &starts, double t) {
	std::size_t k = 0;
	while (k + 1 < starts.size() && t >= starts[k + 1]) ++k;
	return k;
}

// `whole` (its key, name, description, fps, still time and meta) made of `parts` one after another, part k coming in with `ins[k]` (length 0: a cut; ins[0] is ignored).
inline Film joined(Film whole, const std::vector<Film> &parts, const std::vector<Transition> &ins) {
	struct Shared {
		std::vector<Film> parts;
		std::vector<double> starts;
		std::vector<Transition> ins;
	};
	auto s = std::make_shared<Shared>();
	s->parts = parts;
	s->ins = ins;
	s->ins.resize(parts.size());
	double at = 0.0;
	for (const Film &p : parts) {
		s->starts.push_back(at);
		at += p.duration;
	}
	whole.duration = at;
	const auto local = [s](double t, std::size_t &k) {
		k = partAt(s->starts, t);
		return std::min(std::max(0.0, t - s->starts[k]), s->parts[k].duration);
	};
	whole.sceneAt = [s, local](double t) {
		std::size_t k = 0;
		const double u = local(t, k);
		return s->parts[k].sceneAt(u);
	};
	whole.headerAt = [s, local](double t) {
		std::size_t k = 0;
		const double u = local(t, k);
		return s->parts[k].headerAt ? s->parts[k].headerAt(u) : std::string();
	};
	whole.shotStarts = [s]() {
		std::vector<double> out;
		for (std::size_t k = 0; k < s->parts.size(); ++k) {
			const std::vector<double> own = s->parts[k].shotStarts ? s->parts[k].shotStarts() : std::vector<double>{0.0};
			for (double t : own) out.push_back(s->starts[k] + t);
		}
		return out;
	};
	// the sound: each part's on a mix of its own, laid at its start; a part that comes in with a fade or a dissolve brings its sound in over that time
	whole.soundtrack = [s](audio::Mix &m) {
		for (std::size_t k = 0; k < s->parts.size(); ++k) {
			if (!s->parts[k].soundtrack) continue;
			audio::Mix part(s->parts[k].duration);
			s->parts[k].soundtrack(part);
			const double in = k == 0 ? 0.0 : std::max(0.25, s->ins[k].length * 0.5);
			const double out = k + 1 < s->parts.size() ? std::max(0.25, s->ins[k + 1].length * 0.5) : 0.0;
			m.addAt(part, s->starts[k], in, out);
		}
	};
	bool anyCaptions = false, anySubjects = false, anyEvents = false, anyFeet = false, anySpeech = false, anyTransitions = false;
	for (std::size_t k = 0; k < parts.size(); ++k) {
		anyCaptions = anyCaptions || static_cast<bool>(parts[k].captions);
		anySubjects = anySubjects || static_cast<bool>(parts[k].subjectsAt);
		anyEvents = anyEvents || static_cast<bool>(parts[k].events);
		anyFeet = anyFeet || static_cast<bool>(parts[k].feetAt);
		anySpeech = anySpeech || static_cast<bool>(parts[k].speechAt);
		anyTransitions = anyTransitions || static_cast<bool>(parts[k].transitions) || (k > 0 && s->ins[k].length > 0.0);
	}
	if (anyCaptions)
		whole.captions = [s]() {
			std::vector<Caption> out;
			for (std::size_t k = 0; k < s->parts.size(); ++k)
				if (s->parts[k].captions)
					for (Caption c : s->parts[k].captions()) {
						c.t0 += s->starts[k], c.t1 += s->starts[k];
						out.push_back(c);
					}
			return out;
		};
	if (anySubjects)
		whole.subjectsAt = [s, local](double t) {
			std::size_t k = 0;
			const double u = local(t, k);
			return s->parts[k].subjectsAt ? s->parts[k].subjectsAt(u) : std::vector<Vec3>();
		};
	if (anyEvents)
		whole.events = [s]() {
			std::vector<FilmEvent> out;
			for (std::size_t k = 0; k < s->parts.size(); ++k)
				if (s->parts[k].events)
					for (FilmEvent e : s->parts[k].events()) {
						e.t += s->starts[k];
						out.push_back(e);
					}
			return out;
		};
	if (anyFeet)
		whole.feetAt = [s, local](double t) {
			std::size_t k = 0;
			const double u = local(t, k);
			return s->parts[k].feetAt ? s->parts[k].feetAt(u) : std::vector<Foot>();
		};
	if (anySpeech)
		whole.speechAt = [s, local](double t) {
			std::size_t k = 0;
			const double u = local(t, k);
			return s->parts[k].speechAt ? s->parts[k].speechAt(u) : 0.0;
		};
	if (anyTransitions) {
		whole.transitions = [s]() {
			std::vector<Transition> out;
			for (std::size_t k = 0; k < s->parts.size(); ++k) {
				if (k > 0 && s->ins[k].length > 0.0) {
					Transition tr = s->ins[k];
					tr.at = s->starts[k];
					out.push_back(tr);
				}
				if (s->parts[k].transitions)
					for (Transition tr : s->parts[k].transitions()) {
						if (tr.at <= 1e-9 && k > 0 && s->ins[k].length > 0.0) continue;   // (the part's own way in gives way to the one the film gives it)
						tr.at += s->starts[k];
						out.push_back(tr);
					}
			}
			return out;
		};
		// the other side of a dissolve or a wipe: across the cut between two parts, the other part's scene (the one coming in at its start before the cut, the one going out at
		// its end after it); within a part, the part's own
		const auto acrossCut = [s](double t, std::size_t &other, double &u) {
			for (std::size_t k = 1; k < s->parts.size(); ++k) {
				const Transition &tr = s->ins[k];
				if (tr.length <= 0.0 || tr.kind == Transition::Fade || std::abs(t - s->starts[k]) > 0.5 * tr.length) continue;
				other = t < s->starts[k] ? k : k - 1;
				u = t < s->starts[k] ? 0.0 : s->parts[k - 1].duration;
				return true;
			}
			return false;
		};
		whole.sceneAcross = [s, local, acrossCut](double t) {
			std::size_t k = 0;
			double u = 0.0;
			if (acrossCut(t, k, u)) return s->parts[k].sceneAt(u);
			u = local(t, k);
			return s->parts[k].sceneAcross ? s->parts[k].sceneAcross(u) : s->parts[k].sceneAt(u);
		};
		whole.acrossShotTime = [s, local, acrossCut](double t) {
			std::size_t k = 0;
			double u = 0.0;
			if (acrossCut(t, k, u)) return s->starts[k] + std::min(u, std::max(0.0, s->parts[k].duration - 1e-6));
			u = local(t, k);
			return s->starts[k] + (s->parts[k].acrossShotTime ? s->parts[k].acrossShotTime(u) : u);
		};
	}
	return whole;
}

}  // namespace film_parts
}  // namespace cartoon
