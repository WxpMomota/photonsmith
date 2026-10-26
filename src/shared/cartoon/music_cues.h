#pragma once
// music_cues.h -- a film's music cues in a text file, read when its soundtrack is made: pbrt_scenes/film_assets/<film key>/cues.txt. A cue is placed by its shot, so a shot
// that is retimed takes its music with it, and a note added, moved or made quieter needs no rebuild. Every film gets it (the film registry in cartoons.h adds the cues to
// each film's soundtrack).
//
//     # The Last Nomai Signal: cues on top of the score.
//     [The canyon]                 # the times after a section are from that shot's start ([12]: the 12th shot; [*]: from the film's start)
//     2.2   piano D4 3.0           # at 2.2 s into the shot: a piano D4 for 3 s
//     4.0   strings D3 A3 D4  length 6  gain 0.04  pan -0.2  send 0.8
//     6.5   pluck A4 C5 E5  spread 0.25          # an arpeggio: a note every 0.25 s
//     @77.2 bell 86                # @: a film time, whatever the section
//
// A cue line is `<time> <instrument> [notes...] [length L] [gain G] [pan P] [send S] [spread D]`. Notes are whole MIDI numbers (60 is middle C) or names (C4, F#3, Bb2),
// and a number with a point after them is the length ("piano D4 3.0"). The
// instruments are the generic ones (genericInstruments: bell, mallet, pluck, bass, pad, strings, knock, swell, and the sound effects footstep-<surface>, impact, flash,
// touch) and the film's own (Film::instruments: the Nomai film's guitar, banjo, piano, resonance, drone, beep). What the file says wrong is printed once and the line left out.
//
// A recording: `3.0  sample voices/hello.wav  gain 0.8  by kai` plays a WAV file (beside the cues file; any rate, mono or stereo: audio::readWav). `by NAME` makes it that
// character's voice: voiceAt() is how loud it is at a moment (0..1, the loudest of the line 1), for the film's code to open a mouth by - or, behind a mask, to light up.

#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "cartoon_audio.h"
#include "film_speech.h"   // (a recording's mouth shapes)
#include "cartoon_lib.h"
#include "film_cache.h"
#include "sfx.h"

