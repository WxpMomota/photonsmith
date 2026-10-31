// film_model3_tests.cpp - the third modeling round: skinned model characters and gaits (model_skin.h, film_model_kind.h), their faces, shapes over time (flex, morph,
// build, vanish, wobble: model_anim.h), mechanisms (spin, gear, piston, roll, route, aim, attach), models in the rigid-body world, fur and draped cloth, covering a piece
// with another model, sculpting strokes, remeshing and closing holes (model_surface.h), regions and texture sets, 3D text (model_text.h) and chart uvs.

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../../src/shared/cartoon/cartoon_films.h"
#include "../../src/shared/cartoon/film_model.h"
#include "../../src/shared/cartoon/model_skin.h"

namespace ff = cartoon::film_file;
namespace model = cartoon::model;
using V3 = cartoon::Vec3;

namespace {

const char *kSet = R"(LookAt 0 2 12  0 1 0  0 1 0
Camera "perspective" "float fov" [ 40 ]
Film "rgb" "integer xresolution" [ 320 ] "integer yresolution" [ 180 ]
Sampler "halton" "integer pixelsamples" [ 16 ]
WorldBegin
LightSource "infinite" "rgb L" [ 0.5 0.5 0.5 ]
)";

std::string loadFilm(const std::string &name, const std::string &filmText, ff::DataFilm &film) {
	cartoon::film_kinds::install();
	cartoon::film_kinds::sea::install();
	const std::filesystem::path d = std::filesystem::temp_directory_path() / ("photonsmith_model3_test_" + name);
	std::filesystem::create_directories(d);
	std::ofstream(d / "s.pbrt", std::ios::binary) << kSet;
	std::ofstream(d / (name + ".film"), std::ios::binary) << filmText;
	return film.load((d / (name + ".film")).string());
}
const std::string kHead = "scene s.pbrt\nlength 6\n";
const std::string kShot = "shot 0 S\n  key 0 1.5 5  0 0.8 0  45\n";
std::string problemWith(const std::string &lines) {
	ff::DataFilm bad;
	return loadFilm("bad", kHead + lines + "\n" + kShot, bad);
}
model::Report solid(const cartoon::Mesh &m) { return model::examine(model::welded(m, 1e-6)); }
const char *kBlob = "model blob\n  piece body colour 0.4 0.7 0.9\n    skinned\n    ellipsoid 0 0.9 0 size 0.6 1.0 0.4\n    blend 0.05 capsule 0.15 0.5 0 to 0.15 0.02 0 radius 0.08\n"
					"    blend 0.05 capsule -0.15 0.5 0 to -0.15 0.02 0 radius 0.08\n    blend 0.05 capsule 0.3 1.1 0 to 0.55 0.75 0 radius 0.06\n"
					"    blend 0.05 capsule -0.3 1.1 0 to -0.55 0.75 0 radius 0.06\n    blend 0.05 sphere 0 1.55 0 radius 0.2\n"
					"  piece brow.l colour 0.1 0.1 0.1\n    box 0.08 1.66 0.18 size 0.08 0.015 0.02\n  piece lid.l colour 0.4 0.7 0.9\n    sphere 0.08 1.6 0.17 radius 0.035\nend\n";

}  // namespace

