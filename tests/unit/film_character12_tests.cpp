// film_character12_tests.cpp - round 12, film characters: skin that light goes into (`skin-light`), real eyes (eye_model.h), a rigged model's face of morph targets
// (gltf_skin.h, face_morphs.h), lip sync from the words (film_lipsync.h), dual-quaternion skinning, hair of strands (film_hair.h), clothes (film_cloth.h skirt, coat,
// scarf), feet on the ground (`footing`), a face acted from a video (face_track.h) and a skeleton fitted to a model that has none (auto_rig.h).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../../src/shared/cartoon/auto_rig.h"
#include "../../src/shared/cartoon/cartoon_films.h"   // (the films themselves, which these tests reach into)
#include "../../src/shared/cartoon/eye_model.h"
#include "../../src/shared/cartoon/face_morphs.h"
#include "../../src/shared/cartoon/face_track.h"
#include "../../src/shared/cartoon/film_cache.h"
#include "../../src/shared/cartoon/film_lipsync.h"

namespace ff = cartoon::film_file;
namespace sp = cartoon::speech;
using V3 = cartoon::Vec3;

namespace {

const char *kGround = R"(# a floor, and a step 22 cm high behind z = 0.05
LookAt 0 1.2 4  0 0.4 0  0 1 0
Camera "perspective" "float fov" [ 40 ]
Film "rgb" "integer xresolution" [ 320 ] "integer yresolution" [ 180 ]
WorldBegin
LightSource "distant" "point3 from" [ 3 6 4 ] "point3 to" [ 0 0 0 ] "rgb L" [ 2 2 2 ]
AttributeBegin
  Material "diffuse" "rgb reflectance" [ 0.5 0.5 0.5 ]
  Shape "trianglemesh" "integer indices" [ 0 1 2  0 2 3 ] "point3 P" [ -20 0 -20   20 0 -20   20 0 20   -20 0 20 ]
AttributeEnd
AttributeBegin
  Material "diffuse" "rgb reflectance" [ 0.5 0.5 0.6 ]
  Shape "trianglemesh" "integer indices" [ 0 1 2 0 2 3  4 6 5 4 7 6  0 4 5 0 5 1  3 2 6 3 6 7  0 3 7 0 7 4  1 5 6 1 6 2 ] "point3 P" [ -2 0 -1  2 0 -1  2 0.22 -1  -2 0.22 -1   -2 0 0.05  2 0 0.05  2 0.22 0.05  -2 0.22 0.05 ]
AttributeEnd
)";

std::filesystem::path dirFor(const std::string &name) {
	const std::filesystem::path d = std::filesystem::temp_directory_path() / ("photonsmith_char12_test_" + name);
	std::filesystem::remove_all(d);
	std::filesystem::create_directories(d);
	return d;
}
std::string loadFilm(const std::string &name, const std::string &filmText, ff::DataFilm &film, const std::filesystem::path &dir = {}) {
	cartoon::film_kinds::install();
	const std::filesystem::path d = dir.empty() ? dirFor(name) : dir;
	std::ofstream(d / "s.pbrt", std::ios::binary) << kGround;
	std::ofstream(d / (name + ".film"), std::ios::binary) << filmText;
	return film.load((d / (name + ".film")).string());
}
std::string problemWith(const std::string &lines) {
	ff::DataFilm bad;
	return loadFilm("bad", "scene s.pbrt\nlength 4\n" + lines + "\n", bad);
}
std::size_t count(const std::string &text, const std::string &what) {
	std::size_t n = 0;
	for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) ++n;
	return n;
}
const gltf_skin::Rig &mira() {
	static const gltf_skin::Rig rig = [] {
		const std::string file = cartoon::film_cache::asset("library/mira.glb");
		std::ifstream in(file, std::ios::binary);
		const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		gltf_skin::Result r = gltf_skin::load(bytes);
		EXPECT_TRUE(r.ok) << r.error;
		return r.rig;
	}();
	return rig;
}

}  // namespace

