// film_model2_tests.cpp - the second modeling round: moving models (placements `as ID`, `move`/`turn`/`show` of them and their pieces), models as characters with shape
// keys (film_model_kind.h), booleans on any piece (model_boolean.h), decimation (model_decimate.h), importing and exporting meshes (model_import.h), parameters, variants
// and library models (film_model.h, film_parse_model.h), decals and baked normal maps (model_texture.h), generators of living things and places (model_grow.h), and
// fracture (model_fracture.h).

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../../src/shared/cartoon/cartoon_films.h"
#include "../../src/shared/cartoon/film_model.h"

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

std::filesystem::path dirFor(const std::string &name) { return std::filesystem::temp_directory_path() / ("photonsmith_model2_test_" + name); }
std::string loadFilm(const std::string &name, const std::string &filmText, ff::DataFilm &film) {
	cartoon::film_kinds::install();
	cartoon::film_kinds::sea::install();
	const std::filesystem::path d = dirFor(name);
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
const char *kBot = "model bot\n  piece body colour 0.8 0.3 0.2\n    box 0 0.6 0 size 0.5 0.6 0.3 round 0.03\n  piece head\n    sphere 0 1.1 0 radius 0.2\n"
				   "    key smile\n    lattice 1 1 1 0.05 0.05 0\n  piece eyes colour 1 1 1\n    sphere 0.07 1.15 0.17 radius 0.04\n    mirror x\n"
				   "  piece leg.l\n    capsule 0.12 0.3 0 to 0.12 0.02 0 radius 0.05\n  piece leg.r\n    capsule -0.12 0.3 0 to -0.12 0.02 0 radius 0.05\n"
				   "  piece arm.l\n    capsule 0.3 0.85 0 to 0.42 0.5 0 radius 0.04\n  piece arm.r\n    capsule -0.3 0.85 0 to -0.42 0.5 0 radius 0.04\nend\n";

}  // namespace

// ---- 1: moving models ----
TEST(FilmModel2, PlacedModelsMoveHingeAndHide) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("move", kHead + kBot + "place bot at 0 0 0 as r1\nmove r1 0 0 0 0  4 2 0 0\nturn r1.arm.r about -0.3 0.85 0 axis 0 0 1  0 0  2 120\nshow r1.eyes 0 1\n" + kShot, film), "");
	const std::string a = film.sceneAt(0.5), b = film.sceneAt(3.0);
	EXPECT_NE(a, b) << "it moves";
	EXPECT_EQ(film.sceneAt(3.0), b) << "the same frame twice";
	EXPECT_NE(a.find("model_bot_eyes"), std::string::npos);
	EXPECT_EQ(b.find("model_bot_eyes"), std::string::npos) << "the eyes hidden after 1 s";
	EXPECT_NE(problemWith(std::string(kBot) + "place bot at 0 0 0 as r1\nturn r1.wing about 0 0 0 axis 0 1 0  0 0  1 90"), "") << "no piece called wing";
	EXPECT_NE(problemWith(std::string(kBot) + "place bot at 0 0 0 as r1\nplace bot at 1 0 0 as r1"), "") << "one id each";
}

// ---- 2: models as characters ----
TEST(FilmModel2, AModelIsACharacterWithAPieceRigAndShapeKeys) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("kind", kHead + kBot + "character robo bot at 0 0 0\nwalk robo 1.0 0.6  0 2\ngesture robo \"wave\" 3 3.4 4.5 5\nfeel robo happy 5 5.3 6 6\nsay robo 3 \"Hello!\"\n" + kShot, film), "");
	ASSERT_EQ(film.spec.characters.size(), 1u);
	EXPECT_NE(film.spec.characters[0].kind.find("/bot"), std::string::npos) << "the film's own kind";
	const std::string still = film.sceneAt(0.5), walking = film.sceneAt(1.6), waving = film.sceneAt(4.0), happy = film.sceneAt(5.8);
	EXPECT_NE(still.find("_rig"), std::string::npos) << "its rigid pieces from the cache";
	EXPECT_NE(still, walking);
	EXPECT_NE(walking, waving);
	EXPECT_NE(happy.find("trianglemesh"), std::string::npos) << "the smiling head written out, its key blended in";
	EXPECT_NE(problemWith(std::string(kBot) + "character robo bot at 0 0 0\ngesture robo \"dance\" 1 1.5 2 2.5"), "") << "a pose its library has not";
}

