// film_model_tests.cpp - the modeling round: subdivision surfaces (model_subdiv.h), shapes meshed soft or hard (model_sdf.h), the modifier stack (model_modifiers.h),
// sweeps and lofts (model_curves.h), painted textures (model_texture.h), displacement (model_detail.h), kit parts and generators (model_kit.h), model blocks in film
// files with levels of detail and instancing (film_model.h, film_parse_model.h), and the model doctor (model_doctor.h).

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

std::string loadFilm(const std::string &name, const std::string &filmText, ff::DataFilm &film) {
	cartoon::film_kinds::install();
	const std::filesystem::path d = std::filesystem::temp_directory_path() / ("photonsmith_model_test_" + name);
	std::filesystem::remove_all(d);
	std::filesystem::create_directories(d);
	std::ofstream(d / "s.pbrt", std::ios::binary) << kSet;
	std::ofstream(d / (name + ".film"), std::ios::binary) << filmText;
	return film.load((d / (name + ".film")).string());
}
std::string problemWith(const std::string &lines) {
	ff::DataFilm bad;
	return loadFilm("bad", "scene s.pbrt\nlength 4\n" + lines + "\nshot 0 S\n  key 0 1 4  0 1 0  50\n", bad);
}
model::Report solid(const cartoon::Mesh &m) { return model::examine(model::welded(m, 1e-6)); }
std::size_t count(const std::string &text, const std::string &what) {
	std::size_t n = 0;
	for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) ++n;
	return n;
}

}  // namespace

// ---- 1: subdivision ----
TEST(FilmModel, SubdivisionSmoothsACageAndCreasesKeepItsEdges) {
	const model::Cage box = model::cageBox(1, 1, 1);
	EXPECT_EQ(model::problemsOf(box), "");
	const cartoon::Mesh soft = model::subdivide(box, 3);
	EXPECT_EQ(soft.tris.size(), 6u * 64u * 2u);
	const model::Report r = solid(soft);
	EXPECT_TRUE(r.closed());
	EXPECT_EQ(r.flipped, 0u);
	EXPECT_GT(r.volume, 0.0) << "it faces out";
	EXPECT_LT(r.volume, 0.6) << "a pebble, far rounder than the unit box";
	const model::Report sharp = solid(model::subdivide(model::cageBox(1, 1, 1, 6.0), 3));
	EXPECT_GT(sharp.volume, 0.95) << "creased edges: nearly the box";
	const model::Report half = solid(model::subdivide(model::cageBox(1, 1, 1, 1.0), 3));
	EXPECT_GT(half.volume, r.volume);
	EXPECT_LT(half.volume, sharp.volume) << "a semi-sharp crease: between";
	model::Cage bad = box;
	bad.faces.push_back({0, 1, 99});
	EXPECT_NE(model::problemsOf(bad), "");
}

// ---- 2 and 4: shapes, soft and hard ----
TEST(FilmModel, ShapesMeshSoftOrHardWithSharpEdges) {
	model::Part p;
	model::Prim b;
	b.kind = "box", b.size = {1, 0.5, 0.8};
	p.sdf.steps.push_back({model::Op::Add, 0.0, b});
	p.hard = true, p.cell = 0.04;
	const cartoon::Mesh hard = model::partMesh(p);
	const model::Report r = solid(hard);
	EXPECT_TRUE(r.closed());
	EXPECT_NEAR(r.volume, 0.4, 0.02) << "the box's own volume";
	double nearest = 1e9;   // (dual contouring puts a vertex on the corner, where surface nets bevel it by a cell)
	for (const V3 &v : hard.verts) nearest = std::min(nearest, cartoon::length(v - V3{0.5, 0.25, 0.4}));
	EXPECT_LT(nearest, 0.01);
	// a cut takes volume away, a blend adds a fillet
	model::Prim c;
	c.kind = "cylinder", c.size = {0.2, 1.0, 0}, c.turn = {90, 0, 0};
	model::Part q = p;
	q.sdf.steps.push_back({model::Op::Cut, 0.0, c});
	EXPECT_LT(solid(model::partMesh(q)).volume, r.volume - 0.05);
}

