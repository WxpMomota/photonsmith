#pragma once
// film_lipsync.h -- lip sync from the words of a `say` line (round 12): the mouth takes the shape of each sound, not only of each syllable's vowel.
//
//   the sounds    each word is read into visemes (film_speech.h: the fifteen of Oculus's lip sync) by English spelling rules - th, sh, ch, ph, wh, ck, ng, qu, a soft c and g,
//                 silent letters (kn, wr, gn, mb, gh, a final e), a magic e (time, home, cute), the vowel pairs (ee, oo, ou, oi, ai ...), r after a vowel - and a list of
//                 common words English does not spell as it says (the, one, said, would ...). A letter of another script (a Chinese character) is its syllable's onset and vowel.
//   the timing    a word's visemes share the time its syllables are spoken (film_speech.h syllablesOf), a vowel twice a consonant's
//   the mouth     coarticulation, as Cohen and Massaro model it: each sound pulls the mouth towards its shape by a dominance that falls off away from it, so a quiet sound
//                 (t, k, n) takes its neighbours' shape while the lips must meet for an m, b or p and the lip must touch the teeth for an f or a v; the shapes lead the
//                 sound by 35 ms, as a mouth gets ready for a sound before it is heard; between words of a phrase it does not shut, at a pause it rests.

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "film_speech.h"