// ---- 1: skinned characters and gaits ----
TEST(FilmModel3, ASkinBendsWithItsBones) {
	const cartoon::Mesh bar = model::subdivide(model::cageBox(0.2, 2.0, 0.2, 1.0), 3);
	const std::vector<model::Bone> bones{{"spine", {0, -1, 0}, {0, 0, 0}}, {"head", {0, 0, 0}, {0, 1, 0}}};
	const model::Weights w = model::weightsOf(bar, bones);
	for (std::size_t v = 0; v < bar.verts.size(); ++v) EXPECT_NEAR(w.w[v][0] + w.w[v][1] + w.w[v][2], 1.0, 1e-9);
	const cartoon::Mesh bent = model::skinned(bar, w, {cartoon::identity(), cartoon::mul(cartoon::translate({0, 0, 0}), cartoon::rotate({0, 0, 1}, 90.0))});
	V3 lo, hi;
	model::boxOf(bent, lo, hi);
	EXPECT_LT(hi.y, 0.6) << "the top half bent over";
	EXPECT_GT(std::max(std::abs(lo.x), std::abs(hi.x)), 0.6);
	EXPECT_TRUE(solid(bent).closed()) << "one surface still";
	EXPECT_EQ(model::autoSkeleton(bar).size(), 6u);
	EXPECT_EQ(model::gaitPhase(0), model::gaitPhase(3)) << "a trot: front left with back right";
	EXPECT_NE(model::gaitPhase(0), model::gaitPhase(1));
	EXPECT_EQ(model::gaitPhase(0), model::gaitPhase(4)) << "a tripod: left front and back with right middle";
	EXPECT_NE(model::gaitPhase(4), model::gaitPhase(5));
}

// ---- 1 and 2: a skinned character with a face ----
// Bones of your own: only the skin within reach moves, it fades smoothly to nothing (no seam), the rest stays exactly where it is, and a child rides on its parent.
TEST(FilmModel3, ABoneOfYourOwnBendsOnlyTheSkinNearIt) {
	cartoon::Mesh m;
	for (int k = 0; k <= 40; ++k) m.verts.push_back({0.0, k * 0.05, 0.0});
	m.normals.assign(m.verts.size(), V3{1, 0, 0});
	m.tris.push_back({0, 1, 2});
	model::Bone ear;
	ear.name = "ear", ear.a = {0, 1.5, 0}, ear.b = {0, 2.0, 0}, ear.radius = 0.6;
	const model::Weights w = model::weightsOf(m, {ear});
	const cartoon::Mat4 turn = cartoon::mul(cartoon::translate(ear.a), cartoon::rotate({0, 0, 1}, 60.0), cartoon::translate(ear.a * -1.0));
	const cartoon::Mesh posed = model::skinned(m, w, {turn});
	for (std::size_t v = 0; v <= 17; ++v) EXPECT_EQ(posed.verts[v].x, m.verts[v].x) << "out of reach stays at rest, vertex " << v;
	EXPECT_LT(posed.verts[40].x, -0.3) << "the tip follows the bone";
	double largest = 0.0;
	for (std::size_t v = 1; v < m.verts.size(); ++v) largest = std::max(largest, cartoon::length(posed.verts[v] - posed.verts[v - 1]));
	EXPECT_LT(largest, 0.12) << "no tear between neighbours (they were 0.05 apart)";

	model::Bone root, tip;
	root.name = "root", root.a = {0, 0, 0}, root.b = {0, 1, 0};
	tip.name = "tip", tip.a = {0, 1, 0}, tip.b = {0, 2, 0}, tip.parent = "root";
	const cartoon::Mat4 r = cartoon::rotate({0, 0, 1}, 90.0);
	const std::vector<cartoon::Mat4> chained = model::chainedPoses({tip, root}, {cartoon::identity(), r});
	const V3 q = cartoon::transformPoint(chained[0], V3{0, 2, 0});
	EXPECT_NEAR(q.x, -2.0, 1e-9) << "the child rides on its parent's turn";
	EXPECT_NEAR(q.y, 0.0, 1e-9);
}

