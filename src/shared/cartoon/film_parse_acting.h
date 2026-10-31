#pragma once
// film_parse_acting.h -- film lines that act on `# @anim` blocks, groups and placed models: `parent`, `group` (what rides on what), `hop`, `blink` and `gaze` (film_parse.h has
// the words). `hop` and `blink` become tracks here; a `gaze` stays one, worked out at each moment (DataFilm::gazeMatrix).

#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "film_spec.h"

namespace cartoon {
namespace film_file {

inline bool actingVerb(const std::string &verb) { return verb == "parent" || verb == "group" || verb == "hop" || verb == "blink" || verb == "gaze" || verb == "surround"; }

// A shot's `subject NAME [at X Y Z] ...`: whom it is about, and for a block, group or placement where on it to look. "" or what is wrong with it.
inline std::string subjectLine(Spec &s, Shot &shot, const std::vector<std::string> &t) {
	for (std::size_t i = 1; i < t.size(); ++i) {
		if (t[i] != "at") {
			shot.about.push_back(t[i]);
			continue;
		}
		std::array<double, 3> at{};
		if (i == 1 || i + 3 >= t.size() || !detail::number(t[i + 1], at[0]) || !detail::number(t[i + 2], at[1]) || !detail::number(t[i + 3], at[2]))
			return "`subject NAME at X Y Z`: where on it to look";
		s.subjectPoints[t[i - 1]] = at, i += 3;
	}
	return std::string();
}

// One such line, split into words `w`, line `n` of the file: what it says goes into `s`. "" or what is wrong with it.
inline std::string actingLine(Spec &s, const std::string &verb, const std::vector<std::string> &w, int n) {
	using detail::number;
	if (verb == "surround") {   // film_surround.h
		const char *usage = "`surround FILE [radius R] [height H] [repeat N] [bright B] [ground R G B] [at X Z]`";
		if (w.size() < 2) return usage;
		surround::Surround &a = s.around;
		a.on = true, a.file = w[1];
		for (std::size_t i = 2; i < w.size();) {
			const auto one = [&](double &v) {
				const bool good = i + 1 < w.size() && number(w[i + 1], v) && v > 0.0;
				i += 2;
				return good;
			};
			bool ok = false;
			if (w[i] == "radius") ok = one(a.radius);
			else if (w[i] == "height") ok = one(a.height);
			else if (w[i] == "repeat") ok = one(a.repeat);
			else if (w[i] == "bright") ok = one(a.bright);
			else if (w[i] == "ground" && i + 3 < w.size()) ok = number(w[i + 1], a.ground[0]) && number(w[i + 2], a.ground[1]) && number(w[i + 3], a.ground[2]), i += 4;
			else if (w[i] == "at" && i + 2 < w.size()) ok = number(w[i + 1], a.x) && number(w[i + 2], a.z), i += 3;
			if (!ok) return usage;
		}
		return std::string();
	}
	if (verb == "parent" || verb == "group") {   // what rides on what (DataFilm::animMatrix)
		if (verb == "parent" && (w.size() != 4 || w[2] != "to")) return "`parent CHILD to OTHER`";
		if (verb == "group" && (w.size() < 4 || w[2] != "of")) return "`group NAME of A B C ...`";
		if (verb == "group") s.groups.push_back(w[1]);
		for (std::size_t i = verb == "group" ? 3 : 1; i < (verb == "group" ? w.size() : 2); ++i) {
			const std::string child = w[i], over = verb == "group" ? w[1] : w[3];
			for (std::string up = over;;) {   // (not onto itself, by any way round)
				if (up == child) return "`" + child + "` would ride on itself";
				const auto it = s.parents.find(up);
				if (it == s.parents.end()) break;
				up = it->second;
			}
			s.parents[child] = over;
		}
		return std::string();
	}
	if (verb == "hop") {   // the arc and, given its feet, a squash before, a stretch in the air and a squash as it lands
		const char *usage = "`hop NAME T SECONDS by X Y Z [height H] [feet X Y Z]`";
		double t0 = 0.0, dur = 0.0, by[3] = {0.0, 0.0, 0.0}, height = -1.0, feet[3] = {0.0, 0.0, 0.0};
		bool hasFeet = false;
		if (w.size() < 8 || w[4] != "by" || !number(w[2], t0) || !number(w[3], dur) || dur <= 0.0 || !number(w[5], by[0]) || !number(w[6], by[1]) || !number(w[7], by[2])) return usage;
		for (std::size_t i = 8; i < w.size();) {
			if (w[i] == "height" && i + 1 < w.size() && number(w[i + 1], height) && height >= 0.0) i += 2;
			else if (w[i] == "feet" && i + 3 < w.size() && number(w[i + 1], feet[0]) && number(w[i + 2], feet[1]) && number(w[i + 3], feet[2])) hasFeet = true, i += 4;
			else return usage;
		}
		if (height < 0.0) height = 0.25 * std::sqrt(by[0] * by[0] + by[1] * by[1] + by[2] * by[2]) + 0.3;
		Anim arc;
		arc.kind = Kind::Move, arc.name = w[1], arc.line = n, arc.ease = "linear";
		for (int k = 0; k <= 16; ++k) {
			const double u = k / 16.0;
			Key key;
			key.t = t0 + dur * u;
			for (int c = 0; c < 3; ++c) key.v[c] = by[c] * u;
			key.v[1] += 4.0 * height * u * (1.0 - u);
			arc.keys.push_back(key);
		}
		s.anims.push_back(arc);
		if (hasFeet) {
			Anim sq;
			sq.kind = Kind::Stretch, sq.name = w[1], sq.line = n, sq.ease = "smooth";
			for (int c = 0; c < 3; ++c) sq.about[c] = feet[c];
			const std::pair<double, double> shape[] = {{t0 - 0.22, 1.0}, {t0 - 0.05, 0.84}, {t0 + 0.07, 1.16}, {t0 + dur - 0.06, 1.08}, {t0 + dur + 0.05, 0.8}, {t0 + dur + 0.32, 1.0}};
			for (const auto &[at, v] : shape) {
				Key key;
				key.t = at, key.v[0] = v;
				sq.keys.push_back(key);
			}
			s.anims.push_back(sq);
		}
		return std::string();
	}
	if (verb == "blink") {   // shut along y for a moment: no second copy of the eyes
		const char *usage = "`blink NAME about X Y Z [shut S] T T T ...`";
		Anim a;
		a.kind = Kind::Resize, a.name = w.size() > 1 ? w[1] : std::string(), a.line = n, a.ease = "linear";
		double shut = 0.1;
		if (w.size() < 7 || w[2] != "about" || !number(w[3], a.about[0]) || !number(w[4], a.about[1]) || !number(w[5], a.about[2])) return usage;
		std::size_t i = 6;
		if (w[i] == "shut") {
			if (i + 2 >= w.size() || !number(w[i + 1], shut) || shut <= 0.0) return usage;
			i += 2;
		}
		for (double last = -1e30; i < w.size(); ++i) {
			double at = 0.0;
			if (!number(w[i], at) || at < last + 0.25) return "`blink`: its times go up, at least a quarter of a second apart";
			last = at;
			const std::pair<double, double> shape[] = {{at, 1.0}, {at + 0.06, shut}, {at + 0.1, shut}, {at + 0.22, 1.0}};
			for (const auto &[when, v] : shape) {
				Key key;
				key.t = when, key.v[0] = 1.0, key.v[1] = v, key.v[2] = 1.0;
				a.keys.push_back(key);
			}
		}
		if (a.keys.empty()) return usage;
		s.anims.push_back(a);
		return std::string();
	}
	if (verb == "gaze") {   // turned to keep looking at something, moving or not (DataFilm::gazeMatrix)
		const char *usage = "`gaze NAME [about X Y Z] [forward X Y Z] at X Y Z|WHO [offset X Y Z] [max DEG] [from T0 T1] [to T2 T3]`";
		if (w.size() < 4) return usage;
		Anim a;
		a.kind = Kind::Gaze, a.name = w[1], a.line = n, a.ease = "smooth";
		a.axis[0] = 0.0, a.axis[1] = 0.0, a.axis[2] = 1.0;
		double in0 = -1e30, in1 = -1e30, out0 = 1e30, out1 = 1e30;
		bool seen = false;
		for (std::size_t i = 2; i < w.size();) {
			const auto three = [&](double *dst) {
				if (i + 3 >= w.size() || !number(w[i + 1], dst[0]) || !number(w[i + 2], dst[1]) || !number(w[i + 3], dst[2])) return false;
				i += 4;
				return true;
			};
			double unused = 0.0;
			if (w[i] == "about") { if (!three(a.about)) return usage; }
			else if (w[i] == "forward") { if (!three(a.axis)) return usage; }
			else if (w[i] == "offset") { if (!three(a.point)) return usage; }
			else if (w[i] == "at") {
				seen = true;
				if (i + 1 >= w.size()) return usage;
				if (number(w[i + 1], unused)) { if (!three(a.point)) return usage; }
				else a.target = w[i + 1], i += 2;
			} else if (w[i] == "max" && i + 1 < w.size() && number(w[i + 1], a.most)) i += 2;
			else if (w[i] == "from" && i + 2 < w.size() && number(w[i + 1], in0) && number(w[i + 2], in1)) i += 3;
			else if (w[i] == "to" && i + 2 < w.size() && number(w[i + 1], out0) && number(w[i + 2], out1)) i += 3;
			else return usage;
		}
		if (!seen) return usage;
		const auto keyAt = [&](double at, double v) {
			Key key;
			key.t = at, key.v[0] = v;
			a.keys.push_back(key);
		};
		if (in1 > -1e29) keyAt(in0, 0.0), keyAt(in1, 1.0);
		if (out0 < 1e29) {
			if (a.keys.empty()) keyAt(out0, 1.0);
			else if (out0 > a.keys.back().t) keyAt(out0, 1.0);
			keyAt(out1, 0.0);
		}
		s.anims.push_back(a);
		return std::string();
	}
	return std::string();
}

}  // namespace film_file
}  // namespace cartoon