namespace cartoon {
namespace speech {
namespace lipsync {

// A sound of a line: when it is heard and its viseme.
struct Phone {
	double t0 = 0.0, t1 = 0.0;
	int viseme = kSil;
};

namespace detail {
// Visemes written as letters: p PP, f FF, t TH, d DD, k kk, c CH, s SS, n nn, r RR, a aa, e E, i I, o O, u U.
inline int visemeOfLetter(char c) {
	switch (c) {
		case 'p': return kPP;
		case 'f': return kFF;
		case 't': return kTH;
		case 'd': return kDD;
		case 'k': return kKK;
		case 'c': return kCH;
		case 's': return kSS;
		case 'n': return kNN;
		case 'r': return kRR;
		case 'a': return kAA;
		case 'e': return kE;
		case 'i': return kI;
		case 'o': return kO;
		case 'u': return kU;
		default: return kSil;
	}
}
inline bool vowel(char c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y'; }
// The common words English does not spell as they sound, in viseme letters (above).
inline const std::map<std::string, std::string> &irregular() {
	static const std::map<std::string, std::string> words = {
		{"the", "ta"}, {"a", "a"}, {"of", "af"}, {"to", "du"}, {"do", "du"}, {"you", "iu"}, {"your", "or"}, {"are", "ar"}, {"was", "uas"}, {"were", "ur"},
		{"one", "uan"}, {"two", "du"}, {"who", "u"}, {"what", "uad"}, {"where", "uer"}, {"there", "ter"}, {"their", "ter"}, {"they", "tei"}, {"said", "sed"},
		{"says", "ses"}, {"have", "af"}, {"give", "kif"}, {"live", "dif"}, {"come", "kap"}, {"some", "sap"}, {"done", "dan"}, {"none", "nan"}, {"love", "daf"},
		{"move", "puf"}, {"does", "das"}, {"goes", "kous"}, {"been", "pin"}, {"eye", "ai"}, {"i", "ai"}, {"is", "is"}, {"his", "is"}, {"as", "as"}, {"has", "as"},
		{"many", "peni"}, {"any", "eni"}, {"only", "oundi"}, {"once", "uans"}, {"friend", "frend"}, {"people", "pipad"}, {"island", "aidand"}, {"would", "ud"},
		{"could", "kud"}, {"should", "cud"}, {"through", "tru"}, {"though", "tou"}, {"enough", "inaf"}, {"laugh", "daf"}, {"door", "dor"}, {"floor", "fdor"},
		{"four", "for"}, {"great", "kreid"}, {"break", "preik"}, {"eight", "eid"}, {"heart", "ard"}, {"earth", "rt"}, {"learn", "drn"}, {"word", "urd"},
		{"work", "urk"}, {"world", "urdd"}, {"water", "uodr"}, {"women", "uipan"}, {"woman", "uupan"}, {"busy", "pisi"}, {"again", "aken"}, {"against", "akensd"},
		{"about", "apaud"}, {"above", "apaf"}, {"because", "pikos"}, {"before", "pifor"}, {"idea", "aidia"}, {"quiet", "kuaiad"}, {"ok", "oukei"}, {"okay", "oukei"},
		{"hello", "edou"}, {"yes", "ies"}, {"no", "nou"}, {"go", "kou"}, {"so", "sou"}, {"oh", "ou"}, {"hi", "ai"}, {"hey", "ei"}, {"why", "uai"}, {"buy", "pai"},
		{"my", "pai"}, {"by", "pai"}, {"be", "pi"}, {"he", "i"}, {"she", "ci"}, {"we", "ui"}, {"me", "pi"}, {"from", "fram"}, {"for", "for"}, {"or", "or"},
		{"into", "indu"}, {"onto", "ondu"}, {"put", "pud"}, {"push", "puc"}, {"pull", "pud"}, {"full", "fud"}, {"sure", "cur"}, {"sugar", "cukr"}, {"ocean", "ocan"},
		{"answer", "ansr"}, {"listen", "disan"}, {"often", "ofan"}, {"climb", "kdaip"}, {"thumb", "tap"}, {"knee", "ni"}, {"know", "nou"}, {"knew", "niu"},
		{"blood", "pdad"}, {"flood", "fdad"}, {"young", "iak"}, {"touch", "dac"}, {"country", "kandri"}, {"double", "dapad"}, {"trouble", "drapad"}, {"cousin", "kasan"},
		{"wolf", "uudf"}, {"wool", "uud"}, {"good", "kud"}, {"look", "duk"}, {"book", "puk"}, {"took", "duk"}, {"foot", "fud"}, {"stood", "sdud"}, {"wood", "uud"},
		{"cheese", "cis"}, {"moon", "pun"}, {"made", "peid"}, {"rock", "rok"}, {"think", "tink"}, {"day", "dei"}, {"what's", "uads"}, {"it's", "ids"}, {"don't", "dound"},
		{"can't", "kand"}, {"won't", "uound"}, {"i'm", "aip"}, {"you're", "ior"}, {"we're", "uir"}, {"they're", "ter"}, {"let's", "deds"}};
	return words;
}
}  // namespace detail

// A word's visemes (lower case letters, no apostrophes needed).
inline std::vector<int> visemesOf(std::string w) {
	for (char &c : w) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	std::vector<int> out;
	if (const auto it = detail::irregular().find(w); it != detail::irregular().end()) {
		for (char c : it->second) out.push_back(detail::visemeOfLetter(c));
		return out;
	}
	w.erase(std::remove(w.begin(), w.end(), '\''), w.end());
	std::string v;   // (viseme letters)
	const auto at = [&](std::size_t i) { return i < w.size() ? w[i] : '\0'; };
	const auto is = [&](std::size_t i, const char *s) { return w.compare(i, std::string(s).size(), s) == 0; };
	const std::size_t n = w.size();
	// a magic e: a vowel, one consonant and a last e (or es / ed): the vowel says its name, the e is silent
	const auto magicE = [&](std::size_t i) {
		if (i + 2 >= n || detail::vowel(at(i + 1)) || at(i + 1) == 'r' || at(i + 1) == 'w' || at(i + 1) == 'x') return false;
		const std::size_t e = i + 2;
		return at(e) == 'e' && (e + 1 == n || (e + 2 == n && (at(e + 1) == 's' || at(e + 1) == 'd')));
	};
	for (std::size_t i = 0; i < n;) {
		const char c = w[i];
		// silent letters at the start and the end
		if (i == 0 && (is(0, "kn") || is(0, "gn") || is(0, "pn"))) { v += 'n'; i += 2; continue; }
		if (i == 0 && is(0, "wr")) { v += 'r'; i += 2; continue; }
		if (i == 0 && is(0, "ps")) { v += 's'; i += 2; continue; }
		if (is(i, "mb") && i + 2 == n) { v += 'p'; i += 2; continue; }
		// endings that are not spelt as said
		if (is(i, "tion") || is(i, "sion")) { v += "can"; i += 4; continue; }
		if (is(i, "ture")) { v += "cr"; i += 4; continue; }
		if (is(i, "ough")) { v += at(i + 4) == 't' ? "o" : (i + 4 == n ? "ou" : "o"); i += 4; continue; }
		if (is(i, "augh")) { v += 'o'; i += 4; continue; }
		if (is(i, "eigh")) { v += "ei"; i += 4; continue; }
		if (is(i, "igh")) { v += "ai"; i += 3; continue; }
		if (c == 'g' && at(i + 1) == 'h') { if (i == 0) v += 'k'; i += 2; continue; }   // (a ghost's is hard; elsewhere silent)
		// consonant pairs
		if (is(i, "tch")) { v += 'c'; i += 3; continue; }
		if (is(i, "th")) { v += 't'; i += 2; continue; }
		if (is(i, "sh") || is(i, "ch")) { v += 'c'; i += 2; continue; }
		if (is(i, "ph")) { v += 'f'; i += 2; continue; }
		if (is(i, "wh")) { v += 'u'; i += 2; continue; }
		if (is(i, "ck")) { v += 'k'; i += 2; continue; }
		if (is(i, "ng")) { v += 'k'; i += 2; continue; }
		if (is(i, "qu")) { v += "ku"; i += 2; continue; }
		// a vowel and an r: one sound (er, ir, ur), or the vowel and the r (ar, or)
		if ((c == 'e' || c == 'i' || c == 'u') && at(i + 1) == 'r' && !detail::vowel(at(i + 2))) { v += 'r'; i += 2; continue; }
		if ((c == 'a' || c == 'o') && at(i + 1) == 'r' && !detail::vowel(at(i + 2))) { v += c; v += 'r'; i += 2; continue; }
		// vowel pairs
		if (is(i, "ee") || is(i, "ea") || (is(i, "ie") && i + 2 < n) || is(i, "ei") || (is(i, "ey") && i + 2 == n)) { v += 'i'; i += 2; continue; }
		if (is(i, "ie") && i + 2 == n) { v += "ai"; i += 2; continue; }
		if (is(i, "oo")) { v += 'u'; i += 2; continue; }
		if (is(i, "ou")) { v += "au"; i += 2; continue; }
		if (is(i, "ow")) { v += i + 2 == n ? "ou" : "au"; i += 2; continue; }
		if (is(i, "oi") || is(i, "oy")) { v += "oi"; i += 2; continue; }
		if (is(i, "au") || is(i, "aw")) { v += 'o'; i += 2; continue; }
		if (is(i, "ai") || is(i, "ay")) { v += "ei"; i += 2; continue; }
		if (is(i, "oa")) { v += "ou"; i += 2; continue; }
		if (is(i, "ue") || is(i, "ew")) { v += "iu"; i += 2; continue; }
		if (is(i, "ui")) { v += 'u'; i += 2; continue; }
		// single vowels: a magic e makes one say its name
		if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u') {
			if (magicE(i)) {
				v += c == 'a' ? "ei" : c == 'i' ? "ai" : c == 'o' ? "ou" : c == 'u' ? "iu" : "i";
				w[i + 2] = '_';   // (the e is silent; the consonant is read as itself next, soft before it: face, age)
				++i;
				continue;
			}
			if (c == 'e' && i + 1 == n && n > 2) { ++i; continue; }   // (a final e is silent)
			v += c == 'u' ? 'a' : c == 'o' ? 'o' : c;   // (cup, hot)
			++i;
			continue;
		}
		if (c == 'y') {
			if (i == 0 && detail::vowel(at(1))) v += 'i';                                                // (yes)
			else if (i + 1 == n) v += std::none_of(w.begin(), w.begin() + static_cast<std::ptrdiff_t>(i), detail::vowel) ? "ai" : "i";   // (my, happy)
			else v += 'i';                                                                                // (gym)
			++i;
			continue;
		}
		// single consonants
		switch (c) {
			case 'b': case 'm': case 'p': v += 'p'; break;
			case 'f': case 'v': v += 'f'; break;
			case 't': case 'd': case 'l': v += 'd'; break;
			case 'n': v += 'n'; break;
			case 'k': case 'q': v += 'k'; break;
			case 'g': v += at(i + 1) == 'e' || at(i + 1) == 'i' || at(i + 1) == 'y' || at(i + 1) == '_' ? 'c' : 'k'; break;
			case 'c': v += at(i + 1) == 'e' || at(i + 1) == 'i' || at(i + 1) == 'y' || at(i + 1) == '_' ? 's' : 'k'; break;
			case 's': case 'z': v += 's'; break;
			case 'x': v += i == 0 ? "s" : "ks"; break;
			case 'j': v += 'c'; break;
			case 'r': v += 'r'; break;
			case 'w': v += 'u'; break;
			default: break;   // (h, and the silent e a magic e left: '_')
		}
		++i;
	}
	for (char c : v) {
		const int x = detail::visemeOfLetter(c);
		if (x == kSil) continue;
		if (!out.empty() && out.back() == x && x != kAA && x != kE && x != kI && x != kO && x != kU) continue;   // (a double consonant is one sound)
		out.push_back(x);
	}
	if (out.empty()) out.push_back(kNN);   // (hmm)
	return out;
}

// The words of a line in the order syllablesOf numbers them (Syllable::word): a run of letters and apostrophes, or a letter of another script (as "" here: its syllable
// gives its sounds).
inline std::vector<std::string> wordsOf(const std::string &s) {
	std::vector<std::string> out;
	for (std::size_t i = 0; i < s.size();) {
		const unsigned char c = static_cast<unsigned char>(s[i]);
		if (c >= 0x80) {
			const std::size_t n = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
			const bool punctuation = n == 3 && (s.compare(i, 3, "\xE3\x80\x82") == 0 || s.compare(i, 3, "\xEF\xBC\x8C") == 0 || s.compare(i, 3, "\xEF\xBC\x81") == 0 ||
												 s.compare(i, 3, "\xEF\xBC\x9F") == 0);
			if (!punctuation) out.push_back("");
			i += n;
			continue;
		}
		if (std::isalpha(c) || c == '\'') {
			std::size_t j = i;
			std::string word;
			while (j < s.size() && (std::isalpha(static_cast<unsigned char>(s[j])) || s[j] == '\'')) word += s[j++];
			out.push_back(word);
			i = j;
			continue;
		}
		++i;
	}
	return out;
}

// A line's sounds in film time: each word's visemes over the time its syllables are spoken (from a little before the first, for its first consonants, to the end of the
// last), a vowel twice as long as a consonant.
inline std::vector<Phone> phonesOf(const Line &line, const std::vector<Syllable> &syllables) {
	std::vector<Phone> out;
	const std::vector<std::string> words = wordsOf(line.text);
	const double rate = std::max(0.2, line.rate);
	for (std::size_t a = 0; a < syllables.size();) {
		std::size_t b = a;
		while (b < syllables.size() && syllables[b].word == syllables[a].word) ++b;
		const std::size_t wi = static_cast<std::size_t>(syllables[a].word);
		std::vector<int> v;
		if (wi < words.size() && !words[wi].empty()) v = visemesOf(words[wi]);
		if (v.empty()) {   // (another script: each syllable's onset and vowel)
			for (std::size_t k = a; k < b; ++k) {
				const char o = syllables[k].onset;
				const int on = o == 'm' || o == 'b' || o == 'p' ? kPP : o == 's' ? kSS : o == 't' || o == 'l' ? kDD : o == 'k' ? kKK : o == 'n' ? kNN : kSil;
				if (on != kSil) v.push_back(on);
				v.push_back(detail::visemeOfLetter(syllables[k].vowel));
			}
		}
		const double t0 = syllables[a].t0 - 0.05 / rate, t1 = syllables[b - 1].t1;
		double units = 0.0;
		for (int x : v) units += x >= kAA ? 2.0 : 1.0;
		double t = t0;
		for (int x : v) {
			const double d = (t1 - t0) * (x >= kAA ? 2.0 : 1.0) / std::max(1.0, units);
			out.push_back({t, t + d, x});
			t += d;
		}
		a = b;
	}
	return out;
}

// How strongly each viseme holds the mouth to its shape (Cohen and Massaro's dominance): the lips must meet for an m, b, p and touch the teeth for an f, v; a t, k or n is
// hardly seen and takes its neighbours' shape.
inline double dominance(int v) {
	static const double d[kVisemes] = {0.15, 1.0, 0.9, 0.5, 0.35, 0.3, 0.7, 0.5, 0.35, 0.6, 0.8, 0.8, 0.8, 0.8, 0.8};
	return v >= 0 && v < kVisemes ? d[v] : 0.15;
}
// The mouth at t: every sound near it pulling towards its shape (see the top), shapes leading the sound by 35 ms, resting where no sound is near.
inline Mouth mouthAt(const std::vector<Phone> &phones, double t) {
	constexpr double kLead = 0.035, kFall = 0.028;   // (seconds: how far ahead, how fast a sound's pull falls off beyond its own time)
	const double u = t + kLead;
	std::array<double, kVisemes> w{};
	double sum = 0.0, nearest = 1e30;
	for (const Phone &p : phones) {
		if (p.t1 < u - 0.4) continue;
		if (p.t0 > u + 0.4) break;
		const double c = 0.5 * (p.t0 + p.t1), half = 0.5 * (p.t1 - p.t0);
		const double off = std::max(0.0, std::abs(u - c) - 0.6 * half);
		nearest = std::min(nearest, std::max(0.0, std::abs(u - c) - half));
		const double d = dominance(p.viseme) * std::exp(-off / kFall);
		w[static_cast<std::size_t>(p.viseme)] += d;
		sum += d;
	}
	const double rest = 0.15 + 2.0 * smoothstep(0.12, 0.30, nearest);   // (a pause: the mouth comes to rest; a short gap between words does not shut it)
	w[kSil] += rest;
	sum += rest;
	Mouth m;
	for (int v = 0; v < kVisemes; ++v) {
		const double k = w[static_cast<std::size_t>(v)] / sum;
		if (k <= 0.0) continue;
		const Mouth s = visemeShape(v);
		m.open += k * s.open, m.wide += k * s.wide, m.round += k * s.round, m.closed += k * s.closed, m.teeth += k * s.teeth;
		m.viseme[static_cast<std::size_t>(v)] = k;
	}
	m.open *= 1.0 - smoothstep(0.35, 0.7, m.closed);   // (lips that must meet do meet)
	return m;
}

}  // namespace lipsync
}  // namespace speech
}  // namespace cartoon