// ---- 3: modifiers ----
TEST(FilmModel, TheModifierStack) {
	model::Part p;
	model::Prim b;
	b.kind = "box", b.at = {0, 0.5, 0}, b.size = {0.4, 1.0, 0.4};
	p.sdf.steps.push_back({model::Op::Add, 0.0, b});
	p.cell = 0.04;
	const cartoon::Mesh base = model::partMesh(p);
	const double v0 = solid(base).volume;
	const auto with = [&](const std::string &kind, std::vector<double> v, const std::string &axis = "y") { return model::applied(base, {kind, v, axis}); };
	EXPECT_NEAR(solid(with("twist", {90})).volume, v0, 0.02 * v0) << "a twist keeps the volume";
	EXPECT_LT(solid(with("taper", {0.3})).volume, 0.7 * v0);
	EXPECT_EQ(with("mirror", {}, "x").tris.size(), 2 * base.tris.size());
	EXPECT_EQ(with("array", {3, 1, 0, 0}).tris.size(), 3 * base.tris.size());
	EXPECT_EQ(with("radial", {6}).tris.size(), 6 * base.tris.size());
	V3 lo, hi;
	model::boxOf(with("bend", {90}), lo, hi);
	EXPECT_GT(hi.z, 0.4) << "bent over towards +z";
	model::boxOf(with("lattice", {1, 1, 1, 0.3, 0, 0}), lo, hi);
	EXPECT_GT(hi.x, 0.45);
	const double smoothed = solid(with("smooth", {3})).volume;
	EXPECT_NEAR(smoothed, v0, 0.15 * v0) << "smoothing keeps its size";
	// solidify: an open sheet becomes a closed wall
	cartoon::Mesh sheet;
	sheet.verts = {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}};
	sheet.tris = {{0, 2, 1}, {0, 3, 2}};
	cartoon::sdf::computeNormals(sheet);
	EXPECT_EQ(solid(sheet).holes, 4u);
	const model::Report wall = solid(model::applied(sheet, {"solidify", {0.05}, "y"}));
	EXPECT_TRUE(wall.closed());
	EXPECT_EQ(wall.flipped, 0u);
	EXPECT_NEAR(std::abs(wall.volume), 0.05, 0.005);
}

// ---- 5: curves ----
TEST(FilmModel, SweepsAndLofts) {
	model::Sweep sw;
	sw.profile = model::circleProfile(0.05), sw.path = {{0, 0, 0}, {0.5, 0.3, 0}, {1, 0, 0.4}, {1.5, 0.2, 0.2}};
	const model::Report tube = solid(model::sweepMesh(sw));
	EXPECT_TRUE(tube.closed());
	EXPECT_EQ(tube.flipped, 0u);
	EXPECT_GT(tube.volume, 0.0) << "it faces out";
	model::Loft l;
	l.rings = {{0, 0.5, 0.5}, {1, 0.3, 0.6}, {2, 0.5, 0.2}};
	const model::Report skin = solid(model::loftMesh(l));
	EXPECT_TRUE(skin.closed());
	EXPECT_EQ(skin.flipped, 0u);
	EXPECT_GT(skin.volume, 0.5);
	sw.loop = true;
	EXPECT_EQ(solid(model::sweepMesh(sw)).holes, 0u) << "a closed loop joins up";
	const std::vector<V3> c = model::spline({{0, 0, 0}, {1, 1, 0}, {2, 0, 0}}, 8, false);
	EXPECT_NEAR(cartoon::length(c.front() - V3{0, 0, 0}), 0.0, 1e-9);
	EXPECT_NEAR(cartoon::length(c.back() - V3{2, 0, 0}), 0.0, 1e-9) << "through its points";
}

// ---- 6 and 7: paint and displacement ----
TEST(FilmModel, PaintAndDisplacement) {
	const cartoon::Mesh m = model::subdivide(model::cageBox(1, 1, 1, 2.0), 2);
	std::vector<int> from;
	const cartoon::Mesh uv = model::boxUnwrapped(m, &from);
	ASSERT_EQ(uv.uvs.size(), uv.verts.size());
	ASSERT_EQ(from.size(), uv.verts.size());
	for (const auto &[u, v] : uv.uvs) EXPECT_TRUE(u >= 0 && u <= 1 && v >= 0 && v <= 1);
	model::Paint p;
	p.on = true, p.base = {0.2, 0.2, 0.8}, p.wear = {1, 1, 1}, p.wearAmount = 1.0, p.dirt = {0, 0, 0}, p.dirtAmount = 1.0;
	EXPECT_GT(model::paintAt(p, {0, 0, 0}, 0.9).x, 0.5) << "worn on a bulge";
	EXPECT_LT(model::paintAt(p, {0, 0, 0}, -0.9).z, 0.5) << "dirty in a crease";
	EXPECT_NEAR(model::paintAt(p, {0, 0, 0}, 0.0).z, 0.8, 0.05);
	std::vector<float> rgb(64 * 64 * 3, 0.0f);
	p.res = 64;
	model::paintInto(rgb, uv, std::vector<double>(uv.verts.size(), 0.0), p);
	EXPECT_GT(rgb[(32 * 64 + 10) * 3 + 2], 0.5f) << "the picture is painted";
	for (const std::string &k : model::patternKinds())
		for (double x = 0.0; x < 2.0; x += 0.37) EXPECT_TRUE(std::isfinite(model::patternAt(k, {x, 0.3 * x, 0.1}, 0.2, 1))) << k;
	const cartoon::Mesh lumpy = model::displaced(m, {"bumps", 0.05, 0.2, 1});
	EXPECT_GT(lumpy.tris.size(), m.tris.size()) << "split finely enough to show the pattern";
	EXPECT_TRUE(solid(lumpy).closed());
}