// In a film: a placed model's own bone posed over time reshapes it, an imported mesh keeps its picture and uvs as it bends, a bone that is not there is said.
TEST(FilmModel3, PoseInAFilmKeepsAnImportedMeshsPicture) {
	const std::filesystem::path d = std::filesystem::temp_directory_path() / "photonsmith_model3_test_pose";
	std::filesystem::create_directories(d);
	std::ofstream(d / "strip.obj", std::ios::binary) << "v -0.1 0 0\nv 0.1 0 0\nv 0.1 1 0\nv -0.1 1 0\nv -0.1 2 0\nv 0.1 2 0\nvt 0 0\nvt 1 0\nvt 1 0.5\nvt 0 0.5\nvt 0 1\nvt 1 1\nf 1/1 2/2 3/3\nf 1/1 3/3 4/4\nf 4/4 3/3 6/6\nf 4/4 6/6 5/5\n";
	std::ofstream(d / "strip.png", std::ios::binary) << "x";
	const std::string text = "model strip\n  piece body\n    mesh " + (d / "strip.obj").generic_string() + "\n    texture " + (d / "strip.png").generic_string() + "\n"
							 "    bone ear from 0 1 0 to 0 2 0 radius 1.2\nend\nplace strip at 0 0 0 as s\npose s ear axis 0 0 1  1 0  2 70\n";
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("pose", kHead + text + kShot, film), "");
	const std::string rest = film.sceneAt(0.5), bent = film.sceneAt(2.5);
	EXPECT_NE(rest, bent) << "the pose reshapes it";
	EXPECT_NE(bent.find("strip.png"), std::string::npos) << "its picture stays on as it bends";
	EXPECT_NE(bent.find("point2 uv"), std::string::npos) << "and its uvs";
	EXPECT_NE(problemWith(text.substr(0, text.find("pose s")) + "pose s nobone 1 0  2 70\n"), "");
}
// Squash and stretch on a block: V along the axis, the width made up so the volume stays; `resize` scales each axis on its own.
TEST(FilmModel3, StretchKeepsTheVolumeAndResizeIsPerAxis) {
	const std::string set = std::string(kSet) + "# @anim ball\nAttributeBegin\n  Shape \"sphere\" \"float radius\" [ 1 ]\nAttributeEnd\n";
	const std::filesystem::path d = std::filesystem::temp_directory_path() / "photonsmith_model3_test_stretch";
	std::filesystem::create_directories(d);
	std::ofstream(d / "s.pbrt", std::ios::binary) << set;
	const auto scaleLine = [&](const std::string &track) {
		std::ofstream(d / "f.film", std::ios::binary) << "scene s.pbrt\nlength 6\nease 0\n" + track + "\n" + kShot;
		ff::DataFilm film;
		EXPECT_EQ(film.load((d / "f.film").string()), "");
		const std::string s = film.sceneAt(3.0);
		const std::size_t at = s.find("Scale ");
		return at == std::string::npos ? std::string() : s.substr(at, s.find('\n', at) - at);
	};
	std::istringstream in(scaleLine("stretch ball about 0 0 0  0 4  6 4").substr(6));
	double x = 0, y = 0, z = 0;
	in >> x >> y >> z;
	EXPECT_NEAR(y, 4.0, 1e-6);
	EXPECT_NEAR(x * y * z, 1.0, 1e-6) << "the volume stays";
	EXPECT_NEAR(x, z, 1e-9);
	std::istringstream in2(scaleLine("resize ball about 0 0 0  0 1 2 3  6 1 2 3").substr(6));
	in2 >> x >> y >> z;
	EXPECT_NEAR(x, 1.0, 1e-6);
	EXPECT_NEAR(y, 2.0, 1e-6);
	EXPECT_NEAR(z, 3.0, 1e-6);
}

