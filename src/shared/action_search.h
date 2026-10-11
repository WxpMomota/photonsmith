#pragma once
// action_search.h -- the matching behind the Scene Builder's "search for an action" box (F3): which of a list of action names fit what was typed, best first. Qt-free and std-only
// so the unit tests and the GUI use the same code.
//
// A query is words; an action fits when EVERY word is found in its name, anywhere and in any order ("lock all" finds "Unlock everything locked"), ignoring the case of ASCII
// letters (names are UTF-8: a letter outside ASCII must match exactly). An empty query fits everything, in the order given. Best first means: names that START with the first
// word, then names with a WORD that starts with it, then the rest; within each, the order the actions were given in.

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace action_search {

inline std::string lowered(const std::string& s) {
	std::string out = s;
	for (char& c : out)
		if (static_cast<unsigned char>(c) < 0x80) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return out;
}

inline std::vector<std::string> wordsOf(const std::string& query) {
	std::vector<std::string> words;
	std::string cur;
	for (char c : query) {
		if (c == ' ' || c == '\t') {
			if (!cur.empty()) words.push_back(lowered(cur));
			cur.clear();
		} else {
			cur.push_back(c);
		}
	}
	if (!cur.empty()) words.push_back(lowered(cur));
	return words;
}

// True if `word` (already lowered) starts a word of `name` (already lowered): at the start, or after a space, a colon, a dash, a slash or a bracket.
inline bool startsAWord(const std::string& name, const std::string& word) {
	for (std::size_t at = name.find(word); at != std::string::npos; at = name.find(word, at + 1)) {
		if (at == 0) return true;
		const char before = name[at - 1];
		if (before == ' ' || before == ':' || before == '-' || before == '/' || before == '(' || before == '.') return true;
	}
	return false;
}

// The indices of the names that fit the query, best first.
inline std::vector<int> match(const std::vector<std::string>& names, const std::string& query) {
	const std::vector<std::string> words = wordsOf(query);
	struct Hit { int index; int rank; };
	std::vector<Hit> hits;
	for (std::size_t i = 0; i < names.size(); ++i) {
		const std::string name = lowered(names[i]);
		bool all = true;
		for (const std::string& w : words)
			if (name.find(w) == std::string::npos) { all = false; break; }
		if (!all) continue;
		int rank = 2;
		if (!words.empty()) {
			if (name.compare(0, words[0].size(), words[0]) == 0) rank = 0;
			else if (startsAWord(name, words[0])) rank = 1;
		} else {
			rank = 0;
		}
		hits.push_back({static_cast<int>(i), rank});
	}
	std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.rank < b.rank; });
	std::vector<int> out;
	for (const Hit& h : hits) out.push_back(h.index);
	return out;
}

}  // namespace action_search