// ---- 8: kit parts and generators ----
TEST(FilmModel, KitPartsAndGenerators) {
	for (const cartoon::Mesh &m : {model::kit::crate(0.6, 0.5, 0.6), model::kit::barrel(0.3, 0.8), model::kit::panel(1, 0.6), model::kit::door(0.9, 2), model::kit::wheel(0.35, 0.2, 5)})
		EXPECT_GT(m.tris.size(), 100u);
	for (const char *kind : {"building", "chair", "table", "shelf", "lamp", "car", "rocket", "boat"}) {
		std::string error;
		const std::vector<model::Part> parts = model::kit::make(kind, {}, nullptr, error);
		EXPECT_EQ(error, "") << kind;
		ASSERT_FALSE(parts.empty()) << kind;
		for (const model::Part &p : parts) EXPECT_GT(model::partMesh(p, 4).tris.size(), 0u) << kind << " " << p.name;
	}
	std::string error;
	model::kit::make("spaceship", {}, nullptr, error);
	EXPECT_NE(error, "");
}

// ---- 2 and 9: model blocks in a film, levels of detail and instancing ----
TEST(FilmModel, AModelBlockPlacedAndInstanced) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("block", "scene s.pbrt\nlength 4\n"
							   "model pot\n  piece body colour 0.8 0.3 0.2\n    cylinder 0 0.2 0 radius 0.15 height 0.4 round 0.02\n    cut cylinder 0 0.4 0 radius 0.12 height 0.5\n"
							   "  piece rim metal colour 0.9 0.8 0.5\n    torus 0 0.4 0 radius 0.14 tube 0.015\nend\n"
							   "place pot at 0 0 0 turn 30\nscatter pot 5 in -3 -3 3 3 seed 2\nshot 0 S\n  key 0 1 4  0 0.3 0  50\n",
							   film),
			  "");
	ASSERT_EQ(film.spec.models.size(), 1u);
	EXPECT_EQ(film.spec.models[0].parts.size(), 2u);
	EXPECT_EQ(film.spec.modelPlaces.size(), 6u);
	const std::string scene = film.sceneAt(1.0);
	EXPECT_NE(scene.find("# models"), std::string::npos);
	EXPECT_GE(count(scene, "ObjectInstance \"model_pot_lod"), 2u) << "a model placed many times is instanced";
	EXPECT_EQ(model::lodFor(1.0, 5.0), 1);
	EXPECT_EQ(model::lodFor(1.0, 50.0), 2);
	EXPECT_EQ(model::lodFor(1.0, 500.0), 4);
	EXPECT_LT(model::partMesh(film.spec.models[0].parts[0], 4).tris.size(), model::partMesh(film.spec.models[0].parts[0], 1).tris.size()) << "a far level is lighter";
	// errors
	EXPECT_NE(problemWith("model pot\n  sphere 0 0 0 radius 1"), "") << "no end";
	EXPECT_NE(problemWith("model pot\n  sphere 0 0 0\nend"), "") << "a sphere's radius";
	EXPECT_NE(problemWith("model pot\n  wobble 3\nend"), "");
	EXPECT_NE(problemWith("model tree\n  sphere 0 0 0 radius 1\nend"), "") << "a kind `place` has";
	EXPECT_NE(problemWith("model pot\n  piece a\nend"), "") << "an empty piece";
	EXPECT_EQ(problemWith("model pot\n  cage box 1 1 1 crease 1\n  levels 2\n  extrude 0 0.5 scale 0.5\n  twist 20\n  displace noise 0.01 size 0.1\nend\nplace pot at 0 0 0"), "");
	EXPECT_EQ(problemWith("model m\n  make car colour 0.2 0.4 0.9\nend\nplace m at 0 0 0"), "");
	EXPECT_EQ(problemWith("model m\n  sweep star 5 0.1 0.05 path 0 0 0 0 1 0 1 1 0 twist 90\n  loft sides 12\n  ring 0 0.2 0.2\n  ring 1 0.1 0.1\nend"), "");
}

// ---- 10: the doctor ----
TEST(FilmModel, TheDoctorFindsWhatIsWrong) {
	cartoon::Mesh tet;
	tet.verts = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
	tet.tris = {{0, 2, 1}, {0, 1, 3}, {0, 3, 2}, {1, 2, 3}};
	model::Report r = model::examine(tet);
	EXPECT_TRUE(r.closed());
	EXPECT_NEAR(r.volume, 1.0 / 6.0, 1e-9);
	EXPECT_TRUE(r.problems().empty());
	cartoon::Mesh inside = tet;
	for (auto &t : inside.tris) std::swap(t[1], t[2]);
	EXPECT_LT(model::examine(inside).volume, 0.0);
	EXPECT_FALSE(model::examine(inside).problems().empty()) << "inside out";
	cartoon::Mesh flipped = tet;
	std::swap(flipped.tris[0][1], flipped.tris[0][2]);
	EXPECT_EQ(model::examine(flipped).flipped, 3u);
	cartoon::Mesh open = tet;
	open.tris.pop_back();
	EXPECT_EQ(model::examine(open).holes, 3u);
	cartoon::Mesh huge = tet;
	for (V3 &v : huge.verts) v = v * 1000.0;
	EXPECT_FALSE(model::examine(huge).problems().empty()) << "the wrong units";
}