// Parents and groups, hop, blink and gaze on plain blocks.
TEST(FilmActing, GroupsParentsHopsBlinksAndGazes) {
	const std::string set = std::string(kSet) + "# @anim a\nAttributeBegin\n  Shape \"sphere\" \"float radius\" [ 1 ]\nAttributeEnd\n# @anim b\nAttributeBegin\n  Shape \"sphere\" \"float radius\" [ 2 ]\nAttributeEnd\n"
							"# @anim c\nAttributeBegin\n  Shape \"sphere\" \"float radius\" [ 3 ]\nAttributeEnd\n";
	const std::filesystem::path d = std::filesystem::temp_directory_path() / "photonsmith_model3_test_acting";
	std::filesystem::create_directories(d);
	std::ofstream(d / "s.pbrt", std::ios::binary) << set;
	const auto load = [&](const std::string &lines, ff::DataFilm &film) {
		std::ofstream(d / "f.film", std::ios::binary) << "scene s.pbrt\nlength 6\nease 0\n" + lines + "\n" + kShot;
		return film.load((d / "f.film").string());
	};
	const auto blockOf = [](const std::string &scene, const std::string &radius) {   // the text of the block of that radius
		const std::size_t end = scene.find("\"float radius\" [ " + radius + " ]");
		return end == std::string::npos ? std::string() : scene.substr(scene.rfind("AttributeBegin", end), end - scene.rfind("AttributeBegin", end));
	};
	{	// a group's move reaches its members; a child rides on its parent, turns included; hidden with it
		ff::DataFilm film;
		ASSERT_EQ(load("group all of a b\nparent c to a\nmove all  0 0 0 0  6 6 0 0\nshow all 0 3\nmove c 0 0 1 0  6 0 1 0", film), "");
		const std::string s = film.sceneAt(1.5), later = film.sceneAt(4.0);
		EXPECT_NE(blockOf(s, "1").find("Translate 1.5 0 0"), std::string::npos) << s.substr(0, 400);
		EXPECT_NE(blockOf(s, "2").find("Translate 1.5 0 0"), std::string::npos);
		EXPECT_NE(blockOf(s, "3").find("Translate 1.5 0 0"), std::string::npos) << "through its parent, to the group";
		EXPECT_NE(blockOf(s, "3").find("Translate 0 1 0"), std::string::npos) << "and its own";
		EXPECT_EQ(blockOf(later, "3"), "") << "hidden with what it rides on";
	}
	{	// a hop: there and back to the ground line by the arc, and its squash; it lands and stays
		ff::DataFilm film;
		ASSERT_EQ(load("hop a 1 1 by 4 0 0 height 2 feet 0 0 0\nblink b about 0 1 0  2 3", film), "");
		EXPECT_NE(blockOf(film.sceneAt(1.5), "1").find("Translate 2 2 0"), std::string::npos) << blockOf(film.sceneAt(1.5), "1");
		EXPECT_NE(blockOf(film.sceneAt(4.0), "1").find("Translate 4 0 0"), std::string::npos);
		EXPECT_NE(blockOf(film.sceneAt(2.08), "2").find("Scale 1 0.1 1"), std::string::npos) << "shut";
		EXPECT_NE(blockOf(film.sceneAt(2.6), "2").find("Scale 1 1 1"), std::string::npos) << "open again";
	}
	{	// a gaze follows what moves: a point turned to by the block's forward, from where the block has been moved to
		ff::DataFilm film;
		ASSERT_EQ(load("move b  0 10 0 0  6 10 0 0\nmove c  0 0 0 0  6 0 0 10\nparent a to c\ngaze a about 0 0 0 forward 0 0 1 at b max 180", film), "");
		const std::string block = blockOf(film.sceneAt(3.0), "1");   // (a rides on c, 5 along z by then, and looks at b at x 10)
		const std::size_t at = block.find("ConcatTransform [");
		ASSERT_NE(at, std::string::npos) << block;
		std::istringstream in(block.substr(at + 17));
		double m[16];
		for (double &x : m) in >> x;
		const V3 wanted = cartoon::normalize(V3{10, 0, -5}), got = cartoon::normalize(V3{m[8], m[9], m[10]});
		EXPECT_NEAR(cartoon::dot(wanted, got), 1.0, 1e-5) << "it looks at b from where it rides";
	}
	ff::DataFilm bad;
	EXPECT_NE(load("parent a to b\nparent b to a", bad), "") << "not onto itself";
	ff::DataFilm bad2;
	EXPECT_NE(load("gaze a at nobody", bad2), "");
}

