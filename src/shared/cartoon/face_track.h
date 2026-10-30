#pragma once
// face_track.h -- a face's acting from a video (`act NAME FILE.face at T [to T1] [amount A]`, round 12): ARKit's blend shapes over time, as
// tools/video_to_acting/video_to_acting.py writes them from MediaPipe's face landmarker (a line `names eyeBlinkLeft jawOpen ...`, then a line a frame: its time in
// seconds and each shape's weight 0..1).
//
// A rigged model with ARKit's morphs takes them as they are (film_rigged.h); a built-in character takes what they mean for its face (film_faces.h: the smile, the brows,
// the eyes, the lids) and its mouth (open, round, wide, the lips pressed), so the person, Pip and the turtle act with the actor's face too.

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "film_faces.h"
#include "film_speech.h"

namespace cartoon {
namespace face_track {

struct Track {
	std::vector<std::string> names;
	std::vector<double> times;
	std::vector<std::vector<double>> rows;   // a row a time, a weight a name
};
using Shapes = std::vector<std::pair<std::string, double>>;

// The track in `text`: "" or what is wrong with it.
inline std::string parse(const std::string &text, Track &t) {
	std::istringstream in(text);
	std::string line;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty() || line[0] == '#') continue;
		std::istringstream words(line);
		if (line.rfind("names", 0) == 0) {
			std::string w;
			words >> w;
			while (words >> w) t.names.push_back(w);
			continue;
		}
		if (t.names.empty()) return "a face track starts with a `names ...` line";
		double time = 0.0, v = 0.0;
		if (!(words >> time)) return "a face track's line is a time and a weight for each name";
		std::vector<double> row;
		while (words >> v) row.push_back(std::clamp(v, 0.0, 1.0));
		if (row.size() != t.names.size()) return "a face track's line has " + std::to_string(row.size()) + " weights for " + std::to_string(t.names.size()) + " names";
		if (!t.times.empty() && time < t.times.back()) return "a face track's times go back";
		t.times.push_back(time);
		t.rows.push_back(std::move(row));
	}
	if (t.rows.empty()) return "a face track with no frames";
	return std::string();
}
inline std::shared_ptr<const Track> load(const std::string &path, std::string &why) {
	std::ifstream f(path, std::ios::binary);
	if (!f) {
		why = "the face track " + path + " cannot be read";
		return nullptr;
	}
	auto t = std::make_shared<Track>();
	const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	if (const std::string e = parse(text, *t); !e.empty()) {
		why = path + ": " + e;
		return nullptr;
	}
	return t;
}

// The shapes at `t` seconds into the track (blended between its frames; held at its ends), each times `amount`.
inline Shapes at(const Track &k, double t, double amount = 1.0) {
	Shapes out;
	if (k.rows.empty()) return out;
	const auto hi = std::upper_bound(k.times.begin(), k.times.end(), t);
	const std::size_t b = std::min<std::size_t>(static_cast<std::size_t>(hi - k.times.begin()), k.times.size() - 1), a = b > 0 ? b - 1 : 0;
	const double u = k.times[b] > k.times[a] ? std::clamp((t - k.times[a]) / (k.times[b] - k.times[a]), 0.0, 1.0) : 0.0;
	for (std::size_t i = 0; i < k.names.size(); ++i) out.push_back({k.names[i], amount * (k.rows[a][i] + (k.rows[b][i] - k.rows[a][i]) * u)});
	return out;
}
inline double shape(const Shapes &s, const char *name) {
	for (const auto &[n, v] : s)
		if (n == name) return v;
	return 0.0;
}

// What the shapes mean to a built-in character's face (film_faces.h), added to its feelings, and to its mouth.
inline faces::Face faceOf(const Shapes &s) {
	const auto pair = [&](const char *l, const char *r) { return 0.5 * (shape(s, l) + shape(s, r)); };
	faces::Face f;
	f.smile = pair("mouthSmileLeft", "mouthSmileRight") - pair("mouthFrownLeft", "mouthFrownRight");
	f.browRaise = pair("browOuterUpLeft", "browOuterUpRight") + 0.5 * shape(s, "browInnerUp") - pair("browDownLeft", "browDownRight");
	f.browTilt = std::clamp(shape(s, "browInnerUp") - pair("browDownLeft", "browDownRight"), -1.0, 1.0);
	f.eyeWide = 0.3 * pair("eyeWideLeft", "eyeWideRight") - 0.15 * pair("eyeSquintLeft", "eyeSquintRight");
	f.lids = 0.95 * pair("eyeBlinkLeft", "eyeBlinkRight");
	f.mouthOpen = 0.0;   // (the mouth: mouthOf)
	return f;
}
inline speech::Mouth mouthOf(const Shapes &s) {
	const auto pair = [&](const char *l, const char *r) { return 0.5 * (shape(s, l) + shape(s, r)); };
	speech::Mouth m;
	m.closed = std::clamp(pair("mouthPressLeft", "mouthPressRight") + shape(s, "mouthClose"), 0.0, 1.0);
	m.open = shape(s, "jawOpen") * (1.0 - m.closed);
	m.round = std::max(shape(s, "mouthFunnel"), shape(s, "mouthPucker"));
	m.wide = std::clamp(pair("mouthStretchLeft", "mouthStretchRight") + 0.4 * pair("mouthSmileLeft", "mouthSmileRight"), 0.0, 1.0);
	m.teeth = shape(s, "mouthRollLower");
	return m;
}

}  // namespace face_track
}  // namespace cartoon
