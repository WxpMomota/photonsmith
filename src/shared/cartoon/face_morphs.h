#pragma once
// face_morphs.h -- a rigged model's face (round 12): its morph targets (gltf_skin.h: Blender's shape keys) driven as the built-in characters' faces are - blinking on their own,
// the feelings of `feel` lines (film_faces.h), the mouth shapes of `say` lines (film_lipsync.h visemes) and of recordings.
//
// A model's morphs are found by their names, as the usual tools name them (any case; a prefix such as "blendShape1." or "Face." is allowed):
//
//   ARKit (Apple's 52; Ready Player Me, Character Creator, many VTuber models)   eyeBlinkLeft, jawOpen, mouthSmileLeft, browInnerUp, mouthFunnel, mouthPucker ...
//   Oculus visemes (Ready Player Me)                                             viseme_sil, viseme_PP, viseme_FF ... viseme_aa, viseme_E, viseme_I, viseme_O, viseme_U
//   VRoid / VRM                                                                  Fcl_EYE_Close, Fcl_MTH_A ... Fcl_MTH_O, Fcl_ALL_Joy, Fcl_BRW_Angry ...; VRM 1's aa ih ou ee oh
//   plain                                                                        blink, smile, frown, angry, surprised, sad, A I U E O, mouthOpen
//
// A face with visemes speaks with them; one with A I U E O speaks with those (the consonants as the vowel nearest them, the lips' closing as a shut mouth); one with only
// ARKit's speaks with its jaw, funnel, pucker, stretch and press. --film-check says which a model has.

#include <algorithm>
#include <array>
#include <cctype>
#include <initializer_list>
#include <string>
#include <vector>

#include "../gltf_skin.h"
#include "film_faces.h"
#include "film_speech.h"