// A swarm: each particle a function of its number and the time; it flies in, circles, dives and is gone, the kept ones hover; a fountain falls; none of it is a sampled light.
TEST(FilmSwarm, FliesCirclesDivesAndCostsNoLightSampling) {
	namespace sw = cartoon::swarm;
	sw::Swarm s;
	ASSERT_EQ(sw::parse({"swarm", "f", "100", "from", "0", "1", "0", "radius", "20", "during", "1", "2", "to", "0", "5", "0", "radius", "3", "arrive", "2", "orbit", "2", "into", "0", "1", "0", "at", "6", "7", "keep", "20", "settle", "9"}, s), "");
	EXPECT_NE(sw::parse({"swarm", "f", "100", "from", "0", "1", "0"}, *std::make_unique<sw::Swarm>()), "") << "`during` is needed";
	const auto alive = [&](double t) {
		int n = 0;
		V3 p;
		double b = 0.0;
		for (int k = 0; k < s.count; ++k) n += sw::at(s, k, t, nullptr, p, b) ? 1 : 0;
		return n;
	};
	EXPECT_EQ(alive(0.5), 0);
	EXPECT_EQ(alive(5.0), 100);
	EXPECT_EQ(alive(9.0), 20) << "the rest have dived";
	V3 a, c;
	double b1 = 0.0, b2 = 0.0;
	ASSERT_TRUE(sw::at(s, 50, 4.0, nullptr, a, b1));
	EXPECT_LT(cartoon::length(a - V3{0, 5, 0}), 3.5) << "circling the point";
	ASSERT_TRUE(sw::at(s, 50, 4.0, nullptr, c, b2));
	EXPECT_EQ(a.x, c.x) << "a fixed function of the time";
	ASSERT_TRUE(sw::at(s, 5, 12.0, nullptr, a, b1));
	EXPECT_GT(cartoon::length(V3{a.x, 0, a.z}), 0.0);
	EXPECT_LT(cartoon::length(V3{a.x, 0, a.z}), 9.5) << "a kept one hovering within its settle";

	sw::Swarm f;
	ASSERT_EQ(sw::parse({"swarm", "s", "50", "from", "0", "1", "0", "during", "0", "0.1", "velocity", "0", "5", "0", "gravity", "2", "life", "3", "colour", "1", "0.5", "0.1", "bright", "10", "glow", "4"}, f), "");
	ASSERT_TRUE(sw::at(f, 3, 1.0, nullptr, a, b1));
	EXPECT_GT(a.y, 2.0) << "thrown up";
	const std::string text = sw::text(f, 1.0, V3{0, 2, 20}, nullptr);
	EXPECT_NE(text.find("\"bool sampled\" false"), std::string::npos) << "no light to aim at";
	EXPECT_NE(text.find("LightSource \"point\""), std::string::npos) << "its one real light";
	EXPECT_EQ(text.find("\"sphere\""), std::string::npos) << "discs in a mesh, not a shape each";
	const std::string one = sw::text(f, 1.0, V3{0, 2, 20}, nullptr);
	EXPECT_EQ(text, one) << "frames in any order";
	EXPECT_EQ(sw::text(f, -1.0, V3{0, 2, 20}, nullptr), "");
	// a trail follows what it comes from
	sw::Swarm tr;
	ASSERT_EQ(sw::parse({"swarm", "t", "10", "after", "star", "during", "0", "2", "life", "5"}, tr), "");
	ASSERT_TRUE(sw::at(tr, 9, 3.0, [](double when) { return V3{when * 10.0, 0, 0}; }, a, b1));
	EXPECT_GT(a.x, 10.0) << "born where the block was by then";
}

