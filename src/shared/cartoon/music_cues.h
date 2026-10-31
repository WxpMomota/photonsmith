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
// instruments are the generic ones (genericInstruments: bell, mallet, pluck, bass, pad, strings, knock, swell, piano, guitar, bowed, drone, and the sound effects
// footstep-<surface>, impact, flash, touch) and the film's own (Film::instruments: the Nomai film's banjo, resonance, beep). What the file says wrong is printed once and the line left out.
//
// A hit on every cut: `oncuts mallet A2 1.0 gain 0.05` (the rest as a cue's) plays at the start of every shot but the first. And under the characters' lines the cues are
// ducked: a film that says when it speaks (Film::speechAt) has its music brought down by up to two thirds while a line is heard (cartoons.h, withFilmFiles).
//
// A recording: `3.0  sample voices/hello.wav  gain 0.8  by kai` plays a WAV file (beside the cues file; any rate, mono or stereo: audio::readWav). `by NAME` makes it that
// character's voice: voiceAt() is how loud it is at a moment (0..1, the loudest of the line 1), for the film's code to open a mouth by - or, behind a mask, to light up.
// Any other kind of sound file (MP3, OGG, FLAC ...) is turned into a WAV by ffmpeg, on the PATH as for a video's assembly.
//
// A recording can be worked on as it plays:
//     trim 2.0 9.5               only those seconds of it
//     fade 0.5 2.0               in over 0.5 s, out over 2 s
//     loop length 40             over and over for 40 s, each join crossfaded (`length` says how long)
//     curve 0 0  3 1  30 1  34 0.2      how loud at each time after the cue starts: pairs of time and level, straight lines between them
//     follow fire                as loud as the film's `light fire ...` track, so a crackle dies with the flame (a film file's; with `curve`, the two multiplied)

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "cartoon_audio.h"
#include "film_speech.h"   // (a recording's mouth shapes)
#include "film_voices.h"
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
	bool everyCut = false;   // `oncuts`: played at every cut (its time after the cut)
	double trim0 = 0.0, trim1 = -1.0, fadeIn = 0.0, fadeOut = 0.0;   // `trim A B`, `fade IN OUT` (a recording's; trim1 < 0: to its end)
	bool loop = false, hasLength = false;                            // `loop`: over and over for `length`
	std::vector<std::pair<double, double>> curve;                    // `curve T K ...`: its level K at T seconds after it starts
	std::string follow;                                              // `follow NAME`: its level the film's `light NAME` track
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

// A word for working on a recording and what follows it on the line. "" or what is wrong with it.
inline std::string recordingWord(Cue &c, const std::string &w, std::istringstream &words) {
	if (w == "loop") return c.loop = true, std::string();
	if (w == "follow") return words >> c.follow ? std::string() : "`follow` needs the name of a `light` track";
	if (w == "trim") return words >> c.trim0 >> c.trim1 && c.trim0 >= 0.0 && c.trim1 > c.trim0 ? std::string() : "`trim FROM TO`: seconds of the recording, going up";
	if (w == "fade") return words >> c.fadeIn >> c.fadeOut && c.fadeIn >= 0.0 && c.fadeOut >= 0.0 ? std::string() : "`fade IN OUT`: seconds";
	for (;;) {   // (`curve`: pairs up to the next word)
		const std::streampos before = words.tellg();
		std::string first, second;
		char *end = nullptr, *end2 = nullptr;
		if (!(words >> first)) break;
		const double at = std::strtod(first.c_str(), &end);
		if (*end != '\0') {
			words.clear();
			words.seekg(before);
			break;
		}
		if (!(words >> second)) return "`curve` takes pairs of time and level";
		const double level = std::strtod(second.c_str(), &end2);
		if (*end2 != '\0' || level < 0.0 || (!c.curve.empty() && at <= c.curve.back().first)) return "`curve T K T K ...`: times going up, levels of 0 or more";
		c.curve.push_back({at, level});
	}
	if (c.curve.empty()) words.clear();
	return c.curve.empty() ? "`curve` takes pairs of time and level" : std::string();
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
		if (first == "oncuts") {   // (a hit on every cut: the rest of the line as a cue's, its time 0 after each cut)
			c.everyCut = true;
			c.shot.clear();
			first = "0";
		}
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
				c.hasLength = c.hasLength || w == "length";
			} else if (w == "trim" || w == "fade" || w == "loop" || w == "curve" || w == "follow") {
				const std::string e = c.instrument == "sample" ? recordingWord(c, w, words) : "'" + w + "' is for a `sample`";
				if (!e.empty()) s.problems.push_back(where + e);
				ok = e.empty();
			} else if (!c.notes.empty() && w.find('.') != std::string::npos && std::strtod(w.c_str(), nullptr) > 0.0) {
				c.length = std::strtod(w.c_str(), nullptr);   // (a number with a point after the notes: the length, "piano D4 3.0"; whole numbers are notes)
			} else if (noteOf(w, v)) {
				c.notes.push_back(v);
			} else {
				s.problems.push_back(where + "'" + w + "' is not a note or one of length, gain, pan, send, spread");
				ok = false;
			}
		}
		if (ok && c.loop && !c.hasLength) s.problems.push_back(where + "`loop` needs a `length`: how long to go on for");
		if (ok && (!c.loop || c.hasLength)) s.cues.push_back(c);
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
		const auto voiced = [](std::vector<float> (*make)(double, double)) {   // (film_voices.h's, which take the note itself)
			return CueInstrument([make](audio::Mix &mix, double t, const std::vector<double> &notes, double length, double gain, double pan, double send) {
				for (double n : notes) mix.put(t, make(n, length), gain, pan, send);
			});
		};
		i["piano"] = voiced([](double n, double l) { return voices::piano(n, l); });
		i["guitar"] = voiced([](double n, double l) { return voices::guitar(n, l); });
		i["bowed"] = voiced([](double n, double l) { return voices::bowed(n, l); });
		i["drone"] = voiced([](double n, double l) { return voices::drone(n, l); });
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