// ---- skin and eyes ----
TEST(FilmCharacter12Look, SkinLightMakesTheSkinSubsurfaceAndRealEyesAClearCornea) {
	ff::DataFilm plain, lit;
	ASSERT_EQ(loadFilm("plain", "scene s.pbrt\nlength 2\ncharacter ann person at 0 0 0\n", plain), "");
	ASSERT_EQ(loadFilm("lit", "scene s.pbrt\nlength 2\ncharacter ann person at 0 0 0\nskin-light 1.5\neyes real\n", lit), "");
	const std::string a = plain.sceneAt(0.5), b = lit.sceneAt(0.5);
	EXPECT_GT(count(b, "\"subsurface\""), count(a, "\"subsurface\"") + 1);
	EXPECT_EQ(count(a, "\"float eta\" [ 1.3760 ]"), 0u);
	EXPECT_GT(count(b, "\"float eta\" [ 1.3760 ]"), 0u) << "the cornea";
	EXPECT_GT(count(b, "\"bool shadow\" false"), 0u) << "the cornea and the white cast no shadow on the iris";
	EXPECT_NE(problemWith("character ann person\nskin-light 9"), "");
	EXPECT_NE(problemWith("character ann person\neyes glass"), "");
}
TEST(FilmCharacter12Look, PupilsOpenInTheDarkAndWithFearAndNarrowWithAnger) {
	namespace eye = cartoon::eye;
	EXPECT_GT(eye::pupilOf(1.0, 0.0), eye::pupilOf(0.0, 0.0));
	EXPECT_GT(eye::pupilOf(0.3, 1.0), eye::pupilOf(0.3, 0.0));
	EXPECT_LT(eye::pupilOf(0.3, -1.0), eye::pupilOf(0.3, 0.0));
	EXPECT_GE(eye::pupilOf(0.0, -1.0), 0.22);
	EXPECT_LE(eye::pupilOf(1.0, 1.0), 0.58);
	// the iris sits deeper than a ray's offset on a person's eye (2.8 cm across): a gap thinner than 1 mm and the GPU's rays skip past it
	EXPECT_GT((eye::kApex - eye::kIrisY) * 0.028, 0.006);
	EXPECT_GT((1.0 - eye::kWhite) * 0.028, 0.0015);
}

// ---- lip sync ----
TEST(FilmCharacter12LipSync, WordsAreReadIntoTheMouthShapesOfTheirSounds) {
	namespace ls = sp::lipsync;
	EXPECT_EQ(ls::visemesOf("map"), (std::vector<int>{sp::kPP, sp::kAA, sp::kPP}));
	EXPECT_EQ(ls::visemesOf("the"), (std::vector<int>{sp::kTH, sp::kAA}));
	const std::vector<int> fish = ls::visemesOf("fish"), time = ls::visemesOf("time"), knee = ls::visemesOf("knee"), face = ls::visemesOf("face");
	EXPECT_EQ(fish.front(), sp::kFF);
	EXPECT_EQ(fish.back(), sp::kCH) << "sh";
	EXPECT_EQ(time, (std::vector<int>{sp::kDD, sp::kAA, sp::kI, sp::kPP})) << "a magic e: the i says its name, the e is silent";
	EXPECT_EQ(knee.front(), sp::kNN) << "a silent k";
	EXPECT_EQ(face.back(), sp::kSS) << "a soft c before a silent e";
}
TEST(FilmCharacter12LipSync, TheLipsMeetForAnMAndTheShapesLeadTheSound) {
	namespace ls = sp::lipsync;
	sp::Line l;
	l.text = "mama";
	const std::vector<sp::Syllable> y = sp::syllablesOf(l);
	const std::vector<ls::Phone> ph = ls::phonesOf(l, y);
	ASSERT_EQ(ph.size(), 4u);
	EXPECT_EQ(ph[2].viseme, sp::kPP);
	const double mid = 0.5 * (ph[2].t0 + ph[2].t1) - 0.035;   // (the mouth gets there 35 ms before the sound)
	const sp::Mouth m = ls::mouthAt(ph, mid), a = ls::mouthAt(ph, 0.5 * (ph[1].t0 + ph[1].t1) - 0.035);
	EXPECT_GT(m.closed, 0.6) << "the lips meet for the m";
	EXPECT_LT(m.open, 0.15);
	EXPECT_GT(a.open, 0.5) << "and open for the a";
	EXPECT_LT(ls::mouthAt(ph, ph.back().t1 + 1.0).open, 0.02) << "at rest after the line";
	// a recording's three numbers become visemes for a face drawn with them
	sp::Mouth rec;
	rec.open = 0.9;
	sp::visemesFromShape(rec);
	EXPECT_GT(rec.viseme[sp::kAA], 0.5);
}