TEST(FilmModel3, ASkinnedCharacterWalksAndFeels) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("skin", kHead + kBlob + "character bo blob at 0 0 0\nwalk bo 1.0 0.5  0 2\nfeel bo surprised 4 4.3 6 6\nsay bo 4.2 \"Oh!\"\n" + kShot, film), "");
	const std::string a = film.sceneAt(0.5), b = film.sceneAt(1.7), c = film.sceneAt(4.6);
	EXPECT_NE(a.find("trianglemesh"), std::string::npos) << "its skin written out, posed";
	EXPECT_NE(a, b) << "it walks";
	EXPECT_NE(b, c);
}

// ---- 3: shapes over time ----
TEST(FilmModel3, ShapesChangeOverTime) {
	EXPECT_NEAR(model::keyedAt({{1, 0}, {3, 10}}, 2.0), 5.0, 1e-9);
	EXPECT_NEAR(model::keyedAt({{1, 0}, {3, 10}}, 9.0), 10.0, 1e-9);
	const cartoon::Mesh box = model::subdivide(model::cageBox(0.5, 1, 0.5, 6.0), 2);
	V3 lo, hi;
	model::boxOf(model::flexed(box, "stretch", 2.0), lo, hi);
	EXPECT_NEAR(hi.y - lo.y, 2.0, 0.01);
	const double v = solid(box).volume;
	EXPECT_NEAR(std::abs(solid(model::flexed(box, "squash", 0.5)).volume), v, 0.02 * v) << "a squash keeps its volume";
	const cartoon::Mesh ball = cartoon::uvSphere(0.4, 24, 12), cube = model::subdivide(model::cageBox(0.8, 0.8, 0.8, 6.0), 2);
	const double vm = std::abs(solid(model::morphed(ball, cube, 0.5, 0.03)).volume);
	EXPECT_GT(vm, 0.2);
	EXPECT_LT(vm, 0.52) << "half way between a ball and a cube";
	EXPECT_LT(model::vanished(box, 0.5, 1).tris.size(), box.tris.size());
	EXPECT_EQ(model::vanished(box, 1.0, 1).tris.size(), 0u);
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("anim", kHead + "model pot\n  cylinder 0 0.3 0 radius 0.2 height 0.6\nend\nmodel cup\n  sphere 0 0.3 0 radius 0.3\nend\n"
										"place pot at 0 0 0 as p1\nflex p1.main stretch 0 1  2 1.6\nwobble p1.main 2\nmove p1 0 0 0 0  2 2 0 0\nplace cup at 1 0 0 as c1\nmorph c1 to pot from 1 to 3\n"
										"place pot at -1 0 0 as p2\nbuild p2 from 0 to 2 pop\nvanish p2 from 4 to 5\n" + kShot,
							  film),
			  "");
	EXPECT_NE(film.sceneAt(1.0), film.sceneAt(1.5));
	EXPECT_NE(film.sceneAt(2.0).find("morph_cup_pot"), std::string::npos) << "melting into the pot";
	EXPECT_NE(problemWith("model pot\n  sphere 0 0 0 radius 1\nend\nplace pot at 0 0 0 as p1\nflex p1.main wiggle 0 1"), "");
	EXPECT_NE(problemWith("model pot\n  sphere 0 0 0 radius 1\nend\nflex nobody.main bend 0 1"), "");
}