// A sound file of another kind: ffmpeg makes a WAV of it, for audio::readWav. False, with why, when it cannot.
inline bool readOther(const std::string &path, std::vector<float> &out, std::string *why) {
	if (path.find_first_of("\"$`") != std::string::npos) return *why = "has a name that cannot be passed to ffmpeg", false;
	std::error_code ec;
	if (!std::filesystem::exists(path, ec)) return *why = "is not there", false;
	const std::filesystem::path wav = std::filesystem::temp_directory_path(ec) / ("rt_cue_" + std::to_string(std::hash<std::string>{}(path)) + ".wav");
	const std::string command = "ffmpeg -v error -y -i \"" + path + "\" -ar " + std::to_string(audio::kRate) + " \"" + wav.string() + "\"";
	const bool made = std::system(command.c_str()) == 0 && audio::readWav(wav.string(), out, why);
	std::filesystem::remove(wav, ec);
	if (!made && why) *why = "could not be read as a WAV, nor by ffmpeg (is it on the PATH?)";
	return made;
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
		std::string kind = std::filesystem::path(path).extension().string();
		for (char &ch : kind) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		if (kind == ".wav" ? audio::readWav(path, k.sound, &k.why) : readOther(path, k.sound, &k.why)) {
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

// The level of the music under speech (`speech`: how loud the lines are at a time, 0..1), one value each 1/100 s for `seconds`: brought down by up to two thirds as a line is
// heard, in 0.06 s, and back up over 0.5 s after it.
inline std::vector<float> duckingOf(const std::function<double(double)> &speech, double seconds) {
	std::vector<float> gain;
	double level = 0.0;
	for (double t = 0.0; t <= seconds; t += 0.01) {
		const double target = std::min(1.0, 1.6 * speech(t));
		level += (target - level) * (target > level ? 1.0 - std::exp(-0.01 / 0.06) : 1.0 - std::exp(-0.01 / 0.5));
		gain.push_back(static_cast<float>(1.0 - 0.65 * level));
	}
	return gain;
}

// A `follow`ed track: how strong `name` is at a film time, below 0 when it has no such track.
using Track = std::function<double(const std::string &name, double t)>;

// What a `sample` cue at film time t plays: the recording trimmed, looped, faded and made louder and quieter over time.
inline std::vector<float> worked(const std::vector<float> &sound, const Cue &c, double t, const Track &track) {
	const auto samples = [](double seconds) { return static_cast<std::size_t>(std::max(0.0, seconds) * audio::kRate); };
	const std::size_t from = std::min(sound.size(), samples(c.trim0)), to = c.trim1 < 0.0 ? sound.size() : std::max(from, std::min(sound.size(), samples(c.trim1)));
	std::vector<float> out(sound.begin() + static_cast<std::ptrdiff_t>(from), sound.begin() + static_cast<std::ptrdiff_t>(to));
	if (c.loop && !out.empty()) {
		const std::vector<float> once = out;
		const std::size_t total = samples(c.length), join = std::min(samples(0.05), once.size() / 2);
		while (out.size() < total) {
			for (std::size_t i = 0; i < join; ++i) {   // (the end of one going over into the start of the next)
				const float k = (static_cast<float>(i) + 0.5f) / static_cast<float>(join);
				float &tail = out[out.size() - join + i];
				tail = tail * (1.0f - k) + once[i] * k;
			}
			out.insert(out.end(), once.begin() + static_cast<std::ptrdiff_t>(join), once.end());
		}
		out.resize(total);
	}
	// (a recording cut in the middle of a sound would click: 5 ms even when no fade is asked for)
	const std::size_t in = std::min(out.size(), samples(c.trim0 > 0.0 ? std::max(c.fadeIn, 0.005) : c.fadeIn));
	const std::size_t away = std::min(out.size(), samples(c.trim1 >= 0.0 || c.loop ? std::max(c.fadeOut, 0.005) : c.fadeOut));
	const auto smooth = [](double x) { return static_cast<float>(x * x * (3.0 - 2.0 * x)); };
	for (std::size_t i = 0; i < in; ++i) out[i] *= smooth(static_cast<double>(i) / static_cast<double>(in));
	for (std::size_t i = 0; i < away; ++i) out[out.size() - 1 - i] *= smooth(static_cast<double>(i) / static_cast<double>(away));
	if (c.curve.empty() && c.follow.empty()) return out;
	const auto level = [&](double s) {
		double k = 1.0;
		if (!c.curve.empty()) {
			std::size_t seg = 0;
			while (seg + 1 < c.curve.size() && c.curve[seg + 1].first <= s) ++seg;
			k = s <= c.curve.front().first || seg + 1 == c.curve.size() ? c.curve[s <= c.curve.front().first ? 0 : seg].second
																		: c.curve[seg].second + (c.curve[seg + 1].second - c.curve[seg].second) * (s - c.curve[seg].first) / (c.curve[seg + 1].first - c.curve[seg].first);
		}
		return !c.follow.empty() && track ? k * std::max(0.0, track(c.follow, t + s)) : k;
	};
	constexpr std::size_t step = audio::kRate / 100;   // (a track is worked out each 1/100 s and joined up)
	for (std::size_t at = 0; at < out.size(); at += step) {
		const double a = level(static_cast<double>(at) / audio::kRate), b = level(static_cast<double>(at + step) / audio::kRate);
		for (std::size_t i = at; i < std::min(out.size(), at + step); ++i) out[i] *= static_cast<float>(a + (b - a) * static_cast<double>(i - at) / step);
	}
	return out;
}

// The cues on a mix: each at its time with its instrument (the film's own, else a generic one). What cannot be played is added to `problems`.
inline void play(audio::Mix &m, const Sheet &sheet, const std::map<std::string, CueInstrument> &own, const std::vector<double> &starts, const std::vector<std::string> &names,
				 std::vector<std::string> &problems, const Track &track = {}) {
	std::vector<Cue> cues;   // (an `oncuts` cue made into one at each cut)
	for (const Cue &c : sheet.cues) {
		if (!c.everyCut) {
			cues.push_back(c);
			continue;
		}
		for (std::size_t k = 1; k < starts.size(); ++k) {
			Cue at = c;
			at.absolute = true;
			at.time = starts[k] + c.time;
			cues.push_back(at);
		}
	}
	for (const Cue &c : cues) {
		double t = 0.0;
		std::string why;
		if (!timeOf(c, starts, names, t, why)) {
			problems.push_back("line " + std::to_string(c.line) + ": " + why);
			continue;
		}
		if (c.instrument == "sample") {   // a recording: as it is, at its gain and pan
			const Recording &r = recording(recordingPath(sheet, c.file));
			if (!c.follow.empty() && (!track || track(c.follow, t) < 0.0)) problems.push_back("line " + std::to_string(c.line) + ": the film has no `light " + c.follow + "` track to follow");
			if (r.sound.empty()) problems.push_back("line " + std::to_string(c.line) + ": the recording " + c.file + " " + (r.why.empty() ? "is empty" : r.why));
			else m.put(t, worked(r.sound, c, t, track), c.gain, c.pan, c.send);
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
		if (!timeOf(c, starts, names, t0, why) || t < t0 || (c.trim1 >= 0.0 && t - t0 + c.trim0 > c.trim1)) continue;
		const Recording &r = recording(recordingPath(sheet, c.file));
		const std::size_t k = static_cast<std::size_t>((t - t0 + c.trim0) * 100.0);
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