// ---- a rigged model's face and skin ----
TEST(FilmCharacter12Rigged, MorphTargetsAreReadNamedAndAddedBeforeTheSkin) {
	const gltf_skin::Rig &rig = mira();
	ASSERT_EQ(rig.morphs.size(), 21u);
	const int jaw = rig.morphNamed("JAWOPEN");
	ASSERT_GE(jaw, 0);
	std::vector<double> open(rig.morphs.size(), 0.0);
	open[static_cast<std::size_t>(jaw)] = 1.0;
	double lo0[3], hi0[3], lo1[3], hi1[3];
	const gltf_skin::Pose rest = gltf_skin::restPose(rig);
	const auto mouthLow = [&](const std::vector<double> *w) {   // (the lowest point of the mouth's material)
		double low = 1e30;
		for (const gltf_skin::Posed &p : gltf_skin::posed(rig, rest, w))
			if (p.material >= 0 && rig.materials[static_cast<std::size_t>(p.material)].name == "mouth")
				for (std::size_t i = 1; i < p.positions.size(); i += 3) low = std::min(low, p.positions[i]);
		return low;
	};
	EXPECT_LT(mouthLow(&open), mouthLow(nullptr) - 0.015) << "the jaw opens the mouth";
	gltf_skin::boundsOf(gltf_skin::posed(rig, rest), lo0, hi0);
	gltf_skin::boundsOf(gltf_skin::posed(rig, rest, &open), lo1, hi1);
	EXPECT_NEAR(hi0[1], hi1[1], 1e-9) << "the rest of her is where it was";
}
TEST(FilmCharacter12Rigged, TheFaceIsFoundByItsNamesAndDrivenByTheMouthAndTheFeelings) {
	namespace fm = cartoon::face_morphs;
	const gltf_skin::Rig &rig = mira();
	const fm::Map m = fm::mapOf(rig);
	EXPECT_TRUE(m.any() && m.speaks());
	EXPECT_FALSE(m.blinkL.empty() || m.blinkR.empty() || m.smile.empty() || m.jaw.empty() || m.funnel.empty());
	cartoon::faces::Face happy;
	happy.smile = 1.0;
	sp::Mouth say;
	say.open = 0.8;
	std::vector<double> w(rig.morphs.size(), 0.0);
	fm::weightsOf(m, fm::controlsOf(happy, 1.0, say), w);
	EXPECT_NEAR(w[static_cast<std::size_t>(rig.morphNamed("jawOpen"))], 0.8, 1e-9);
	EXPECT_NEAR(w[static_cast<std::size_t>(rig.morphNamed("eyeBlinkLeft"))], 1.0, 1e-9);
	EXPECT_GT(w[static_cast<std::size_t>(rig.morphNamed("mouthSmileRight"))], 0.9);
	// other tools' names: a prefix, VRoid's, Oculus's
	gltf_skin::Rig other;
	other.morphs = {"blendShape1.eyeBlinkLeft", "Fcl_MTH_A", "viseme_aa", "viseme_PP"};
	const fm::Map o = fm::mapOf(other);
	EXPECT_FALSE(o.blinkL.empty());
	EXPECT_FALSE(o.vowels[0].empty());
	EXPECT_FALSE(o.visemes[sp::kAA].empty() || o.visemes[sp::kPP].empty());
}
TEST(FilmCharacter12Rigged, DualQuaternionsKeepATwistedElbowsVolume) {
	const gltf_skin::Rig &rig = mira();
	const int twist = rig.clipNamed("twist");
	ASSERT_GE(twist, 0);
	gltf_skin::Pose pose = gltf_skin::restPose(rig);
	gltf_skin::apply(rig, rig.clips[static_cast<std::size_t>(twist)], 1.0, pose, 1.0, false);
	const std::vector<gltf_skin::Posed> rest = gltf_skin::posed(rig, gltf_skin::restPose(rig)), linear = gltf_skin::posed(rig, pose), dq = gltf_skin::posed(rig, pose, nullptr, true);
	int elbow = -1;
	for (std::size_t n = 0; n < rig.nodes.size(); ++n)
		if (rig.nodes[n].name == "elbow.L") elbow = static_cast<int>(n);
	ASSERT_GE(elbow, 0);
	const auto at = [&](const gltf_skin::Pose &p) {
		const auto m = gltf_skin::worldOf(rig, p)[static_cast<std::size_t>(elbow)];
		return V3{m[12], m[13], m[14]};
	};
	const V3 e0 = at(gltf_skin::restPose(rig)), e1 = at(pose);
	double sumL = 0.0, sumD = 0.0, sumR = 0.0;
	int n = 0;
	for (std::size_t k = 0; k < rest.size(); ++k)
		for (std::size_t i = 0; i + 2 < rest[k].positions.size(); i += 3) {
			const V3 r{rest[k].positions[i], rest[k].positions[i + 1], rest[k].positions[i + 2]};
			if (cartoon::length(r - e0) > 0.05) continue;   // (the skin round her left elbow)
			sumR += cartoon::length(r - e0);
			sumL += cartoon::length(V3{linear[k].positions[i], linear[k].positions[i + 1], linear[k].positions[i + 2]} - e1);
			sumD += cartoon::length(V3{dq[k].positions[i], dq[k].positions[i + 1], dq[k].positions[i + 2]} - e1);
			++n;
		}
	ASSERT_GT(n, 20);
	EXPECT_LT(sumL, 0.8 * sumR) << "a linear skin collapses round a twisted joint";
	EXPECT_GT(sumD, 0.9 * sumR) << "a dual-quaternion one keeps its volume";
}
TEST(FilmCharacter12Rigged, AFilmDrivesARiggedFaceWithItsLinesAndFeelings) {
	ff::DataFilm film;
	const std::string glb = cartoon::film_cache::asset("library/mira.glb");
	ASSERT_EQ(loadFilm("face", "scene s.pbrt\nlength 4\nrigged mira " + glb + "\ncharacter mira mira at 0 0 0\nfeel mira happy 0.5 0.8\nsay mira 1 \"Ah, ah, ah.\"\n", film), "");
	EXPECT_NE(film.sceneAt(0.2), film.sceneAt(1.1)) << "her face moves as she speaks";
	EXPECT_NE(problemWith("rigged mira " + glb + " skinning soft"), "");
}

