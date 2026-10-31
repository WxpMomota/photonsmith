#pragma once
// film_score.h -- a first score for a film made from a file, drafted from what happens in it (`ray_tracer --film-score <key> [OUT]`): a cues file (music_cues.h) to start from.
//
// Each shot gets a mood from what its characters feel in it (their `feel` lines: happy, sad, scared ...; nothing felt: calm) and a pace from how it moves (running is lively,
// a short shot quicker), and from them a key (major or minor), a tempo and a progression of chords: a pad holding each chord, the bass on its root, a melody of its notes on
// the beat (a bell when it is happy, a pluck when calm, strings when sad, a low pulse when it is afraid), a swell into a surprise; and a hit on every cut. The levels are low:
// the music sits under the lines, and is ducked under them besides. A draft: change it as any cues file.
//
// Under the sea (round 17: a film with `underwater`) a calm or happy shot is scored for the water: slower, its chords with their sevenths (a floating, unresolved sound), a
// marimba-like mallet for the melody, everything sent deep into the reverb, and now and then a run of high notes rising like bubbles. (The whole score is heard muffled
// while the camera is under the water: DataFilm::musicCutoffs.)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "film_file.h"

namespace cartoon {
namespace score {

// A shot's mood: the expression felt most in it (the sum of every character's feelings over the shot), or "calm".
inline std::string moodOf(const film_file::DataFilm &d, double t0, double t1) {
	std::map<std::string, double> felt;
	for (const film_file::Character &c : d.spec.characters)
		for (const faces::Feeling &f : c.feelings)
			for (double t = t0; t < t1; t += 0.25) felt[f.when.pose] += rig::gestureWeight(f.when, t) * f.amount;
	std::string best = "calm";
	double most = 0.5;   // (a moment of a feeling is not the shot's mood)
	for (const auto &[e, w] : felt)
		if (w > most) most = w, best = e;
	return best;
}
// Whether anyone runs in the shot.
inline bool lively(const film_file::DataFilm &d, double t0, double t1) {
	for (const film_file::Character &c : d.spec.characters)
		for (const film_file::WalkLine &w : c.walks)
			if (w.style == film_file::WalkLine::Run && w.t0 < t1 && w.t0 + 3.0 > t0) return true;
	return false;
}

struct Style {
	bool minor = false;
	double bpm = 90.0;
	std::vector<std::vector<int>> chords;   // degrees of the scale (0 the tonic) each chord's root, third and fifth stand on
	std::string melody, pad = "pad";
	double melodyGain = 0.035;
	double send = 0.7;      // (how much of the chords goes to the reverb)
	bool bubbles = false;   // (under the sea: a rising run of high notes now and then)
};
inline Style styleOf(const std::string &mood, bool fast, bool sea = false) {
	Style s;
	if (sea && (mood == "calm" || mood == "happy" || mood == "smug" || mood == "sleepy" || mood == "thinking")) {
		s.bpm = mood == "happy" || mood == "smug" ? 92 : 72, s.chords = {{0, 2, 4, 6}, {3, 5, 7, 9}, {5, 7, 9, 11}, {1, 3, 5, 7}}, s.melody = "mallet", s.send = 0.9;   // Imaj7 IVmaj7 vi7 ii7
		s.bubbles = true;
	} else if (mood == "happy" || mood == "smug") {
		s.bpm = 112, s.chords = {{0, 2, 4}, {4, 6, 8}, {5, 7, 9}, {3, 5, 7}}, s.melody = "bell";   // I V vi IV
	} else if (mood == "sad") {
		s.minor = true, s.bpm = 66, s.chords = {{0, 2, 4}, {5, 7, 9}, {2, 4, 6}, {6, 8, 10}}, s.melody = "strings", s.pad = "strings", s.melodyGain = 0.025;   // i VI III VII
	} else if (mood == "scared" || mood == "worried" || mood == "angry" || mood == "disgusted") {
		s.minor = true, s.bpm = mood == "angry" ? 120 : 80, s.chords = {{0, 2, 4}, {3, 5, 7}}, s.melody = "knock", s.melodyGain = 0.05;   // i iv, a pulse
	} else if (mood == "surprised") {
		s.bpm = 100, s.chords = {{0, 3, 4}, {0, 2, 4}}, s.melody = "bell";   // sus4 to I
	} else if (mood == "sleepy" || mood == "thinking") {
		s.bpm = 70, s.chords = {{0, 2, 4}, {3, 5, 7}}, s.melody = "mallet", s.melodyGain = 0.025;
	} else {   // calm
		s.bpm = 88, s.chords = {{0, 2, 4}, {3, 5, 7}, {0, 2, 4}, {4, 6, 8}}, s.melody = "pluck";
	}
	if (fast) s.bpm *= 1.25;
	return s;
}
// A scale degree's MIDI note in C major or A minor from `root` (60 or 57), so many octaves up.
inline int noteOf(int degree, bool minor, int octave) {
	static const int major[] = {0, 2, 4, 5, 7, 9, 11}, aeolian[] = {0, 2, 3, 5, 7, 8, 10};
	const int root = minor ? 57 : 60;
	const int d = ((degree % 7) + 7) % 7, up = (degree - d) / 7;
	return root + (minor ? aeolian[d] : major[d]) + 12 * (up + octave);
}

// The draft: a cues file's text.
inline std::string draft(const film_file::DataFilm &d) {
	char buf[200];
	std::string out = "# A first score for \"" + d.spec.name + "\", drafted from what happens in it (film_score.h): a mood and a pace for each shot. Change it as any cues file.\n";
	out += "oncuts mallet A2 1.5 gain 0.04 send 0.6          # a hit on every cut\n";
	const std::vector<double> starts = d.shotStarts();
	for (std::size_t k = 0; k < starts.size(); ++k) {
		const double t0 = starts[k], t1 = k + 1 < starts.size() ? starts[k + 1] : d.spec.length;
		const std::string mood = moodOf(d, t0, t1);
		const bool fast = lively(d, t0, t1) || t1 - t0 < 3.0;
		const Style s = styleOf(mood, fast, d.spec.hasUnderwater);
		const double beat = 60.0 / s.bpm, bar = 4.0 * beat;
		std::snprintf(buf, sizeof buf, "\n[%zu]   # %s: %s, %s, %.0f a minute\n", k + 1, k < d.spec.shots.size() ? d.spec.shots[k].name.c_str() : "the film", mood.c_str(),
					  s.minor ? "minor" : "major", s.bpm);
		out += buf;
		int chord = 0;
		for (double at = 0.0; at < t1 - t0 - 0.2; at += 2.0 * bar, ++chord) {
			const std::vector<int> &c = s.chords[static_cast<std::size_t>(chord) % s.chords.size()];
			const double len = std::min(2.0 * bar, t1 - t0 - at) + 0.6;
			// (the chords round middle C and the bass an octave under, at levels heard over a film's ambience: an octave lower they were under 200 Hz, where small
			// speakers and ears are weakest, and birdsong at 2 to 5 kHz hid them)
			std::snprintf(buf, sizeof buf, "%-6.2f %-7s %d %d %d", at, s.pad.c_str(), noteOf(c[0], s.minor, 0), noteOf(c[1], s.minor, 0), noteOf(c[2], s.minor, 0));
			out += buf;
			if (c.size() > 3) out += " " + std::to_string(noteOf(c[3], s.minor, 0));   // (the seventh)
			std::snprintf(buf, sizeof buf, "  length %.2f  gain 0.05  send %.1f\n", len, s.send);
			out += buf;
			if (s.bubbles && chord % 2 == 1) {   // (bubbles: four notes of the chord two octaves up, rising quickly, soft, deep in the reverb)
				std::snprintf(buf, sizeof buf, "%-6.2f pluck   %d %d %d %d  spread 0.07  length 0.6  gain 0.018  send 1.0\n", at + bar + 0.5 * beat, noteOf(c[0], s.minor, 2),
							  noteOf(c[1], s.minor, 2), noteOf(c[2], s.minor, 2), noteOf(c[0] + 7, s.minor, 2));
				out += buf;
			}
			std::snprintf(buf, sizeof buf, "%-6.2f bass    %d  length %.2f  gain 0.08\n", at, noteOf(c[0], s.minor, -1), std::min(2.0 * bar, len));
			out += buf;
			for (int b = 0; b < 8 && at + b * beat < t1 - t0 - 0.1; b += (s.melody == "knock" ? 1 : 2)) {   // (the melody on every other beat; a pulse on every beat)
				const int degree = c[static_cast<std::size_t>((b / 2 + chord) % static_cast<int>(c.size()))] + (b % 4 == 2 ? 7 : 0);
				const double g = 2.0 * s.melodyGain;   // (the melody over the chords and the ambience)
				if (s.melody == "knock") std::snprintf(buf, sizeof buf, "%-6.2f knock  gain %.3f\n", at + b * beat, b % 4 == 0 ? g : 0.6 * g);
				else std::snprintf(buf, sizeof buf, "%-6.2f %-7s %d  length %.2f  gain %.3f  send 0.5\n", at + b * beat, s.melody.c_str(), noteOf(degree, s.minor, 0), 2.0 * beat, g);
				out += buf;
			}
		}
		if (mood == "surprised") out += "0.00   swell   length 1.2  gain 0.05\n";
	}
	return out;
}

}  // namespace score
}  // namespace cartoon