// ---- 3: booleans ----
TEST(FilmModel2, BooleansOnAnyMesh) {
	const cartoon::Mesh block = model::subdivide(model::cageBox(1, 1, 1, 6.0), 2);
	const cartoon::Mesh ball = cartoon::uvSphere(0.45, 32, 16);
	const double v0 = solid(block).volume;
	const model::Report cutOut = solid(model::combined(block, model::transformed(ball, cartoon::translate({0.5, 0.5, 0.5})), model::Op::Cut, 0.0, 0.03));
	EXPECT_TRUE(cutOut.closed());
	EXPECT_LT(cutOut.volume, v0 - 0.03) << "a bite taken out of the corner";
	EXPECT_GT(cutOut.volume, v0 - 0.2);
	const model::MeshField f = model::meshField(ball, 0.03);
	EXPECT_LT(f({0, 0, 0}), 0.0) << "inside";
	EXPECT_GT(f({0, 0.8, 0}), 0.0) << "outside";
	EXPECT_NEAR(f({0, 0.45, 0}), 0.0, 0.03) << "on the surface";
	EXPECT_EQ(problemWith("model m\n  piece t\n    tool\n    sphere 0 0.5 0 radius 0.3\n  piece b\n    cage box 1 1 1 crease 3\n    boolean cut t\nend\nplace m at 0 0 0"), "");
	EXPECT_NE(problemWith("model m\n  piece b\n    cage box 1 1 1\n    boolean slice t\nend"), "");
}

// ---- 4: decimation ----
TEST(FilmModel2, DecimationKeepsTheShapeWithFewerTriangles) {
	const cartoon::Mesh ball = model::subdivide(model::cageBox(1, 1, 1), 4);   // (3072 quads)
	const double v0 = solid(ball).volume;
	const cartoon::Mesh light = model::decimated(ball, 600);
	EXPECT_LE(light.tris.size(), 600u);
	EXPECT_GT(light.tris.size(), 300u);
	const model::Report r = solid(light);
	EXPECT_TRUE(r.closed());
	EXPECT_EQ(r.flipped, 0u);
	EXPECT_NEAR(r.volume, v0, 0.06 * v0) << "its shape kept";
	EXPECT_EQ(problemWith("model m\n  sphere 0 0.5 0 radius 0.5\n  budget 400\nend\nplace m at 0 0 0"), "");
}

// ---- 5: import and export ----
TEST(FilmModel2, MeshesInAndOut) {
	const std::filesystem::path d = dirFor("io");
	std::filesystem::create_directories(d);
	std::vector<model::Exported> pieces{{"box", {0.5, 0.2, 0.1}, model::subdivide(model::cageBox(1, 2, 1, 6.0), 1)}};
	ASSERT_TRUE(model::exportObj((d / "box.obj").string(), pieces));
	EXPECT_TRUE(std::filesystem::exists(d / "box.mtl"));
	std::string error;
	const cartoon::Mesh back = model::importMesh((d / "box.obj").string(), error);
	EXPECT_EQ(error, "");
	EXPECT_EQ(back.tris.size(), pieces[0].mesh.tris.size());
	V3 lo, hi;
	model::boxOf(model::fitted(back, 3.0), lo, hi);
	EXPECT_NEAR(hi.y - lo.y, 3.0, 1e-6);
	EXPECT_NEAR(lo.y, 0.0, 1e-6) << "stood on the ground";
	EXPECT_EQ(problemWith("model m\n  mesh " + (d / "box.obj").generic_string() + " fit 1.5\n  displace noise 0.01 size 0.2\nend\nplace m at 0 0 0"), "");
	EXPECT_NE(problemWith("model m\n  mesh nowhere.obj\nend"), "");
}

// ---- 6: parameters, variants, the library ----
TEST(FilmModel2, ParametersVariantsAndTheLibrary) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("params", kHead + "model post params h 1 r 0.1\n  cylinder 0 $h/2 0 radius $r height $h\nend\nmodel tall from post h 3\nmodel posts from post variants 3 h 0.5 2\n"
										 "place tall at 0 0 0\nscatter posts 6 in -3 -3 3 3\n" + kShot,
							   film),
			  "");
	const auto sizeOf = [&](const std::string &name) {
		for (const model::Model &m : film.spec.models)
			if (m.name == name) return model::sizeOf("t", m);
		return 0.0;
	};
	EXPECT_NEAR(sizeOf("post"), 1.0, 0.1);
	EXPECT_NEAR(sizeOf("tall"), 3.0, 0.15) << "made again, 3 m tall";
	EXPECT_GT(std::count_if(film.spec.models.begin(), film.spec.models.end(), [](const model::Model &m) { return m.name.rfind("posts-", 0) == 0; }), 2);
	EXPECT_NE(problemWith("model post params h 1\n  sphere 0 0 0 radius $w\nend"), "") << "no parameter w";
	// a model from the library, beside the film file
	const std::filesystem::path lib = dirFor("lib");
	std::filesystem::create_directories(lib / "library");
	std::ofstream(lib / "library" / "lamp.model", std::ios::binary) << "model lamp params h 2\n  cylinder 0 $h/2 0 radius 0.05 height $h\n  sphere 0 $h 0 radius 0.15\nend\n";
	ff::DataFilm libFilm;
	ASSERT_EQ(loadFilm("lib", kHead + "model lamp2 from lamp h 3\nplace lamp2 at 0 0 0\n" + kShot, libFilm), "");
	EXPECT_FALSE(libFilm.spec.models[0].parts.empty());
	EXPECT_NE(problemWith("model x from nothing-here\nplace x at 0 0 0"), "");
}

