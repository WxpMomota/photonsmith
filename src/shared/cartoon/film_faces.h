#pragma once
// film_faces.h -- what a character feels, on its face (film_file.h `feel NAME EXPRESSION IN0 IN1 [OUT0 OUT1] [amount A]`): an expression eased in and out at film times, as
// a gesture is, made of a few numbers any face can take - how it smiles, where its brows are and how they slant, how open its eyes are and how its lids slant, whether its
// mouth hangs open, whether its ears droop, how its head tips. Each kind of character draws them with its own face (film_kinds.h); two feelings at once add up.

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "rig.h"

namespace cartoon {
namespace faces {

// A face's change from its rest: every number 0 at rest.
struct Face {
	double smile = 0.0;       // -1 (down) .. 1 (up)
	double browRaise = 0.0;   // -1 (down) .. 1 (up)
	double browTilt = 0.0;    // -1 (the angry V: inner ends down) .. 1 (worried: inner ends up)
	double lidTilt = 0.0;     // -1 (sad: slanting up towards the nose) .. 1 (angry: down towards it)
	double eyeWide = 0.0;     // more (surprise) or less open, as a share of the eye's size
	double lids = 0.0;        // 0 .. 1: the upper lids lowered (sleepy, smug)
	double mouthOpen = 0.0;   // 0 .. 1: the mouth hangs open (a gasp)
	double droop = 0.0;       // -1 (ears perked) .. 1 (drooping)
	double headPitch = 0.0;   // degrees (positive looks down)
	double headRoll = 0.0;    // degrees (a tilt of the head)

	Face &add(const Face &o, double w) {
		smile += o.smile * w;
		browRaise += o.browRaise * w;
		browTilt += o.browTilt * w;
		lidTilt += o.lidTilt * w;
		eyeWide += o.eyeWide * w;
		lids += o.lids * w;
		mouthOpen += o.mouthOpen * w;
		droop += o.droop * w;
		headPitch += o.headPitch * w;
		headRoll += o.headRoll * w;
		return *this;
	}
};

// The expressions a `feel` line can name.
inline const std::map<std::string, Face> &expressions() {
	static const std::map<std::string, Face> e = [] {
		std::map<std::string, Face> m;
		//                 smile  raise  tilt   lidTilt wide   lids  open  droop pitch roll
		m["happy"] =      {0.65,  0.25,  0.0,   0.0,   0.05,  0.10, 0.0,  -0.6,  0.0, 4.0};
		m["sad"] =        {-0.70, 0.0,   0.85,  -0.6,  -0.05, 0.25, 0.0,  1.0,  12.0, 0.0};
		m["angry"] =      {-0.55, -0.45, -0.9,  0.75,  0.0,   0.15, 0.0,  0.3,   4.0, 0.0};
		m["surprised"] =  {0.0,   0.90,  0.15,  0.0,   0.35,  0.0,  0.55, -1.0, -6.0, 0.0};
		m["scared"] =     {-0.45, 0.60,  0.75,  -0.3,  0.30,  0.0,  0.25, 0.6,  -4.0, 0.0};
		m["worried"] =    {-0.30, 0.20,  0.70,  -0.3,  0.0,   0.05, 0.0,  0.4,   4.0, 3.0};
		m["sleepy"] =     {0.0,   -0.20, 0.0,   0.0,   -0.05, 0.50, 0.05, 0.6,  10.0, 6.0};
		m["smug"] =       {0.45,  0.0,   -0.25, 0.2,   0.0,   0.35, 0.0,  0.0,  -4.0, 7.0};
		m["thinking"] =   {-0.10, 0.15,  -0.20, 0.0,   0.0,   0.15, 0.0,  0.0,  -6.0, 9.0};
		m["disgusted"] =  {-0.55, -0.20, -0.50, 0.3,   0.0,   0.30, 0.0,  0.2,  -5.0, -5.0};
		return m;
	}();
	return e;
}
inline std::string expressionList() {
	std::string out;
	for (const auto &[name, face] : expressions()) out += (out.empty() ? "" : ", ") + name;
	return out;
}

// A feeling: an expression eased in and out (rig::Gesture's times; its pose is the expression's name), at an amount.
struct Feeling {
	rig::Gesture when;
	double amount = 1.0;
};

// The face at t: every feeling's expression by its weight, added.
inline Face faceAt(const std::vector<Feeling> &feelings, double t) {
	Face f;
	for (const Feeling &k : feelings) {
		const double w = rig::gestureWeight(k.when, t) * k.amount;
		if (w <= 0.0) continue;
		const auto it = expressions().find(k.when.pose);
		if (it != expressions().end()) f.add(it->second, w);
	}
	return f;
}

}  // namespace faces
}  // namespace cartoon