// ---- hair ----
TEST(FilmCharacter12Hair, StrandsGrowOnTheHeadInPlaceOfTheSculptedHair) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("hair", "scene s.pbrt\nlength 4\ncharacter ann person at 0 0 0 hair ginger\nhair ann long density 0.25\ngesture ann \"shake head\" 1 1.2 2 2.2\n", film), "");
	const std::string a = film.sceneAt(0.5), b = film.sceneAt(1.5);
	EXPECT_GT(count(a, "Shape \"curve\""), 2000u);
	EXPECT_EQ(count(a, "Material \"hair\""), 1u);
	EXPECT_NE(a, b) << "it moves with the head";
	EXPECT_NE(problemWith("character tess turtle\nhair tess short").find("takes no hair"), std::string::npos);
	EXPECT_NE(problemWith("character ann person\nhair ann dreadlocks"), "");
}
TEST(FilmCharacter12Hair, TipsTrailWhereTheHeadWasAndLieOutsideTheSkull) {
	namespace hr = cartoon::hair;
	hr::Groom g;
	g.style = "bob", g.density = 0.05, g.frizz = 0.0;
	hr::Scalp s{cartoon::identity(), {0.0, 0.0, 0.0}, {0.12, 0.13, 0.12}};
	const std::string still = hr::text(g, s, s.head, {}, {0.3, 0.2, 0.1}, 1.0, 7), turned = hr::text(g, s, cartoon::translate(V3{0.1, 0.0, 0.0}), {}, {0.3, 0.2, 0.1}, 1.0, 7);
	EXPECT_NE(still, turned);
	// every point of every strand is outside the skull (the curves' points: after "point3 P" [)
	std::size_t at = 0;
	int inside = 0, points = 0;
	while ((at = still.find("\"point3 P\" [", at)) != std::string::npos) {
		at += 12;
		double v[3];
		for (int k = 0; k < 30; k += 3) {
			char *end = nullptr;
			v[0] = std::strtod(still.c_str() + at, &end), v[1] = std::strtod(end, &end), v[2] = std::strtod(end, &end);
			at = static_cast<std::size_t>(end - still.c_str());
			if (k == 0) continue;   // (the root is just under the skin)
			const double e = std::pow(v[0] / 0.12, 2) + std::pow(v[1] / 0.13, 2) + std::pow(v[2] / 0.12, 2);
			inside += e < 0.999 ? 1 : 0;
			++points;
		}
	}
	EXPECT_GT(points, 100);
	EXPECT_EQ(inside, 0);
}