// ---- 7: decals, pictures and baked normals ----
TEST(FilmModel2, DecalsPicturesAndNormalMaps) {
	model::Paint p;
	p.on = true, p.base = {0.3, 0.3, 0.3};
	p.decals.push_back({"no-such-picture.png", {0, 0, 0}, {0, 0, -1}, 1, 1});
	EXPECT_NEAR(model::paintAt(p, {0, 0, 0}, 0.0, {0, 0, 1}).x, 0.3, 1e-9) << "a picture that is not there changes nothing";
	const cartoon::Mesh m = model::subdivide(model::cageBox(1, 1, 1, 2.0), 2);
	const cartoon::Mesh uv = model::boxUnwrapped(m);
	std::vector<float> rgb(64 * 64 * 3);
	model::normalsInto(rgb, 64, uv, {{"bumps", 0.05, 0.2, 1, true}});
	double tipped = 0.0;
	for (std::size_t i = 0; i + 2 < rgb.size(); i += 3) tipped = std::max(tipped, std::abs(rgb[i] - 0.5) + std::abs(rgb[i + 1] - 0.5));
	EXPECT_GT(tipped, 0.05) << "the bumps tip the normals";
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("nmap", kHead + "model m\n  sphere 0 0.5 0 radius 0.5\n  displace scales 0.02 size 0.1 bake\n  paint base 0.5 0.6 0.3 res 64\nend\nplace m at 0 0 0\n" + kShot, film), "");
	EXPECT_NE(film.sceneAt(1.0).find("\"string normalmap\""), std::string::npos);
	EXPECT_EQ(problemWith("model m\n  sphere 0 0 0 radius 1\n  decal logo.png at 0 0 1 toward 0 0 -1 size 0.5 0.5\nend"), "");
}

// ---- 8 and 9: generators ----
TEST(FilmModel2, GeneratorsOfLivingThingsAndPlaces) {
	for (const char *kind : {"tree", "bush", "rocks", "coral", "mushroom", "creature", "street", "room"}) {
		std::string error;
		std::map<std::string, double> opt;
		if (std::string(kind) == "street") opt["houses"] = 2;
		const std::vector<model::Part> parts = model::grow::makeAny(kind, opt, nullptr, error);
		EXPECT_EQ(error, "") << kind;
		EXPECT_FALSE(parts.empty()) << kind;
		std::size_t tris = 0;
		for (const model::Part &p : parts) tris += model::partMesh(p, 4).tris.size();
		EXPECT_GT(tris, 100u) << kind;
	}
	std::string error;
	const std::vector<model::Part> bug = model::grow::creature({{"legs", 6}, {"arms", 0}}, nullptr, 1);
	EXPECT_EQ(std::count_if(bug.begin(), bug.end(), [](const model::Part &p) { return p.name.rfind("leg.", 0) == 0; }), 6) << "pieces named for the rig";
	EXPECT_EQ(problemWith("model t\n  make tree kind pine height 5\nend\nplace t at 0 0 0"), "");
	EXPECT_NE(problemWith("model t\n  make tree kind willow\nend"), "");
}

// ---- 10: fracture ----
TEST(FilmModel2, ThingsBreak) {
	model::Part p;
	p.cell = 0.04;
	model::Prim b;
	b.kind = "box", b.at = {0, 0.5, 0}, b.size = {1, 1, 1};
	p.sdf.steps.push_back({model::Op::Add, 0.0, b});
	model::Shatter s;
	s.pieces = 8, s.seed = 3, s.force = 3.0;
	const std::vector<model::Chunk> chunks = model::chunksOf("model2test", "box", {p}, s, [](const model::Part &q) { return model::partMesh(q); });
	EXPECT_GE(chunks.size(), 5u);
	EXPECT_LE(chunks.size(), 8u);
	const V3 mid{0, 0.5, 0};
	const auto where = [&](double since) { return cartoon::transformPoint(model::chunkAt(chunks[0], 0, mid, s, since), chunks[0].centre); };
	EXPECT_NEAR(cartoon::length(where(0.0) - chunks[0].centre), 0.0, 1e-9);
	EXPECT_GT(cartoon::length(where(0.3) - chunks[0].centre), 0.2) << "flying";
	EXPECT_NEAR(cartoon::length(where(9.0) - where(11.0)), 0.0, 0.02) << "at rest";
	EXPECT_LT(where(10.0).y, 0.5) << "on the ground";
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("break", kHead + "model jar\n  cylinder 0 0.3 0 radius 0.2 height 0.6\n  detail 0.03\nend\nplace jar at 0 0 0 as j\nshatter j at 2 pieces 6\n" + kShot, film), "");
	EXPECT_EQ(film.sceneAt(1.0).find("shard_"), std::string::npos);
	const std::string after = film.sceneAt(2.5);
	EXPECT_NE(after.find("shard_"), std::string::npos) << "in pieces";
	EXPECT_NE(after.find("# silt"), std::string::npos) << "a puff of dust";
	EXPECT_NE(problemWith(std::string("model jar\n  sphere 0 0 0 radius 1\nend\nshatter nobody at 1")), "");
}
