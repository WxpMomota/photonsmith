#pragma once
// film_acting.h -- acting from the words: a gesture and an expression for a line of dialogue, read from what it says (film_file.h `acting words`), so a film blocked out
// from a screenplay is acted without a `gesture` or `feel` line for every line. A greeting waves, a yes nods, a no shakes the head, "look" or "there" points, "hmm" or
// "maybe" thinks, a happy exclamation cheers, a thank-you bows, and a question that does none of these shrugs; the words of a feeling (happy, sad, angry, scared,
// surprised, worried, thinking) show on the face. Each comes at the word that brings it - its place in the line, as a share of the line's length - and only poses the
// character's kind has are used. One gesture and one expression a line at most: the first word that asks for one.

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace cartoon {
namespace acting {

struct Cue {
	std::string what;          // a pose of the kind's library, or an expression
	double at = 0.0;           // the time of the word that brings it
	bool expression = false;
};

namespace detail {
struct Word {
	std::string text;   // lower case, letters and apostrophes only
	double share = 0.0; // where it starts in the line, 0..1
};
inline std::vector<Word> wordsOf(const std::string &line) {
	std::vector<Word> out;
	std::string w;
	std::size_t start = 0;
	for (std::size_t i = 0; i <= line.size(); ++i) {
		const char c = i < line.size() ? line[i] : ' ';
		if (std::isalpha(static_cast<unsigned char>(c)) || c == '\'') {
			if (w.empty()) start = i;
			w += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		} else if (!w.empty()) {
			out.push_back({w, line.empty() ? 0.0 : static_cast<double>(start) / static_cast<double>(line.size())});
			w.clear();
		}
	}
	return out;
}
inline bool in(const std::string &w, std::initializer_list<const char *> list) {
	for (const char *x : list)
		if (w == x) return true;
	return false;
}
inline bool has(const std::vector<std::string> &poses, const std::string &p) { return std::find(poses.begin(), poses.end(), p) != poses.end(); }
}  // namespace detail

// The gesture and the expression a line asks for, said from t0 to t1 by a character whose kind has `poses` (and a face to show an expression on, when `face`).
inline std::vector<Cue> cuesOf(const std::string &line, double t0, double t1, const std::vector<std::string> &poses, bool face) {
	using detail::in;
	const std::vector<detail::Word> words = detail::wordsOf(line);
	const auto when = [&](double share) { return t0 + share * std::max(0.0, t1 - t0); };
	std::string ending;   // the marks at its end ("?", "!", "?!" ...)
	for (std::size_t i = line.size(); i-- > 0;) {
		const char c = line[i];
		if (std::isspace(static_cast<unsigned char>(c)) || c == '"') continue;
		if (c != '?' && c != '!' && c != '.') break;
		ending += c;
	}
	const bool question = ending.find('?') != std::string::npos, exclaimed = line.find('!') != std::string::npos;
	std::vector<Cue> out;
	// the gesture: the first word that asks for one
	const auto gesture = [&](const std::string &pose, double share) {
		if (!out.empty() || !detail::has(poses, pose)) return false;
		out.push_back({pose, when(share), false});
		return true;
	};
	bool happyWord = false;
	for (const detail::Word &w : words)
		happyWord = happyWord || in(w.text, {"yay", "hooray", "wonderful", "great", "amazing", "brilliant", "fantastic", "yes", "awesome", "lovely"});
	for (std::size_t k = 0; k < words.size() && out.empty(); ++k) {
		const detail::Word &w = words[k];
		const bool opening = k < 2;
		if ((opening && in(w.text, {"hello", "hi", "hey", "bye", "goodbye", "morning", "evening", "afternoon", "welcome"})) && gesture("wave", w.share)) break;
		if (opening && in(w.text, {"yes", "yeah", "yep", "sure", "okay", "ok", "right", "indeed", "agreed", "alright"}) && !(exclaimed && happyWord && detail::has(poses, "cheer")) &&
			gesture("nod", w.share))
			break;
		if (in(w.text, {"no", "nope", "never", "nah", "not"}) && (opening || w.text != "not") && gesture("shake head", w.share)) break;
		if (in(w.text, {"look", "there", "over", "yonder"}) && (gesture("point", w.share) || gesture("point up", w.share))) break;
		if (in(w.text, {"hmm", "maybe", "perhaps", "wonder", "guess", "suppose"}) && gesture("think", w.share)) break;
		if (exclaimed && happyWord && in(w.text, {"yay", "hooray", "wonderful", "great", "amazing", "brilliant", "fantastic", "yes", "awesome", "lovely"}) && gesture("cheer", w.share)) break;
		if (in(w.text, {"thank", "thanks", "pleased"}) && (gesture("bow", w.share) || gesture("nod", w.share))) break;
	}
	if (out.empty() && question && !exclaimed) gesture("shrug", 0.7);   // (an exclaimed question is a surprise: below)
	// the expression: the first word of a feeling (a question that is exclaimed is a surprise)
	if (face) {
		std::string feeling;
		double share = 0.0;
		for (const detail::Word &w : words) {
			if (in(w.text, {"happy", "love", "lovely", "great", "wonderful", "yay", "hooray", "glad", "fun", "beautiful", "nice", "thank", "thanks", "amazing", "awesome"})) feeling = "happy";
			else if (in(w.text, {"sad", "sorry", "miss", "lost", "alone", "cry", "goodbye", "unfortunately"})) feeling = "sad";
			else if (in(w.text, {"angry", "mad", "hate", "stop", "enough", "furious"})) feeling = "angry";
			else if (in(w.text, {"scared", "afraid", "help", "danger", "frightened", "terrified"})) feeling = "scared";
			else if (in(w.text, {"wow", "whoa", "really", "what", "oh"}) && exclaimed) feeling = "surprised";
			else if (in(w.text, {"hmm", "maybe", "perhaps", "wonder", "think"})) feeling = "thinking";
			else if (in(w.text, {"worry", "worried", "careful", "hope", "nervous"})) feeling = "worried";
			if (!feeling.empty()) {
				share = w.share;
				break;
			}
		}
		if (feeling.empty() && question && exclaimed) feeling = "surprised";
		if (!feeling.empty()) out.push_back({feeling, when(share), true});
	}
	return out;
}

}  // namespace acting
}  // namespace cartoon