// ---- clothes ----
TEST(FilmCharacter12Clothes, ASkirtHangsRoundTheWaistAndSwingsWithTheLegs) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("skirt", "scene s.pbrt\nlength 4\ncharacter bea person at 0 0 -1\nwalk bea 0.2 1.0  0 -1  0 2\ncloth skirt on bea\ncloth scarf on bea\ncloth coat on bea\n", film), "");
	const std::string a = film.sceneAt(1.0), b = film.sceneAt(1.4);
	EXPECT_EQ(count(a, "# cloth: a skirt"), 1u);
	EXPECT_EQ(count(a, "# cloth: a scarf"), 1u);
	EXPECT_EQ(count(a, "# cloth: a coat"), 1u);
	EXPECT_EQ(count(a, "nan"), 0u);
	EXPECT_NE(a, b);
	EXPECT_NE(problemWith("cloth skirt at 0 1 0"), "");
	EXPECT_NE(problemWith("character bea person\ncloth skirt on bea flare 9"), "");
}

// ---- feet on the ground ----
TEST(FilmCharacter12Footing, OnAStepOneFootStandsOnItAndTheOtherOnTheFloor) {
	ff::DataFilm flat, ground;
	const std::string body = "scene s.pbrt\nlength 3\ncharacter ann person at 0.4 0 0.05 facing 90\n";
	ASSERT_EQ(loadFilm("flat", body, flat), "");
	ASSERT_EQ(loadFilm("ground", body + "footing ground\n", ground), "");
	const auto feet = [](const ff::DataFilm &f) {
		const ff::Character &c = f.spec.characters[0];
		ff::CharacterMoment m = f.actingMoment(c, 1.0);
		f.lockFeet(c, 1.0, m);
		f.footOnGround(c, m);
		std::vector<double> ys;
		for (const cartoon::Foot &x : ff::characterKinds().at(c.kind).feet(m)) ys.push_back(x.at.y);
		return ys;
	};
	const std::vector<double> a = feet(flat), b = feet(ground);
	ASSERT_EQ(b.size(), 2u);
	EXPECT_NEAR(a[0], a[1], 1e-6) << "flat: both feet at its own height";
	EXPECT_GT(std::abs(b[0] - b[1]), 0.15) << "one foot up on the step, one on the floor";
	EXPECT_NE(problemWith("character ann person\nfooting ann tiptoe"), "");
}