// ---- 4 and 5: mechanisms, the rigid-body world ----
TEST(FilmModel3, MechanismsAndPhysics) {
	double heading = 0.0;
	const V3 mid = model::routeAt({{0, 0, 0}, {10, 0, 0}}, 0.5, heading);
	EXPECT_NEAR(mid.x, 5.0, 0.05);
	EXPECT_NEAR(heading, 90.0, 1.0) << "facing along +x";
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("mech", kHead + "model cart\n  piece body\n    box 0 0.4 0 size 0.8 0.3 1.2\n  piece wheel\n    cylinder 0.45 0.2 0.4 radius 0.2 height 0.08 turn 0 0 90\n"
										"  piece fan\n    box 0 0.8 0 size 0.6 0.04 0.1\nend\nmodel ball\n  sphere 0 0.3 0 radius 0.3\nend\n"
										"place cart at 0 0 0 as k\nroute k through 0 0 0  3 0 -2  6 0 0 from 0 to 5\nroll k.wheel about 0.45 0.2 0.4 axis 1 0 0 radius 0.2\n"
										"spin k.fan about 0 0.8 0 axis 0 1 0 rpm 60\nplace ball at 0 2 0 as b\nrigidbody b mass 2 bounce 0.4\ncharacter ann person at 2 0 0\n"
										"place ball at 0 0 0 scale 0.3 as hat\nattach hat to ann head\n" + kShot,
							  film),
			  "");
	const auto where = [&](const std::string &id, double t) {
		for (const model::Placed &pl : film.spec.modelPlaces)
			if (pl.id == id) return cartoon::transformPoint(film.placementFrame(pl, t), V3{0, 0, 0});
		return V3{0, 0, 0};
	};
	EXPECT_GT(cartoon::length(where("k", 4.0) - where("k", 1.0)), 2.0) << "driven along its route";
	EXPECT_LT(where("b", 3.0).y, 1.0) << "the ball has fallen";
	EXPECT_GT(where("hat", 1.0).y, 1.4) << "carried on Ann's head";
	EXPECT_NE(film.mechMatrix("k.fan", 0.25), film.mechMatrix("k.fan", 0.5)) << "the fan turns";
	EXPECT_NE(film.mechMatrix("k.wheel", 3.0), film.mechMatrix("k.wheel", 1.0)) << "the wheel rolls as the cart goes";
	EXPECT_NE(problemWith("model m\n  sphere 0 0 0 radius 1\nend\nplace m at 0 0 0 as a\nspin a about 0 0 0 axis 0 1 0 rpm 10"), "") << "spin names a piece";
}

// ---- 6, 7: fur, cloth, covering ----
TEST(FilmModel3, FurClothAndCovering) {
	model::Fur f;
	f.on = true, f.density = 2000;
	const std::string fur = model::furText(cartoon::uvSphere(0.3, 24, 12), f);
	EXPECT_GT(std::count(fur.begin(), fur.end(), '\n'), 1000) << "about 2000 a square metre over its 1.1 m2";
	EXPECT_NE(fur.find("\"curve\""), std::string::npos);
	const cartoon::Mesh ball = cartoon::uvSphere(0.4, 24, 12);
	const model::MeshField field = model::meshField(model::transformed(ball, cartoon::translate({0, 0.4, 0})), 0.04);
	model::Cloth c;
	c.w = c.d = 1.6, c.at = {0, 1.2, 0}, c.res = 24;
	const cartoon::Mesh sheet = model::draped(c, {&field});
	V3 lo, hi;
	model::boxOf(sheet, lo, hi);
	EXPECT_GT(hi.y, 0.7) << "lying over the ball";
	EXPECT_LT(lo.y, 0.2) << "its corners fallen to the ground";
	EXPECT_EQ(problemWith("model stud\n  sphere 0 0 0 radius 0.02\nend\nmodel brick\n  piece top\n    box 0 0.1 0 size 0.6 0.2 0.3\n  cover top with stud 30 up\nend\nplace brick at 0 0 0"), "");
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("cover", kHead + "model stud\n  sphere 0 0 0 radius 0.02\nend\nmodel brick\n  piece top\n    box 0 0.1 0 size 0.6 0.2 0.3\n  cover top with stud 30 up\nend\nplace brick at 0 0 0\n" + kShot, film), "");
	const model::Model &brick = film.spec.models[1];
	ASSERT_EQ(brick.parts.size(), 2u) << "its covering a piece of its own";
	EXPECT_GT(model::pieceMesh(brick, brick.parts[1], 1).tris.size(), 20u * model::pieceMesh(film.spec.models[0], film.spec.models[0].parts[0], 1).tris.size());
	EXPECT_EQ(problemWith("model a\n  piece cap\n    box 0 0.5 0 size 1 0.1 1\n  piece cloth\n    cloth 1.4 1.4 at 0 1 0 res 20\nend\nplace a at 0 0 0"), "");
}

