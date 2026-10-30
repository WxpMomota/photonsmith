#pragma once
// film_speech.h -- a character's lines of dialogue (film_file.h `say NAME T "TEXT"`): when each syllable is spoken, the mouth shape it needs, how loud the voice is at a
// moment, and the voice itself - a cartoon babble made from the words (a buzz shaped by the vowels' formants, a hiss for an s, a pop for a p), so a line is heard in its
// rhythm, its vowels and its intonation without a recording or a speech engine. A recording can still be used instead (a cues file's `sample FILE.wav ... by NAME`).
//
// The timing follows the text: each word is cut into syllables at its vowel groups (a letter of another script - a Chinese character - is a syllable of its own), each takes
// about 0.17 s over `rate`, longer for its consonants; a comma pauses 0.22 s, a full stop, '!' or '?' 0.42 s. The pitch falls over a line, rises at the end of a question,
// and is higher for '!'.

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

#include "cartoon_audio.h"
#include "cartoon_lib.h"

namespace cartoon {
namespace speech {

// The visemes: the mouth shapes speech is drawn with, the fifteen of Oculus's lip sync (Ready Player Me's faces have a morph for each: viseme_sil, viseme_PP ...).
enum Viseme { kSil, kPP, kFF, kTH, kDD, kKK, kCH, kSS, kNN, kRR, kAA, kE, kI, kO, kU, kVisemes };
inline const char *visemeName(int v) {
	static const char *names[kVisemes] = {"sil", "PP", "FF", "TH", "DD", "kk", "CH", "SS", "nn", "RR", "aa", "E", "I", "O", "U"};
	return v >= 0 && v < kVisemes ? names[v] : "";
}
// A mouth shape: how far open, how wide (an "ee") and how round (an "oo"), each 0..1; how far the lips are pressed shut (an m, b, p) and the lower lip is under the teeth
// (an f, v); and how much of each viseme it is (summing to 1 while it speaks; all 0 when only the three numbers are known: a recording).
struct Mouth {
	double open = 0.0, wide = 0.0, round = 0.0;
	double closed = 0.0, teeth = 0.0;
	std::array<double, kVisemes> viseme{};
};
// What a viseme does to the three numbers (and the lips and teeth).
inline Mouth visemeShape(int v) {
	static const double table[kVisemes][5] = {
		{0.0, 0.0, 0.0, 0.0, 0.0},     // sil
		{0.0, 0.05, 0.0, 1.0, 0.0},    // PP: the lips pressed together
		{0.12, 0.35, 0.0, 0.0, 1.0},   // FF: the lower lip under the teeth
		{0.25, 0.30, 0.0, 0.0, 0.0},   // TH
		{0.30, 0.35, 0.0, 0.0, 0.0},   // DD (t d l)
		{0.35, 0.30, 0.0, 0.0, 0.0},   // kk (k g ng)
		{0.25, 0.05, 0.60, 0.0, 0.0},  // CH (ch sh j)
		{0.15, 0.70, 0.0, 0.0, 0.0},   // SS (s z)
		{0.25, 0.30, 0.0, 0.0, 0.0},   // nn
		{0.30, 0.0, 0.50, 0.0, 0.0},   // RR
		{0.95, 0.45, 0.0, 0.0, 0.0},   // aa
		{0.55, 0.80, 0.0, 0.0, 0.0},   // E
		{0.30, 1.00, 0.0, 0.0, 0.0},   // I
		{0.70, 0.0, 0.85, 0.0, 0.0},   // O
		{0.35, 0.0, 1.00, 0.0, 0.0}};  // U
	Mouth m;
	if (v < 0 || v >= kVisemes) return m;
	m.open = table[v][0], m.wide = table[v][1], m.round = table[v][2], m.closed = table[v][3], m.teeth = table[v][4];
	m.viseme[static_cast<std::size_t>(v)] = 1.0;
	return m;
}
// The visemes a mouth known only by its three numbers is most like (a recording's: speech::mouthsOf), for a face that is drawn with visemes.
inline void visemesFromShape(Mouth &m) {
	double sum = 0.0;
	for (double w : m.viseme) sum += w;
	if (sum > 0.0) return;
	const double talk = std::min(1.0, m.open / 0.5);
	m.viseme[kAA] = std::max(0.0, m.open - 0.5 * (m.wide + m.round));
	m.viseme[kE] = m.wide * (1.0 - 0.5 * clamp01(m.open)), m.viseme[kI] = m.wide * 0.5 * (1.0 - clamp01(m.open));
	m.viseme[kO] = m.round * clamp01(m.open / 0.6), m.viseme[kU] = m.round * (1.0 - clamp01(m.open / 0.6));
	sum = 0.0;
	for (double w : m.viseme) sum += w;
	if (sum > 1e-9)
		for (double &w : m.viseme) w *= talk / sum;
	m.viseme[kSil] = 1.0 - talk;
}

// A line of dialogue: who says it, when, the words, how fast (1 the usual), its pitch moved by so many semitones, and whether it is captioned.
struct Line {
	std::string who;
	double t = 0.0;
	std::string text;
	double rate = 1.0, pitch = 0.0;
	bool caption = true;
	int line = 0;   // (the film file's line)
};

struct Syllable {
	double t0 = 0.0, t1 = 0.0;   // the voiced part (film time)
	char vowel = 'a';            // a e i o u: its mouth shape and formants
	char onset = 0;              // its first consonant (0: none): m b p close the lips first; s f z v x c h hiss; t k d g pop
	int word = 0;                // which word of its line it is in (each letter of another script a word of its own: film_lipsync.h)
	double stress = 1.0;         // the first syllable of a word is a little louder
	double pitch = 0.0;          // semitones from the voice's own, from the intonation
};

namespace detail {
inline bool vowelLetter(char c) {
	c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y';
}
inline char vowelOf(const std::string &group) {   // the vowel a group sounds most like
	char v = static_cast<char>(std::tolower(static_cast<unsigned char>(group[0])));
	if (v == 'y') v = 'i';
	if (group.size() > 1) {   // a few pairs: "oo" an u, "ee" "ea" an i, "ou" "ow" an o
		const std::string g = group.substr(0, 2);
		if (g == "oo") v = 'u';
		else if (g == "ee" || g == "ea" || g == "ie") v = 'i';
		else if (g == "ou" || g == "ow" || g == "oa") v = 'o';
		else if (g == "ai" || g == "ay") v = 'e';
	}
	return v;
}
}  // namespace detail

// The syllables of a line, in film time.
inline std::vector<Syllable> syllablesOf(const Line &line) {
	std::vector<Syllable> out;
	const double rate = std::max(0.2, line.rate);
	double t = line.t;
	const std::string &s = line.text;
	std::size_t i = 0;
	int sentence0 = 0;
	int words = 0;   // (Syllable::word)   // the first syllable of the sentence being read (its intonation)
	const auto endSentence = [&](char mark) {
		const int n = static_cast<int>(out.size()) - sentence0;
		for (int k = 0; k < n; ++k) {
			Syllable &y = out[static_cast<std::size_t>(sentence0 + k)];
			const double u = n > 1 ? static_cast<double>(k) / (n - 1) : 1.0;
			y.pitch = 1.5 - 3.0 * u;                                           // it falls over the sentence
			if (mark == '?') y.pitch += 6.0 * std::max(0.0, u - 0.6) / 0.4;     // and rises at the end of a question
			if (mark == '!') y.pitch += 3.0;
		}
		sentence0 = static_cast<int>(out.size());
	};
	while (i < s.size()) {
		const unsigned char c = static_cast<unsigned char>(s[i]);
		if (c >= 0x80) {   // a letter of another script: a syllable of its own (its vowel from its code)
			std::size_t n = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
			unsigned code = 0;
			for (std::size_t k = 0; k < n && i + k < s.size(); ++k) code = code * 131u + static_cast<unsigned char>(s[i + k]);
			const bool punctuation = n == 3 && (s.compare(i, 3, "\xE3\x80\x82") == 0 || s.compare(i, 3, "\xEF\xBC\x8C") == 0 || s.compare(i, 3, "\xEF\xBC\x81") == 0 ||
												 s.compare(i, 3, "\xEF\xBC\x9F") == 0);   // (。 ， ！ ？)
			if (punctuation) {
				const bool comma = s.compare(i, 3, "\xEF\xBC\x8C") == 0;
				t += (comma ? 0.22 : 0.42) / rate;
				if (!comma) endSentence(s.compare(i, 3, "\xEF\xBC\x9F") == 0 ? '?' : s.compare(i, 3, "\xEF\xBC\x81") == 0 ? '!' : '.');
			} else {
				Syllable y;
				y.vowel = "aeiou"[code % 5];
				y.onset = "mbstkln"[(code / 5) % 7];
				y.t0 = t + 0.03 / rate;
				y.t1 = y.t0 + 0.17 / rate;
				t = y.t1 + 0.03 / rate;
				y.word = words++;
				out.push_back(y);
			}
			i += n;
			continue;
		}
		if (std::isalpha(c) || c == '\'') {   // a word
			std::size_t j = i;
			while (j < s.size() && (std::isalpha(static_cast<unsigned char>(s[j])) || s[j] == '\'')) ++j;
			std::string word;
			for (std::size_t k = i; k < j; ++k)
				if (s[k] != '\'') word += static_cast<char>(std::tolower(static_cast<unsigned char>(s[k])));
			// cut at its vowel groups: each syllable is the consonants before a group and the group (a final silent e joins the one before)
			std::vector<std::pair<std::string, std::string>> parts;   // (consonants, vowels)
			std::string cons;
			for (std::size_t k = 0; k < word.size();) {
				const bool v = detail::vowelLetter(word[k]) && !(word[k] == 'y' && k == 0);
				if (!v) {
					cons += word[k++];
					continue;
				}
				std::string group;
				while (k < word.size() && detail::vowelLetter(word[k])) group += word[k++];
				parts.push_back({cons, group});
				cons.clear();
			}
			if (parts.size() > 1 && parts.back().second == "e" && cons.empty() && parts.back().first.size() == 1) parts.pop_back();   // (a silent e: "time")
			if (parts.empty()) parts.push_back({word, "u"});   // (a word with no vowel: "hmm", "shh")
			for (std::size_t k = 0; k < parts.size(); ++k) {
				Syllable y;
				y.vowel = detail::vowelOf(parts[k].second);
				y.onset = parts[k].first.empty() ? 0 : parts[k].first[0];
				const double consonants = static_cast<double>(parts[k].first.size() + (k + 1 == parts.size() ? cons.size() : 0));
				y.stress = k == 0 ? 1.0 : 0.8;
				y.t0 = t + (0.02 + 0.03 * std::min(2.0, static_cast<double>(parts[k].first.size()))) / rate;
				y.t1 = y.t0 + (0.12 + 0.025 * std::min(3.0, consonants)) / rate;
				t = y.t1 + 0.02 / rate;
				y.word = words;
				out.push_back(y);
			}
			t += 0.05 / rate;   // between words
			++words;
			i = j;
			continue;
		}
		if (c == ',' || c == ';' || c == ':') t += 0.22 / rate;
		else if (c == '-' && i + 1 < s.size() && s[i + 1] == '-') t += 0.3 / rate;
		else if (c == '.' || c == '!' || c == '?') {
			t += 0.42 / rate;
			endSentence(static_cast<char>(c));
			while (i + 1 < s.size() && (s[i + 1] == '.' || s[i + 1] == '!' || s[i + 1] == '?')) ++i;   // ("..." "?!")
		}
		++i;
	}
	endSentence('.');
	for (Syllable &y : out) y.pitch += line.pitch;
	return out;
}
// When the line is over: its last syllable's end.
inline double endOf(const std::vector<Syllable> &syllables, double t0) { return syllables.empty() ? t0 : syllables.back().t1; }

// How loud a syllable is at t, 0..1: up in 0.025 s, held, down over 0.05 s.
inline double envelope(const Syllable &y, double t) {
	if (t <= y.t0 - 0.025 || t >= y.t1 + 0.05) return 0.0;
	const double up = smoothstep(y.t0 - 0.025, y.t0 + 0.01, t), down = 1.0 - smoothstep(y.t1 - 0.02, y.t1 + 0.05, t);
	return up * down * y.stress;
}
// The centres formantsOf finds for each vowel of an adult voice (measured on the Windows speech voice saying "father", "see" and "moon", and on the babble voice at
// 140 Hz; a child's or a small creature's higher voice is read less well).
constexpr double kRefA1 = 630, kRefA2 = 1090, kRefE1 = 470, kRefE2 = 1680, kRefI1 = 330, kRefI2 = 1950, kRefO1 = 495, kRefO2 = 980, kRefU1 = 300, kRefU2 = 1300;
inline Mouth shapeOf(char vowel) {
	switch (vowel) {
		case 'a': return {0.95, 0.45, 0.0};
		case 'e': return {0.55, 0.80, 0.0};
		case 'i': return {0.30, 1.00, 0.0};
		case 'o': return {0.70, 0.0, 0.85};
		default: return {0.35, 0.0, 1.0};   // u
	}
}
// The mouth at t for a line's syllables: each vowel's shape by its loudness; between two syllables of a phrase (less than 0.25 s apart) it does not shut but goes half-open
// from the one shape to the next, as a speaking mouth does; it shuts at a pause and the lips meet at the start of an m, b or p.
inline Mouth mouthAt(const std::vector<Syllable> &syllables, double t) {
	Mouth m;
	for (std::size_t i = 0; i + 1 < syllables.size(); ++i) {
		const Syllable &a = syllables[i], &b = syllables[i + 1];
		if (t <= a.t1 || t >= b.t0 || b.t0 - a.t1 > 0.25) continue;
		const double u = smoothstep(a.t1, b.t0, t);
		const Mouth sa = shapeOf(a.vowel), sb = shapeOf(b.vowel);
		const double hold = 0.45 * (1.0 - 0.6 * std::sin(kPi * u));   // (half open, a little less in the middle)
		const bool lips = b.onset == 'm' || b.onset == 'b' || b.onset == 'p';
		m.open = (sa.open + (sb.open - sa.open) * u) * hold * (lips ? 1.0 - smoothstep(0.0, 0.7, u) : 1.0);
		m.wide = (sa.wide + (sb.wide - sa.wide) * u) * hold;
		m.round = (sa.round + (sb.round - sa.round) * u) * hold;
		break;
	}
	for (const Syllable &y : syllables) {
		if (t < y.t0 - 0.08) break;
		const double e = envelope(y, t);
		if (e <= 0.0) continue;
		const Mouth v = shapeOf(y.vowel);
		double open = v.open * e;
		if ((y.onset == 'm' || y.onset == 'b' || y.onset == 'p') && t < y.t0 + 0.03) open *= smoothstep(y.t0 - 0.01, y.t0 + 0.03, t);
		m.open = std::max(m.open, std::min(1.0, open));
		m.wide = std::max(m.wide, v.wide * e);
		m.round = std::max(m.round, v.round * e);
	}
	return m;
}
// How loud the voice is at t, 0..1.
inline double loudnessAt(const std::vector<Syllable> &syllables, double t) {
	double l = 0.0;
	for (const Syllable &y : syllables) {
		if (t < y.t0 - 0.03) break;
		l = std::max(l, envelope(y, t));
	}
	return std::min(1.0, l);
}

// ---- mouths from a recording ----
// A recorded voice's first two formants, one estimate every 1/100 s (0 where it is quiet): the centres of its energy between 200 Hz and 1 kHz and between 0.9 and 3 kHz,
// through a Hann window of 1024 samples round each moment.
inline void formantsOf(const std::vector<float> &sound, const std::vector<float> &loudness, std::vector<double> &f1, std::vector<double> &f2) {
	f1.assign(loudness.size(), 0.0);
	f2.assign(loudness.size(), 0.0);
	const int window = 1024, hop = audio::kRate / 100;
	std::vector<double> hann(window);
	for (int i = 0; i < window; ++i) hann[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (window - 1));
	for (std::size_t f = 0; f < loudness.size(); ++f) {
		if (loudness[f] < 0.08f) continue;
		const long centre = static_cast<long>(f) * hop + hop / 2;
		const auto energy = [&](double hz) {   // (Goertzel at one frequency)
			const double w = 2.0 * kPi * hz / audio::kRate, k = 2.0 * std::cos(w);
			double s1 = 0.0, s2 = 0.0;
			for (int i = 0; i < window; ++i) {
				const long at = centre - window / 2 + i;
				const double x = at >= 0 && at < static_cast<long>(sound.size()) ? sound[static_cast<std::size_t>(at)] * hann[static_cast<std::size_t>(i)] : 0.0;
				const double s0 = x + k * s1 - s2;
				s2 = s1;
				s1 = s0;
			}
			return s1 * s1 + s2 * s2 - k * s1 * s2;
		};
		double e1 = 0.0, c1 = 0.0, e2 = 0.0, c2 = 0.0;
		for (double hz = 200.0; hz <= 1000.0; hz += 50.0) {
			const double e = energy(hz);
			e1 += e, c1 += e * hz;
		}
		for (double hz = 900.0; hz <= 3000.0; hz += 75.0) {
			const double e = energy(hz);
			e2 += e, c2 += e * hz;
		}
		if (e1 > 0.0 && e2 > 0.0) f1[f] = c1 / e1, f2[f] = c2 / e2;
	}
}
// A recorded voice's mouth shapes, one every 1/100 s: each moment's formants (formantsOf) compared with those the five vowels give such centres (in octaves), and the
// vowels' mouths blended by how near each is; by how loud it is then (`loudness`, 0..1); quiet, the mouth shuts.
inline std::vector<Mouth> mouthsOf(const std::vector<float> &sound, const std::vector<float> &loudness) {
	std::vector<double> f1, f2;
	formantsOf(sound, loudness, f1, f2);
	struct Ref {
		char vowel;
		double f1, f2;
	};
	static const Ref refs[] = {{'a', kRefA1, kRefA2}, {'e', kRefE1, kRefE2}, {'i', kRefI1, kRefI2}, {'o', kRefO1, kRefO2}, {'u', kRefU1, kRefU2}};
	std::vector<Mouth> out(loudness.size());
	for (std::size_t f = 0; f < out.size(); ++f) {
		if (f1[f] <= 0.0) continue;
		double weights = 0.0;
		Mouth m;
		for (const Ref &r : refs) {
			const double d1 = std::log2(f1[f] / r.f1), d2 = std::log2(f2[f] / r.f2);
			const double w = std::exp(-(d1 * d1 + d2 * d2) / (2.0 * 0.15 * 0.15));
			const Mouth v = shapeOf(r.vowel);
			m.open += w * v.open, m.wide += w * v.wide, m.round += w * v.round;
			weights += w;
		}
		if (weights <= 1e-12) continue;
		const double l = std::min(1.0, loudness[f] * 1.4) / weights;
		out[f] = {m.open * l, m.wide * l, m.round * l};
	}
	// a little smoothing over three hundredths: a mouth does not jump with every window
	std::vector<Mouth> smooth = out;
	for (std::size_t f = 1; f + 1 < out.size(); ++f) {
		smooth[f].open = (out[f - 1].open + 2.0 * out[f].open + out[f + 1].open) / 4.0;
		smooth[f].wide = (out[f - 1].wide + 2.0 * out[f].wide + out[f + 1].wide) / 4.0;
		smooth[f].round = (out[f - 1].round + 2.0 * out[f].round + out[f + 1].round) / 4.0;
	}
	return smooth;
}

// ---- the voice ----
// A voice: its pitch (Hz) and how far its formants are moved up (a small creature's are higher: 1.25), and a little breath.
struct Voice {
	double pitch = 150.0;
	double formants = 1.0;
	double breath = 0.04;
};

namespace detail {
// A two-pole resonance (band pass) at f Hz with a bandwidth of bw Hz.
struct Resonator {
	double a1 = 0.0, a2 = 0.0, b0 = 0.0, y1 = 0.0, y2 = 0.0;
	void tune(double f, double bw) {
		const double r = std::exp(-kPi * bw / audio::kRate);
		a1 = 2.0 * r * std::cos(2.0 * kPi * f / audio::kRate);
		a2 = -r * r;
		b0 = 1.0 - r;
	}
	double operator()(double x) {
		const double y = b0 * x + a1 * y1 + a2 * y2;
		y2 = y1;
		y1 = y;
		return y;
	}
};
inline void formantsOf(char vowel, double f[3]) {
	static const double table[5][3] = {{730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}, {570, 840, 2410}, {300, 870, 2240}};
	const char *order = "aeiou";
	int k = 4;
	for (int i = 0; i < 5; ++i)
		if (order[i] == vowel) k = i;
	for (int i = 0; i < 3; ++i) f[i] = table[k][i];
}
inline bool hissing(char c) { return c == 's' || c == 'f' || c == 'z' || c == 'v' || c == 'x' || c == 'c' || c == 'h' || c == 'j'; }
inline bool popping(char c) { return c == 't' || c == 'k' || c == 'd' || c == 'g' || c == 'p' || c == 'b' || c == 'q'; }
}  // namespace detail

// The sound of a line (mono, from its start), spoken in `voice`; `seed` varies its breath.
inline std::vector<float> speak(const std::vector<Syllable> &syllables, double t0, const Voice &voice, unsigned seed = 1) {
	if (syllables.empty()) return {};
	const double end = syllables.back().t1 + 0.12;
	std::vector<float> out(audio::samplesFor(end - t0), 0.0f), consonants(out.size(), 0.0f);
	audio::Noise noise{seed};
	double phase = 0.0, lowNoise = 0.0, prevNoise = 0.0;
	std::size_t at = 0;   // the syllable being spoken
	detail::Resonator res[3];
	char tuned = 0;
	for (std::size_t n = 0; n < out.size(); ++n) {
		const double t = t0 + static_cast<double>(n) / audio::kRate;
		while (at + 1 < syllables.size() && t > syllables[at].t1 + 0.05 && t > syllables[at + 1].t0 - 0.08) ++at;
		const Syllable &y = syllables[at];
		const double e = envelope(y, t);
		// the buzz: a pitch that glides to the syllable's, a sawtooth with a little vibrato, through the vowel's three formants
		const double vib = 1.0 + 0.012 * std::sin(2.0 * kPi * 5.5 * t);
		const double f0 = voice.pitch * std::pow(2.0, y.pitch / 12.0) * vib;
		phase += f0 / audio::kRate;
		phase -= std::floor(phase);
		const double saw = 2.0 * phase - 1.0;
		if (tuned != y.vowel) {
			double f[3];
			detail::formantsOf(y.vowel, f);
			for (int k = 0; k < 3; ++k) res[k].tune(f[k] * voice.formants, 60.0 + 40.0 * k);
			tuned = y.vowel;
		}
		const double white = noise.next();
		const double source = saw + voice.breath * white;
		const double voiced = (res[0](source) * 1.0 + res[1](source) * 0.55 + res[2](source) * 0.25) * e;
		// a consonant before the vowel: a hiss (noise with its low end taken away) or a short pop
		double consonant = 0.0;
		if (y.onset && t < y.t0 + 0.01) {
			const double since = t - (y.t0 - 0.07);
			lowNoise += 0.35 * (white - lowNoise);
			if (detail::hissing(y.onset) && since > 0.0) consonant = (white - lowNoise) * 0.18 * smoothstep(0.0, 0.03, since) * (1.0 - smoothstep(0.05, 0.08, since));
			if (detail::popping(y.onset) && t > y.t0 - 0.025) consonant = (white - prevNoise) * 0.12 * std::exp(-(t - (y.t0 - 0.025)) / 0.008);
		}
		prevNoise = white;
		out[n] = static_cast<float>(voiced);
		consonants[n] = static_cast<float>(consonant);
	}
	// the buzz at a peak of 0.6 (whatever its vowels' resonances make of it), the consonants beside it as they are
	float peak = 0.0f;
	for (float v : out) peak = std::max(peak, std::abs(v));
	const float k = peak > 0.0f ? 0.6f / peak : 0.0f;
	for (std::size_t n = 0; n < out.size(); ++n) out[n] = out[n] * k + consonants[n];
	audio::edges(out, 0.002, 0.03);
	return out;
}

}  // namespace speech
}  // namespace cartoon