namespace cartoon {
namespace face_morphs {

// What a face is doing, 0..1 each.
struct Controls {
	double blinkL = 0.0, blinkR = 0.0;
	double smile = 0.0, frown = 0.0;
	double browUp = 0.0, browDown = 0.0, browWorried = 0.0;
	double eyeWide = 0.0, squint = 0.0;
	speech::Mouth mouth;
};

// The morphs of a model that do each thing: (morph, how much) pairs.
struct Map {
	using List = std::vector<std::pair<int, double>>;
	List blinkL, blinkR, smile, frown, browUp, browDown, browWorried, eyeWide, squint;
	List jaw, funnel, pucker, stretch, press, lowerLip;   // (ARKit's mouth)
	std::array<List, speech::kVisemes> visemes;           // (Oculus's)
	std::array<List, 5> vowels;                           // (A I U E O: VRM's)
	bool any() const {
		bool v = false;
		for (const List &l : visemes) v = v || !l.empty();
		for (const List &l : vowels) v = v || !l.empty();
		return v || !blinkL.empty() || !blinkR.empty() || !smile.empty() || !frown.empty() || !browUp.empty() || !browDown.empty() || !browWorried.empty() || !jaw.empty();
	}
	bool speaks() const {
		for (const List &l : visemes)
			if (!l.empty()) return true;
		for (const List &l : vowels)
			if (!l.empty()) return true;
		return !jaw.empty();
	}
};

namespace detail {
inline std::string squash(const std::string &s) {   // (lower case letters and digits only)
	std::string out;
	for (char c : s)
		if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return out;
}
// The morph whose name is `want` or ends with it after a separator (squashed, so "blendShape1.eyeBlinkLeft" and "Fcl_EYE_Close" match), -1 for none.
inline int find(const gltf_skin::Rig &rig, const std::vector<std::string> &squashed, const std::string &want) {
	const std::string w = squash(want);
	for (std::size_t i = 0; i < squashed.size(); ++i)
		if (squashed[i] == w) return static_cast<int>(i);
	for (std::size_t i = 0; i < rig.morphs.size(); ++i) {
		const std::string &name = rig.morphs[i];
		const std::size_t at = name.find_last_of("._|:");   // (a prefix: "Face.Fcl_EYE_Close", "blendShape1.jawOpen")
		if (at != std::string::npos && squash(name.substr(at + 1)) == w) return static_cast<int>(i);
	}
	return -1;
}
// The first of the names a model has (each with its share), added to `list`.
inline void firstOf(const gltf_skin::Rig &rig, const std::vector<std::string> &squashed, Map::List &list, std::initializer_list<const char *> names, double share = 1.0) {
	for (const char *n : names)
		if (const int i = find(rig, squashed, n); i >= 0) {
			list.push_back({i, share});
			return;
		}
}
}  // namespace detail

// The face of a rigged model, found once by its morphs' names.
inline Map mapOf(const gltf_skin::Rig &rig) {
	using detail::firstOf;
	Map m;
	std::vector<std::string> sq;
	for (const std::string &n : rig.morphs) sq.push_back(detail::squash(n));
	firstOf(rig, sq, m.blinkL, {"eyeBlinkLeft", "eyeBlink_L", "blinkLeft", "blink_L", "Fcl_EYE_Close_L", "eyeClosedLeft", "eyesClosed", "Fcl_EYE_Close", "blink", "eyeClose", "eyesClose"});
	firstOf(rig, sq, m.blinkR, {"eyeBlinkRight", "eyeBlink_R", "blinkRight", "blink_R", "Fcl_EYE_Close_R", "eyeClosedRight"});
	firstOf(rig, sq, m.smile, {"mouthSmileLeft", "mouthSmile_L", "mouthSmile", "Fcl_MTH_Joy", "Fcl_ALL_Joy", "smile", "happy", "joy"});
	firstOf(rig, sq, m.smile, {"mouthSmileRight", "mouthSmile_R"});
	firstOf(rig, sq, m.frown, {"mouthFrownLeft", "mouthFrown_L", "mouthFrown", "Fcl_MTH_Sorrow", "Fcl_ALL_Sorrow", "frown", "sad", "sorrow"});
	firstOf(rig, sq, m.frown, {"mouthFrownRight", "mouthFrown_R"});
	firstOf(rig, sq, m.browUp, {"browOuterUpLeft", "browOuterUp_L", "Fcl_BRW_Surprised", "browUp", "browsUp", "browRaise"});
	firstOf(rig, sq, m.browUp, {"browOuterUpRight", "browOuterUp_R"});
	firstOf(rig, sq, m.browUp, {"browInnerUp"}, 0.6);
	firstOf(rig, sq, m.browDown, {"browDownLeft", "browDown_L", "Fcl_BRW_Angry", "Fcl_ALL_Angry", "browDown", "angry"});
	firstOf(rig, sq, m.browDown, {"browDownRight", "browDown_R"});
	firstOf(rig, sq, m.browWorried, {"browInnerUp", "Fcl_BRW_Sorrow", "browWorried", "worried"});
	firstOf(rig, sq, m.eyeWide, {"eyeWideLeft", "eyeWide_L", "Fcl_EYE_Surprised", "eyeWide", "surprised", "Fcl_ALL_Surprised"});
	firstOf(rig, sq, m.eyeWide, {"eyeWideRight", "eyeWide_R"});
	firstOf(rig, sq, m.squint, {"eyeSquintLeft", "eyeSquint_L", "squint"});
	firstOf(rig, sq, m.squint, {"eyeSquintRight", "eyeSquint_R"});
	// the mouth: visemes, else the five vowels, else ARKit's jaw and lips, else any open mouth
	for (int v = 0; v < speech::kVisemes; ++v) firstOf(rig, sq, m.visemes[static_cast<std::size_t>(v)], {("viseme_" + std::string(speech::visemeName(v))).c_str()});
	const char *vrm[5][4] = {{"Fcl_MTH_A", "aa", "A", "mouthA"}, {"Fcl_MTH_I", "ih", "I", "mouthI"}, {"Fcl_MTH_U", "ou", "U", "mouthU"}, {"Fcl_MTH_E", "ee", "E", "mouthE"},
							 {"Fcl_MTH_O", "oh", "O", "mouthO"}};
	for (int k = 0; k < 5; ++k) firstOf(rig, sq, m.vowels[static_cast<std::size_t>(k)], {vrm[k][0], vrm[k][1], vrm[k][2], vrm[k][3]});
	firstOf(rig, sq, m.jaw, {"jawOpen", "mouthOpen", "Fcl_MTH_Open", "open", "jaw"});
	firstOf(rig, sq, m.funnel, {"mouthFunnel"});
	firstOf(rig, sq, m.pucker, {"mouthPucker"});
	firstOf(rig, sq, m.stretch, {"mouthStretchLeft", "mouthStretch_L"}, 1.0);
	firstOf(rig, sq, m.stretch, {"mouthStretchRight", "mouthStretch_R"}, 1.0);
	firstOf(rig, sq, m.press, {"mouthPressLeft", "mouthPress_L", "mouthClose"}, 1.0);
	firstOf(rig, sq, m.press, {"mouthPressRight", "mouthPress_R"}, 1.0);
	firstOf(rig, sq, m.lowerLip, {"mouthRollLower"});
	return m;
}

// The controls of a moment's face: a feeling (film_faces.h), a blink (0..1, with the feeling's lowered lids), and the mouth.
inline Controls controlsOf(const faces::Face &f, double blink, const speech::Mouth &mouth) {
	Controls c;
	const double lids = std::clamp(f.lids, 0.0, 0.9);
	c.blinkL = c.blinkR = std::clamp(lids + (1.0 - lids) * blink, 0.0, 1.0);
	c.smile = std::max(0.0, f.smile), c.frown = std::max(0.0, -f.smile);
	c.browUp = std::max(0.0, f.browRaise), c.browDown = std::max(0.0, -f.browTilt) + 0.5 * std::max(0.0, -f.browRaise), c.browWorried = std::max(0.0, f.browTilt);
	c.eyeWide = std::clamp(f.eyeWide / 0.3, 0.0, 1.0), c.squint = std::clamp(std::max(0.0, f.lidTilt) + std::max(0.0, -f.eyeWide / 0.3), 0.0, 1.0);
	c.mouth = mouth;
	c.mouth.open = std::max(c.mouth.open, f.mouthOpen);
	return c;
}

// The morph weights (Rig::morphs, on top of `weights`) for a face's controls.
inline void weightsOf(const Map &m, const Controls &c, std::vector<double> &weights) {
	const auto set = [&](const Map::List &l, double w) {
		for (const auto &[i, share] : l)
			if (static_cast<std::size_t>(i) < weights.size()) weights[static_cast<std::size_t>(i)] = std::clamp(std::max(weights[static_cast<std::size_t>(i)], w * share), 0.0, 1.0);
	};
	set(m.blinkL, c.blinkL);
	set(m.blinkR, c.blinkR);
	set(m.smile, c.smile);
	set(m.frown, c.frown);
	set(m.browUp, c.browUp);
	set(m.browDown, c.browDown);
	set(m.browWorried, c.browWorried);
	set(m.eyeWide, c.eyeWide);
	set(m.squint, c.squint);
	const speech::Mouth &mo = c.mouth;
	bool visemes = false;
	for (const Map::List &l : m.visemes) visemes = visemes || !l.empty();
	if (visemes) {   // (Oculus's fifteen)
		speech::Mouth v = mo;
		speech::visemesFromShape(v);
		for (int k = 1; k < speech::kVisemes; ++k) set(m.visemes[static_cast<std::size_t>(k)], v.viseme[static_cast<std::size_t>(k)]);
		if (mo.open > 0.0 && v.viseme[speech::kAA] == 0.0 && v.viseme[speech::kO] == 0.0) set(m.jaw, mo.open * 0.5);   // (a gasp: the feeling's open mouth)
		return;
	}
	bool vowels = false;
	for (const Map::List &l : m.vowels) vowels = vowels || !l.empty();
	if (vowels) {   // (A I U E O: each viseme as the vowel it looks most like, a consonant at half; the lips' closing a shut mouth)
		speech::Mouth v = mo;
		speech::visemesFromShape(v);
		using namespace speech;
		const auto &x = v.viseme;
		const double a = x[kAA] + 0.5 * (x[kKK] + x[kDD]), i = x[kI] + 0.5 * (x[kSS] + x[kTH] + x[kNN]), u = x[kU] + 0.5 * (x[kCH] + x[kRR]) + 0.3 * x[kFF], e = x[kE], o = x[kO];
		const double vs[5] = {a, i, u, e, o};
		for (int k = 0; k < 5; ++k) set(m.vowels[static_cast<std::size_t>(k)], vs[k] * (1.0 - mo.closed));
		if (m.vowels[0].empty()) set(m.jaw, mo.open);
		return;
	}
	// ARKit's: the jaw opens the mouth, a funnel and a pucker round it, a stretch widens it, a press shuts the lips, a rolled lower lip touches the teeth
	set(m.jaw, mo.open);
	set(m.funnel, 0.7 * mo.round * clamp01(mo.open / 0.5));
	set(m.pucker, 0.8 * mo.round * (1.0 - clamp01(mo.open / 0.5)));
	set(m.stretch, 0.5 * mo.wide);
	set(m.smile, 0.15 * mo.wide);
	set(m.press, mo.closed);
	set(m.lowerLip, 0.7 * mo.teeth);
}

}  // namespace face_morphs
}  // namespace cartoon