// ---- 8: sculpting, remeshing, closing ----
TEST(FilmModel3, SculptRemeshAndClose) {
	const cartoon::Mesh ball = model::subdivide(model::cageBox(1, 1, 1), 4);   // (a closed pebble, wound out)
	const double v = solid(ball).volume;
	model::Stroke s;
	s.kind = "inflate", s.radius = 0.2, s.strength = 0.08, s.path = {{0, 0.36, 0}};
	EXPECT_GT(solid(model::sculpted(ball, s)).volume, v + 0.002) << "puffed out";
	s.kind = "crease";
	EXPECT_LT(solid(model::sculpted(ball, s)).volume, v - 0.0002) << "creased in (a narrow groove: little volume)";
	cartoon::Mesh open = ball;
	open.tris.resize(open.tris.size() - 32);   // (a patch cut out)
	EXPECT_GT(solid(open).holes, 0u);
	EXPECT_EQ(solid(model::closedHoles(open)).holes, 0u);
	const model::Report re = solid(model::remeshed(ball, 0.04));
	EXPECT_EQ(re.holes, 0u) << "one closed surface again";
	EXPECT_NEAR(re.volume, v, 0.05 * v);
	EXPECT_EQ(problemWith("model m\n  sphere 0 0.5 0 radius 0.5\n  stroke grab 0.2 0.1 path 0 1 0 toward 0 1 0\n  remesh\n  close\nend\nplace m at 0 0 0"), "");
}

// ---- 9, 10: regions, text, charts ----
TEST(FilmModel3, RegionsTextAndCharts) {
	const cartoon::Mesh box = model::subdivide(model::cageBox(1, 1, 1, 6.0), 2);
	model::Region top;
	top.rule = "facing", top.facing = "up";
	const std::vector<cartoon::Mesh> split = model::splitByRegions(box, {top});
	ASSERT_EQ(split.size(), 2u);
	EXPECT_GT(split[1].tris.size(), 0u);
	EXPECT_EQ(split[0].tris.size() + split[1].tris.size(), box.tris.size());
	std::vector<int> from;
	const cartoon::Mesh uv = model::chartUnwrapped(box, &from);
	ASSERT_EQ(uv.uvs.size(), uv.verts.size());
	for (const auto &[u, w] : uv.uvs) EXPECT_TRUE(u >= -1e-9 && u <= 1 + 1e-9 && w >= -1e-9 && w <= 1 + 1e-9);
	std::string error;
	const cartoon::Mesh words = model::textSolid("HI", 0.5, 0.1, "", error);
	if (error.empty()) {   // (a machine with no Arial says so)
		V3 lo, hi;
		model::boxOf(words, lo, hi);
		EXPECT_NEAR(hi.y - lo.y, 0.5, 0.05) << "a capital's height";
		EXPECT_NEAR(hi.z - lo.z, 0.1, 0.01);
		EXPECT_NEAR(lo.y, 0.0, 1e-6);
	}
	EXPECT_EQ(problemWith("model m\n  piece body colour 0.8 0.8 0.8\n    box 0 0.5 0 size 1 1 1\n    region roof facing up colour 0.8 0.1 0.1 matte\n    unwrap\n    paint base 0.5 0.5 0.5\nend\nplace m at 0 0 0"), "");
	EXPECT_NE(problemWith("model m\n  box 0 0 0 size 1 1 1\n  region r sideways\nend"), "");
}