// ---- a face acted from a video ----
TEST(FilmCharacter12Act, AFaceTrackIsReadBlendedAndActed) {
	namespace ft = cartoon::face_track;
	ft::Track t;
	ASSERT_EQ(ft::parse("# a face\nnames jawOpen mouthSmileLeft mouthSmileRight eyeBlinkLeft eyeBlinkRight\n0 0 0 0 0 0\n1 1 0.8 0.8 1 1\n", t), "");
	const ft::Shapes half = ft::at(t, 0.5);
	EXPECT_NEAR(ft::shape(half, "jawOpen"), 0.5, 1e-9);
	EXPECT_NEAR(ft::shape(ft::at(t, 5.0), "jawOpen"), 1.0, 1e-9) << "held at its end";
	EXPECT_GT(ft::faceOf(ft::at(t, 1.0)).smile, 0.7);
	EXPECT_GT(ft::faceOf(ft::at(t, 1.0)).lids, 0.9);
	EXPECT_GT(ft::mouthOf(ft::at(t, 1.0)).open, 0.9);
	ft::Track bad, worse;
	EXPECT_NE(ft::parse("names a b\n0 1\n", bad), "");
	EXPECT_NE(ft::parse("0 1 2\n", worse), "");
	// in a film: the person's face takes it
	const std::filesystem::path d = dirFor("act");
	std::ofstream(d / "me.face", std::ios::binary) << "names jawOpen eyeBlinkLeft eyeBlinkRight\n0 0 0 0\n0.5 1 1 1\n2 1 1 1\n";
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("act", "scene s.pbrt\nlength 4\ncharacter ann person at 0 0 0\nact ann me.face at 1\n", film, d), "");
	EXPECT_NE(film.sceneAt(0.5), film.sceneAt(2.0));
	EXPECT_NE(problemWith("character ann person\nact ann nowhere.face at 1").find("cannot be read"), std::string::npos);
}