namespace cartoon {
namespace music_cues {

struct Cue {
	std::string shot;      // "" (or "*"): from the film's start; a number or a name: that shot's start
	double time = 0.0;     // seconds after it
	bool absolute = false; // @time: a film time, whatever the section
	std::string instrument;
	std::string file;      // `sample`: the WAV file (as written: beside the cues file)
	std::string by;        // `by NAME`: whose voice it is (voiceAt)
	std::vector<double> notes;
	double length = 2.0, gain = 0.1, pan = 0.0, send = 0.3, spread = 0.0;
	int line = 0;
};
struct Sheet {
	std::vector<Cue> cues;
	std::vector<std::string> problems;
	std::string folder;   // where the cues file is (a sample's file is looked for beside it)
};

// A note's MIDI number from a number or a name (C4 = 60, F#3, Bb2); false when it is neither.
inline bool noteOf(const std::string &word, double &midi) {
	char *end = nullptr;
	const double v = std::strtod(word.c_str(), &end);
	if (end && *end == '\0' && end != word.c_str()) {
		midi = v;
		return true;
	}
	static const int base[] = {9, 11, 0, 2, 4, 5, 7};   // A B C D E F G
	if (word.empty() || std::toupper(static_cast<unsigned char>(word[0])) < 'A' || std::toupper(static_cast<unsigned char>(word[0])) > 'G') return false;
	int n = base[std::toupper(static_cast<unsigned char>(word[0])) - 'A'];
	std::size_t i = 1;
	for (; i < word.size() && (word[i] == '#' || word[i] == 'b'); ++i) n += word[i] == '#' ? 1 : -1;
	if (i >= word.size()) return false;
	const int octave = std::atoi(word.c_str() + i);
	if (word.find_first_not_of("-0123456789", i) != std::string::npos) return false;
	midi = 12.0 * (octave + 1) + n;
	return true;
}

inline Sheet parse(const std::string &text) {
	Sheet s;
	std::istringstream in(text);
	std::string line, section;
	for (int n = 1; std::getline(in, line); ++n) {
		for (std::size_t i = 0; i < line.size(); ++i)   // (a comment starts a line or follows a space: the # of F#4 is a sharp)
			if (line[i] == '#' && (i == 0 || std::isspace(static_cast<unsigned char>(line[i - 1])))) {
				line.resize(i);
				break;
			}
		std::istringstream words(line);
		std::string first;
		if (!(words >> first)) continue;
		const std::string where = "line " + std::to_string(n) + ": ";
		if (first.front() == '[') {
			const std::size_t close = line.find(']');
			const std::size_t open = line.find('[');
			if (close == std::string::npos) {
				s.problems.push_back(where + "a section is [shot name], [shot number] or [*]");
				continue;
			}
			section = line.substr(open + 1, close - open - 1);
			while (!section.empty() && std::isspace(static_cast<unsigned char>(section.front()))) section.erase(section.begin());
			while (!section.empty() && std::isspace(static_cast<unsigned char>(section.back()))) section.pop_back();
			if (section == "*") section.clear();
			continue;
		}
		Cue c;
		c.line = n;
		c.shot = section;
		std::string timeWord = first;
		if (timeWord.front() == '@') {
			c.absolute = true;
			timeWord.erase(timeWord.begin());
		} else if (timeWord.front() == '+') {
			timeWord.erase(timeWord.begin());
		}
		char *end = nullptr;
		c.time = std::strtod(timeWord.c_str(), &end);
		if (!end || *end != '\0' || timeWord.empty()) {
			s.problems.push_back(where + "a cue starts with its time (2.5, +2.5 or @77.2), not '" + first + "'");
			continue;
		}
		if (!(words >> c.instrument)) {
			s.problems.push_back(where + "the cue names no instrument");
			continue;
		}
		if (c.instrument == "sample" && !(words >> c.file)) {
			s.problems.push_back(where + "`sample` needs a WAV file's name");
			continue;
		}
		bool ok = true;
		for (std::string w; ok && words >> w;) {
			double v = 0.0;
			if (w == "by") {
				if (!(words >> c.by)) {
					s.problems.push_back(where + "`by` needs a character's name");
					ok = false;
				}
			} else if (w == "length" || w == "gain" || w == "pan" || w == "send" || w == "spread") {
				if (!(words >> v)) {
					s.problems.push_back(where + "'" + w + "' needs a number");
					ok = false;
					break;
				}
				(w == "length" ? c.length : w == "gain" ? c.gain : w == "pan" ? c.pan : w == "send" ? c.send : c.spread) = v;
			} else if (!c.notes.empty() && w.find('.') != std::string::npos && std::strtod(w.c_str(), nullptr) > 0.0) {
				c.length = std::strtod(w.c_str(), nullptr);   // (a number with a point after the notes: the length, "piano D4 3.0"; whole numbers are notes)
			} else if (noteOf(w, v)) {
				c.notes.push_back(v);
			} else {
				s.problems.push_back(where + "'" + w + "' is not a note or one of length, gain, pan, send, spread");
				ok = false;
			}
		}
		if (ok) s.cues.push_back(c);
	}
	return s;
}

// The generic instruments: audio's own voices and the sound effects.
inline const std::map<std::string, CueInstrument> &genericInstruments() {
	using audio::hz;
	static const std::map<std::string, CueInstrument> m = [] {
		std::map<std::string, CueInstrument> i;
		const auto pitched = [](std::vector<float> (*make)(double, double)) {
			return CueInstrument([make](audio::Mix &mix, double t, const std::vector<double> &notes, double length, double gain, double pan, double send) {
				for (double n : notes) mix.put(t, make(hz(n), length), gain, pan, send);
			});
		};
		i["bell"] = pitched([](double f, double l) { return audio::bell(f, l); });
		i["mallet"] = pitched([](double f, double l) { return audio::mallet(f, l); });
		i["bass"] = pitched([](double f, double l) { return audio::bass(f, l); });
		i["pluck"] = pitched([](double f, double l) { return audio::pluck(f, l); });
		i["pad"] = pitched([](double f, double l) { return audio::pad(f, l); });
		i["strings"] = pitched([](double f, double l) { return audio::pad(f, l, std::min(2.0, l / 3.0), std::min(2.0, l / 3.0)); });
		i["knock"] = [](audio::Mix &mix, double t, const std::vector<double> &notes, double, double gain, double pan, double send) {
			mix.put(t, audio::knock(notes.empty() ? 160.0 : hz(notes[0])), gain, pan, send);
		};
		i["swell"] = [](audio::Mix &mix, double t, const std::vector<double> &, double length, double gain, double pan, double send) {   // a rush of air that swells and fades
			mix.put(t, audio::noiseBurst(length, length * 0.4, 3.0 / std::max(0.1, length), 1800.0, 500.0, static_cast<unsigned>(t * 1000.0)), gain, pan, send);
		};
		const std::pair<const char *, sfx::Surface> surfaces[] = {{"dust", sfx::Surface::Dust}, {"stone", sfx::Surface::Stone}, {"grass", sfx::Surface::Grass},
																	{"wood", sfx::Surface::Wood}, {"metal", sfx::Surface::Metal}, {"snow", sfx::Surface::Snow}};
		for (const auto &[name, surface] : surfaces) {
			const sfx::Surface s = surface;
			i[std::string("footstep-") + name] = [s](audio::Mix &mix, double t, const std::vector<double> &, double, double gain, double pan, double send) {
				mix.put(t, sfx::footstep(s, static_cast<int>(t * 100.0)), gain, pan, send);
			};
		}
		i["impact"] = [](audio::Mix &mix, double t, const std::vector<double> &notes, double, double gain, double pan, double send) {   // a note 0..1: its weight
			mix.put(t, sfx::impact(notes.empty() ? 0.5 : notes[0], static_cast<int>(t * 100.0)), gain, pan, send);
		};
		i["flash"] = [](audio::Mix &mix, double t, const std::vector<double> &, double, double gain, double pan, double send) { mix.put(t, sfx::flash(static_cast<int>(t * 100.0)), gain, pan, send); };
		i["touch"] = [](audio::Mix &mix, double t, const std::vector<double> &, double, double gain, double pan, double send) { mix.put(t, sfx::touch(static_cast<int>(t * 100.0)), gain, pan, send); };
		return i;
	}();
	return m;
}

// When a cue sounds: its shot's start (by number from 1 or by name) plus its time, or its time alone (@, or no section). False, with why, when the shot is not the film's.
inline bool timeOf(const Cue &c, const std::vector<double> &starts, const std::vector<std::string> &names, double &t, std::string &why) {
	t = c.time;
	if (c.absolute || c.shot.empty()) return true;
	if (c.shot.find_first_not_of("0123456789") == std::string::npos) {
		const int k = std::atoi(c.shot.c_str()) - 1;
		if (k < 0 || k >= static_cast<int>(starts.size())) {
			why = "there is no shot " + c.shot;
			return false;
		}
		t += starts[static_cast<std::size_t>(k)];
		return true;
	}
	for (std::size_t k = 0; k < names.size() && k < starts.size(); ++k)
		if (names[k] == c.shot) {
			t += starts[k];
			return true;
		}
	why = "there is no shot called \"" + c.shot + "\"";
	return false;
}

// A recording: its sound (audio::readWav, one channel at audio::kRate), its loudness each 1/100 s (RMS, the loudest 1), or why it could not be read.
struct Recording {
	std::filesystem::file_time_type time;
	std::vector<float> sound, loudness;
	std::vector<speech::Mouth> mouth;   // its mouth shapes, a hundredth of a second each (speech::mouthsOf): a recorded voice's lip sync
	std::string why;
};
// A recording, read once a program and kept (by path; read again when the file changes).
inline const Recording &recording(const std::string &path) {
	using Kept = Recording;
	static std::mutex lock;
	static std::map<std::string, Kept> kept;
	const std::lock_guard<std::mutex> hold(lock);
	std::error_code ec;
	const auto time = std::filesystem::last_write_time(path, ec);
	auto it = kept.find(path);
	if (it == kept.end() || it->second.time != time) {
		Kept k;
		k.time = time;
		if (audio::readWav(path, k.sound, &k.why)) {
			const std::size_t window = audio::kRate / 100;
			double loudest = 1e-9;
			for (std::size_t at = 0; at < k.sound.size(); at += window) {
				double sum = 0.0;
				const std::size_t end = std::min(k.sound.size(), at + window);
				for (std::size_t i = at; i < end; ++i) sum += static_cast<double>(k.sound[i]) * k.sound[i];
				const double rms = std::sqrt(sum / std::max<std::size_t>(1, end - at));
				k.loudness.push_back(static_cast<float>(rms));
				loudest = std::max(loudest, rms);
			}
			for (float &l : k.loudness) l = static_cast<float>(l / loudest);
			k.mouth = speech::mouthsOf(k.sound, k.loudness);
		}
		it = kept.insert_or_assign(path, std::move(k)).first;
	}
	return it->second;
}
inline std::string recordingPath(const Sheet &sheet, const std::string &file) {
	const std::filesystem::path p(file);
	return (p.is_absolute() ? p : std::filesystem::path(sheet.folder) / p).string();
}

// The cues on a mix: each at its time with its instrument (the film's own, else a generic one). What cannot be played is added to `problems`.
inline void play(audio::Mix &m, const Sheet &sheet, const std::map<std::string, CueInstrument> &own, const std::vector<double> &starts, const std::vector<std::string> &names,
				 std::vector<std::string> &problems) {
	for (const Cue &c : sheet.cues) {
		double t = 0.0;
		std::string why;
		if (!timeOf(c, starts, names, t, why)) {
			problems.push_back("line " + std::to_string(c.line) + ": " + why);
			continue;
		}
		if (c.instrument == "sample") {   // a recording: as it is, at its gain and pan
			const Recording &r = recording(recordingPath(sheet, c.file));
			if (r.sound.empty()) problems.push_back("line " + std::to_string(c.line) + ": the recording " + c.file + " " + (r.why.empty() ? "is empty" : r.why));
			else m.put(t, r.sound, c.gain, c.pan, c.send);
			continue;
		}
		auto it = own.find(c.instrument);
		const CueInstrument *play = it != own.end() ? &it->second : nullptr;
		if (!play) {
			const auto g = genericInstruments().find(c.instrument);
			if (g != genericInstruments().end()) play = &g->second;
		}
		if (!play) {
			problems.push_back("line " + std::to_string(c.line) + ": no instrument called \"" + c.instrument + "\"");
			continue;
		}
		if (c.spread > 0.0 && c.notes.size() > 1) {
			for (std::size_t k = 0; k < c.notes.size(); ++k) (*play)(m, t + c.spread * static_cast<double>(k), {c.notes[k]}, c.length, c.gain, c.pan, c.send);
		} else {
			(*play)(m, t, c.notes, c.length, c.gain, c.pan, c.send);
		}
	}
}

// A film's cues file ("" when it has none: film_cache::asset), looked for once a process.
inline std::string fileOf(const std::string &key) {
	static std::mutex lock;
	static std::map<std::string, std::string> found;
	const std::lock_guard<std::mutex> hold(lock);
	const auto it = found.find(key);
	if (it != found.end()) return it->second;
	return found[key] = film_cache::asset(key + "/cues.txt");
}
// The cues of a film's file, read now (a soundtrack is made once a video).
inline Sheet read(const std::string &path) {
	std::ifstream in(path, std::ios::binary);
	std::stringstream text;
	text << in.rdbuf();
	Sheet s = parse(text.str());
	s.folder = std::filesystem::path(path).parent_path().string();
	return s;
}

// How loud a character's voice is at film time t, 0 (silent) .. 1 (the loudest of its line): its `sample ... by NAME` cues in the film's cues file (read when it changes),
// the recording's loudness each 1/100 s, times the cue's gain over the loudest gain it has. For the film's code: a mouth opens by it.
// (`mouth`, when given, gets the mouth shape its recordings have then: speech::mouthsOf)
inline double voiceAt(const std::string &key, const std::string &speaker, double t, const std::vector<double> &starts, const std::vector<std::string> &names,
					  speech::Mouth *mouth = nullptr) {
	const std::string file = fileOf(key);
	if (file.empty()) return 0.0;
	static std::mutex lock;
	static std::map<std::string, std::pair<std::filesystem::file_time_type, Sheet>> sheets;
	Sheet sheet;
	{
		const std::lock_guard<std::mutex> hold(lock);
		std::error_code ec;
		const auto time = std::filesystem::last_write_time(file, ec);
		auto it = sheets.find(file);
		if (it == sheets.end() || it->second.first != time) it = sheets.insert_or_assign(file, std::make_pair(time, read(file))).first;
		sheet = it->second.second;
	}
	double loudest = 0.0;
	for (const Cue &c : sheet.cues)
		if (c.instrument == "sample" && c.by == speaker) loudest = std::max(loudest, c.gain);
	double v = 0.0;
	for (const Cue &c : sheet.cues) {
		if (c.instrument != "sample" || c.by != speaker || loudest <= 0.0) continue;
		double t0 = 0.0;
		std::string why;
		if (!timeOf(c, starts, names, t0, why) || t < t0) continue;
		const Recording &r = recording(recordingPath(sheet, c.file));
		const std::size_t k = static_cast<std::size_t>((t - t0) * 100.0);
		if (k < r.loudness.size()) v = std::max(v, r.loudness[k] * c.gain / loudest);
		if (mouth && k < r.mouth.size()) {
			const double share = std::min(1.0, c.gain / loudest);
			mouth->open = std::max(mouth->open, r.mouth[k].open * share);
			mouth->wide = std::max(mouth->wide, r.mouth[k].wide * share);
			mouth->round = std::max(mouth->round, r.mouth[k].round * share);
		}
	}
	return std::min(1.0, v);
}

}  // namespace music_cues
}  // namespace cartoon