// ---- a skeleton for a model with none ----
namespace {
std::vector<cartoon::auto_rig::V3> figure(bool tPose) {   // (points of a stick figure 1.8 m tall: a body, a head, two legs, two arms out or down)
	std::vector<cartoon::auto_rig::V3> p;
	const auto line = [&](double x0, double y0, double x1, double y1, double r) {
		for (int i = 0; i <= 40; ++i)
			for (int k = 0; k < 12; ++k) {
				const double u = i / 40.0, a = 2.0 * 3.14159265 * k / 12.0;
				p.push_back({x0 + (x1 - x0) * u + r * std::cos(a) * (y1 != y0 ? 1.0 : 0.0), y0 + (y1 - y0) * u + r * std::cos(a) * (y1 != y0 ? 0.0 : 1.0), r * std::sin(a)});
			}
	};
	line(0.0, 0.95, 0.0, 1.48, 0.15), line(0.0, 1.50, 0.0, 1.80, 0.11);
	for (double s : {1.0, -1.0}) {
		line(0.10 * s, 0.95, 0.10 * s, 0.0, 0.07);
		if (tPose) line(0.14 * s, 1.40, 0.80 * s, 1.40, 0.05);
		else line(0.22 * s, 1.40, 0.24 * s, 0.82, 0.045);
	}
	return p;
}
}  // namespace
TEST(FilmCharacter12AutoRig, ASkeletonIsFittedToAFigureInATPoseOrWithItsArmsDown) {
	namespace ar = cartoon::auto_rig;
	const ar::Skeleton t = ar::fit(figure(true)), down = ar::fit(figure(false));
	EXPECT_TRUE(t.tPose);
	EXPECT_FALSE(down.tPose);
	ASSERT_EQ(t.joints.size(), 17u);
	EXPECT_GT(t.joints[static_cast<std::size_t>(t.find("LeftHand"))].tip.x, 0.7) << "the hand at the end of the arm";
	EXPECT_LT(t.joints[static_cast<std::size_t>(t.find("RightHand"))].tip.x, -0.7);
	EXPECT_NEAR(t.joints[static_cast<std::size_t>(t.find("LeftUpLeg"))].at.x, 0.10, 0.03) << "the legs where they are";
	const double handTip = down.joints[static_cast<std::size_t>(down.find("LeftHand"))].tip.y;
	EXPECT_NEAR(handTip, 0.80, 0.06) << "a hanging arm as long as it is";
	// a point on the left arm goes to the left arm's bones only; a point on the chest's side to the chest
	std::vector<int> j;
	std::vector<double> w;
	ar::weigh(t, {{0.6, 1.40, 0.05}, {0.15, 1.30, 0.0}}, j, w);
	for (int k = 0; k < 4; ++k)
		if (w[static_cast<std::size_t>(k)] > 0.0) EXPECT_EQ(t.joints[static_cast<std::size_t>(j[static_cast<std::size_t>(k)])].side, 1);
	EXPECT_EQ(t.joints[static_cast<std::size_t>(j[0])].name.find("Arm") != std::string::npos, true);
	EXPECT_EQ(t.joints[static_cast<std::size_t>(j[4])].name.rfind("Spine", 0), 0u);
}
TEST(FilmCharacter12AutoRig, AnUnriggedModelIsRiggedWithClipsAndWalksInAFilm) {
	namespace ar = cartoon::auto_rig;
	gltf_skin::Rig rig;
	gltf_skin::Piece piece;
	for (const ar::V3 &p : figure(true)) piece.positions.insert(piece.positions.end(), {p.x, p.y, p.z}), piece.normals.insert(piece.normals.end(), {0.0, 1.0, 0.0});
	for (std::size_t i = 0; i + 2 < piece.positions.size() / 3; i += 3) piece.indices.insert(piece.indices.end(), {static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1), static_cast<uint32_t>(i + 2)});
	piece.node = 0;
	rig.nodes.push_back({});
	rig.order.push_back(0);
	rig.pieces.push_back(piece);
	ASSERT_EQ(ar::rig(rig), "");
	EXPECT_EQ(rig.skins.size(), 1u);
	for (const char *c : {"idle", "walk", "wave", "nod"}) EXPECT_GE(rig.clipNamed(c), 0) << c;
	// walking moves its legs
	double lo0[3], hi0[3], lo1[3], hi1[3];
	gltf_skin::Pose walk = gltf_skin::restPose(rig);
	gltf_skin::apply(rig, rig.clips[static_cast<std::size_t>(rig.clipNamed("walk"))], 0.25, walk);
	gltf_skin::boundsOf(gltf_skin::posed(rig, gltf_skin::restPose(rig)), lo0, hi0);
	gltf_skin::boundsOf(gltf_skin::posed(rig, walk), lo1, hi1);
	EXPECT_GT(hi1[2] - lo1[2], hi0[2] - lo0[2] + 0.1) << "a stride";
	// an .obj in a film is rigged as it is loaded
	const std::filesystem::path d = dirFor("autorig");
	{
		std::ofstream obj(d / "fig.obj");
		for (const ar::V3 &p : figure(false)) obj << "v " << p.x << " " << p.y << " " << p.z << "\n";
		for (std::size_t i = 1; i + 2 <= figure(false).size(); i += 3) obj << "f " << i << " " << i + 1 << " " << i + 2 << "\n";
	}
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("autorig", "scene s.pbrt\nlength 4\nrigged fig fig.obj\ncharacter f fig at 0 0 0\nwalk f 1 1  0 0  0 2\ngesture f \"wave\" 0.2 0.4 0.8 1\n", film, d), "");
	EXPECT_NE(film.sceneAt(0.1), film.sceneAt(0.6));
}
