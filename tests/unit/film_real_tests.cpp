// film_real_tests.cpp -- what makes plants and animals look real: the land's cracks, trees that stay trees into the distance, leaves.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../../src/shared/cartoon/cartoon_films.h"   // (the films themselves, which these tests reach into)

namespace forest = cartoon::forest;
namespace terrain = cartoon::terrain;
using cartoon::Mesh;
using V3 = cartoon::Vec3;

namespace {

// A grid of `n` x `n` cells a metre across, its height at each corner from `height`, as terrain::surfaceMesh lays one out.
template <class Height>
Mesh grid(int n, Height height) {
	Mesh m;
	for (int j = 0; j <= n; ++j)
		for (int i = 0; i <= n; ++i) {
			m.verts.push_back({static_cast<double>(i), height(i, j), static_cast<double>(j)});
			m.uvs.push_back({0.0, 0.0});
		}
	const int nx = n + 1;
	for (int j = 0; j < n; ++j)
		for (int i = 0; i < n; ++i) {
			const int a = j * nx + i;
			m.tris.push_back({a, a + nx, a + 1});
			m.tris.push_back({a + 1, a + nx, a + nx + 1});
		}
	terrain::landNormals(m);
	return m;
}

double leafArea(const Mesh &m, std::size_t fromTri = 0) {
	double area = 0.0;
	for (std::size_t k = fromTri; k < m.tris.size(); ++k) {
		const auto &t = m.tris[k];
		area += 0.5 * cartoon::length(cartoon::cross(m.verts[t[1]] - m.verts[t[0]], m.verts[t[2]] - m.verts[t[0]]));
	}
	return area;
}

}  // namespace

// A gully one cell wide is no cliff: as one it was a dark crack across the green hills. A real cliff still is one.
TEST(FilmReal, AGullyOneCellWideIsNoCliff) {
	const Mesh gully = grid(40, [](int i, int) { return i == 20 ? -3.0 : 0.0; });
	EXPECT_TRUE(terrain::surfacePart(gully, 1, 0.13).tris.empty());
	EXPECT_TRUE(terrain::surfacePart(gully, 2, 0.13).tris.empty());
	EXPECT_EQ(terrain::surfacePart(gully, 0, 0.13).tris.size(), gully.tris.size()) << "all of it ground";
	const Mesh wall = grid(40, [](int i, int) { return i < 10 ? 0.0 : i < 30 ? 2.0 * (i - 10) : 40.0; });
	EXPECT_GT(terrain::surfacePart(wall, 1, 0.13).tris.size(), 1000u) << "a real cliff is still a cliff";
}

// The land's normals come from its own faces: one taken from finer ground than the mesh has leaned away from the viewer on a face that looks at them, and shaded black.
TEST(FilmReal, TheLandsNormalsAgreeWithItsFaces) {
	const Mesh ridge = grid(30, [](int i, int j) { return 4.0 * std::sin(i * 0.7) * std::cos(j * 0.5); });
	for (const auto &t : ridge.tris) {
		const V3 face = cartoon::normalize(cartoon::cross(ridge.verts[t[1]] - ridge.verts[t[0]], ridge.verts[t[2]] - ridge.verts[t[0]])) * 1.0;
		const V3 up = face.y < 0.0 ? face * -1.0 : face;
		for (int c = 0; c < 3; ++c) EXPECT_GT(cartoon::dot(up, ridge.normals[static_cast<std::size_t>(t[c])]), 0.15);
	}
}

// Far off a tree keeps its shape: its own limbs, and fewer, bigger leaves covering as much sky in the same places. It was a ball, or a cone.
TEST(FilmReal, AFarTreeIsTheNearTreeWithFewerBiggerLeaves) {
	for (const std::string sp : {"broadleaf", "birch", "conifer", "acacia", "palm", "kapok", "shrub"}) {
		const forest::detail::Grown near_ = forest::detail::grow(sp, 1, false, false), far_ = forest::detail::growFar(sp, 1, false);
		EXPECT_EQ(near_.wood.tris.size(), far_.wood.tris.size()) << sp << ": its own limbs";
		EXPECT_LT(far_.foliage.tris.size(), near_.foliage.tris.size() / 3) << sp << ": fewer leaves";
		const double nearArea = leafArea(near_.foliage, near_.coreTris), farArea = leafArea(far_.foliage, far_.coreTris);
		EXPECT_NEAR(farArea / nearArea, 1.0, 0.12) << sp << ": covering as much";
		double nearTop = -1e30, farTop = -1e30;
		for (const V3 &p : near_.foliage.verts) nearTop = std::max(nearTop, p.y);
		for (const V3 &p : far_.foliage.verts) farTop = std::max(farTop, p.y);
		EXPECT_NEAR(farTop, nearTop, 0.1 * nearTop + 0.5) << sp << ": as tall";
	}
}

// Each leaf has a cell of the picture of leaves, and the same one near and far: the top of a crown is in the rows of the most exposed.
TEST(FilmReal, EachLeafHasItsPlaceInThePictureOfLeaves) {
	const forest::detail::Grown tree = forest::detail::grow("broadleaf", 0, false, false);
	ASSERT_EQ(tree.foliage.uvs.size(), tree.foliage.verts.size());
	double low = 0.0, high = 0.0;
	int nLow = 0, nHigh = 0;
	double bottom = 1e30, top = -1e30;
	for (const V3 &p : tree.foliage.verts) bottom = std::min(bottom, p.y), top = std::max(top, p.y);
	for (std::size_t i = 0; i < tree.foliage.verts.size(); ++i) {
		const auto &uv = tree.foliage.uvs[i];
		ASSERT_GE(uv.first, 0.0);
		ASSERT_LE(uv.first, 1.0);
		ASSERT_LE(uv.second, 1.0);
		if (tree.foliage.verts[i].y < bottom + 0.2 * (top - bottom)) low += uv.second, ++nLow;
		if (tree.foliage.verts[i].y > top - 0.2 * (top - bottom)) high += uv.second, ++nHigh;
	}
	ASSERT_GT(nLow, 0);
	ASSERT_GT(nHigh, 0);
	EXPECT_GT(high / nHigh, low / nLow + 0.15);
	const forest::detail::Grown far_ = forest::detail::growFar("broadleaf", 0, false);
	EXPECT_EQ(far_.foliage.uvs.size(), far_.foliage.verts.size());
}

// Autumn does not take a crown all at once: the exposed leaves have turned, those deep inside are still green. No two leaves alike.
TEST(FilmReal, AutumnTurnsTheExposedLeavesFirst) {
	const V3 inside = forest::detail::cellColour("broadleaf", "autumn", 0, 0, 3), outside = forest::detail::cellColour("broadleaf", "autumn", 0, forest::detail::kLeafCells - 1, 3);
	EXPECT_GT(outside.x / outside.y, inside.x / inside.y) << "redder outside, greener inside";
	std::map<int, int> seen;
	for (int col = 0; col < forest::detail::kLeafCells; ++col) ++seen[static_cast<int>(forest::detail::cellColour("broadleaf", "summer", 1, 4, col).y * 2000.0)];
	EXPECT_GT(seen.size(), 5u);
}

// Rain stays clear of the lens, where a streak this bright is a wide smear out of focus, and is as thin as a drop.
TEST(FilmReal, RainKeepsClearOfTheLens) {
	cartoon::particles::Weather w;
	w.kind = "rain", w.amount = 1.0;
	const std::string text = cartoon::particles::weatherText(w, 3.0, {0.0, 2.0, 0.0}, 0.02);
	ASSERT_NE(text.find("point3 P"), std::string::npos);
	const std::size_t from = text.find('[', text.find("point3 P"));
	std::istringstream in(text.substr(from + 1, text.find(']', from) - from - 1));
	double x = 0.0, y = 0.0, z = 0.0, nearest = 1e30;
	while (in >> x >> y >> z) nearest = std::min(nearest, std::sqrt(x * x + (y - 2.0) * (y - 2.0) + z * z));
	EXPECT_GT(nearest, 4.7);
	EXPECT_LT(nearest, 8.0) << "but close enough to be seen";
}

namespace {
terrain::Spec landOf(const std::string &biome) {
	terrain::Spec s;
	s.kind = "hills", s.size = 400.0, s.height = 12.0, s.seed = 3, s.biome = biome, s.ranges = false;
	return s;
}
std::size_t placements(const std::string &text) {
	std::size_t n = 0;
	for (std::size_t at = text.find("ObjectInstance"); at != std::string::npos; at = text.find("ObjectInstance", at + 1)) ++n;
	return n;
}
}  // namespace

// The land grows something of its own: grass in view of the camera, finer near it, none behind it; thinned by blades, not by leaving squares bare, which reads as a grid.
TEST(FilmReal, TheLandGrowsWhatItWouldInViewOfTheCamera) {
	namespace cover = cartoon::cover;
	const terrain::Spec s = landOf("");
	const auto field = terrain::fieldOf(s);
	const V3 eye{0.0, field->heightAt(0.0, 0.0) + 1.6, 0.0}, ahead{0.0, 2.0, -100.0};
	const cover::Cover on;
	const std::string text = cover::text("photonsmith-test-cover", *field, on, eye, ahead, 40.0, 1.0, 0.0, {}, {});
	EXPECT_GT(placements(text), 100u);
	EXPECT_NE(text.find("cover_grass_0_"), std::string::npos) << "the finest squares near the camera";
	EXPECT_NE(text.find("cover_grass_3_"), std::string::npos) << "the coarsest far off";
	EXPECT_NE(text.find("diffusetransmission"), std::string::npos) << "light comes through a blade";
	// the same place and the same square as the camera moves: what grows is fixed to the land
	const V3 eye2{1.0, eye.y, 0.0};
	const std::string moved = cover::text("photonsmith-test-cover", *field, on, eye2, ahead + V3{1.0, 0.0, 0.0}, 40.0, 1.0, 0.0, {}, {});
	const std::size_t a = text.rfind("ConcatTransform"), line = text.find('\n', a);
	EXPECT_NE(moved.find(text.substr(a, line - a)), std::string::npos);
	// snow and `undergrowth off`, and land that grows nothing
	EXPECT_EQ(cover::text("photonsmith-test-cover", *field, on, eye, ahead, 40.0, 1.0, 0.8, {}, {}), "");
	cover::Cover off;
	off.on = false;
	EXPECT_EQ(cover::text("photonsmith-test-cover", *field, off, eye, ahead, 40.0, 1.0, 0.0, {}, {}), "");
	EXPECT_EQ(cover::growthOf(landOf("arctic")).density, 0.0);
	// thin land has as many squares, with fewer blades in each
	EXPECT_LT(cover::growthOf(landOf("desert")).density, 0.3);
	const auto desert = terrain::fieldOf(landOf("desert"));
	EXPECT_NEAR(static_cast<double>(placements(cover::text("photonsmith-test-cover-d", *desert, on, eye, ahead, 40.0, 1.0, 0.0, {}, {}))), static_cast<double>(placements(text)), 0.35 * placements(text));
}

// Under trees the grass gives way to fallen leaves, with an edge that wanders and no square of a grid.
TEST(FilmReal, UnderTheTreesTheGrassGivesWayToLitter) {
	namespace cover = cartoon::cover;
	const terrain::Spec s = landOf("");
	const auto field = terrain::fieldOf(s);
	const double y = field->heightAt(0.0, -12.0);
	forest::Tree tree;
	tree.species = "broadleaf", tree.at = {0.0, y, -12.0};
	const V3 eye{0.0, y + 1.6, 0.0}, ahead{0.0, y, -30.0};
	const std::string text = cover::text("photonsmith-test-cover", *field, {}, eye, ahead, 40.0, 1.0, 0.0, {}, {{tree}});
	EXPECT_NE(text.find("cover_litter_"), std::string::npos);
	EXPECT_NE(text.find("cover_grass_"), std::string::npos) << "and grass beyond them";
	EXPECT_EQ(cover::text("photonsmith-test-cover", *field, {}, eye, ahead, 40.0, 1.0, 0.0, {}, {}).find("cover_litter_"), std::string::npos);
}

TEST(FilmReal, UndergrowthCanBeTurnedOffAndThinned) {
	cartoon::film_file::Spec s;
	bool handled = false;
	EXPECT_EQ(cartoon::film_file::natureLine(s, "undergrowth", "undergrowth amount 0.5", "amount 0.5", handled), "");
	EXPECT_DOUBLE_EQ(s.undergrowth.amount, 0.5);
	EXPECT_EQ(cartoon::film_file::natureLine(s, "undergrowth", "undergrowth off", "off", handled), "");
	EXPECT_FALSE(s.undergrowth.on);
	EXPECT_NE(cartoon::film_file::natureLine(s, "undergrowth", "undergrowth lots", "lots", handled), "");
}

// Where a finer grid of grass hands over to a coarser one no bare ground shows: every spot is in a square that is laid, however the rings' edges wander.
TEST(FilmReal, TheRingsOfGrassLeaveNoHolesWhereTheyMeet) {
	namespace cover = cartoon::cover;
	const V3 eye{0.31, 1.8, 0.77};
	const auto laid = [&](double x, double z) {
		for (std::size_t r = 0; r < cover::rings().size(); ++r) {
			const double cell = cover::rings()[r].cell, cx = (std::floor(x / cell) + 0.5) * cell, cz = (std::floor(z / cell) + 0.5) * cell;
			if (cover::ringLays(r, std::sqrt((cx - eye.x) * (cx - eye.x) + eye.y * eye.y + (cz - eye.z) * (cz - eye.z)), 0.999)) return true;   // (the far edge at its nearest)
		}
		return false;
	};
	for (double d = 0.1; d < 270.0; d += 0.13)
		for (int a = 0; a < 360; a += 7) {
			const double x = eye.x + d * std::cos(a * 0.0174533), z = eye.z + d * std::sin(a * 0.0174533);
			ASSERT_TRUE(laid(x, z)) << d << " m from the camera, at " << a << " degrees";
		}
}

// A fire that lives through a story: it shrinks to nothing and roars back, leans in the wind, and can leave its light, sparks and sound to the film.
TEST(FilmReal, AFireCanChangeOverTheFilm) {
	namespace ff = cartoon::film_file;
	namespace firefx = cartoon::fire;
	ff::Spec s;
	EXPECT_EQ(ff::fireLine(s, "fire", "fire at 1 0 -19  size 3  light off  sparks off  sound off  smoke off  detail 30  bright 2  sizes 0 6  10 0.5  20 8  leans 0 0  10 0.3"), "");
	ASSERT_EQ(s.fires.size(), 1u);
	const firefx::Fire &f = s.fires[0];
	EXPECT_FALSE(f.lit);
	EXPECT_FALSE(f.sparks);
	EXPECT_FALSE(f.sound);
	EXPECT_EQ(f.detail, 30);
	EXPECT_DOUBLE_EQ(f.sizeAt(-1.0), 6.0);
	EXPECT_DOUBLE_EQ(f.sizeAt(5.0), 3.25);
	EXPECT_DOUBLE_EQ(f.sizeAt(99.0), 8.0);
	EXPECT_DOUBLE_EQ(f.leanAt(5.0), 0.15);
	const firefx::Fire plain;
	EXPECT_DOUBLE_EQ(plain.sizeAt(3.0), plain.size) << "with no keys, its size";
	EXPECT_NE(ff::fireLine(s, "fire", "fire at 0 0 0 sizes 5 1  2 3"), "") << "keys go forward";
	EXPECT_NE(ff::fireLine(s, "fire", "fire at 0 0 0 sizes 1 500"), "") << "and stay a fire's size";
	EXPECT_NE(ff::fireLine(s, "fire", "fire at 0 0 0 leans 0 0.5"), "") << "tips stay in its grid";
	ff::Spec dying;
	ASSERT_EQ(ff::fireLine(dying, "fire", "fire at 0 0 0 sizes 0 6  10 0.6"), "");
	EXPECT_DOUBLE_EQ(dying.fires[0].size, 6.0) << "with no `size`, the tallest of its `sizes`: its sparks start from there, not from a fire of 1.2 m";
	namespace particles = cartoon::particles;
	particles::Emitter e;
	e.kind = "sparks", e.at = {0, 0, 0}, e.t0 = 0.0, e.t1 = 20.0, e.rate = 100.0;
	particles::Emitter dies = e;
	dies.strength = {{0.0, 1.0}, {10.0, 0.1}};
	const auto count = [](const std::string &text) { std::size_t n = 0; for (std::size_t at = text.find("Shape"); at != std::string::npos; at = text.find("Shape", at + 1)) ++n; return n; };
	EXPECT_LT(2 * count(particles::emitterText(dies, 15.0)), count(particles::emitterText(e, 15.0))) << "a fire dying down throws fewer sparks";
	EXPECT_NE(firefx::flameText("f", {0, 0, 0}, 2.0, 1.0, 1, 10).find("LightSource"), std::string::npos);
	const std::string dark = firefx::flameText("f", {0, 0, 0}, 2.0, 1.0, 1, 10, 0.0, 1.0, false);
	EXPECT_EQ(dark.find("LightSource"), std::string::npos) << "its light left to the scene";
	EXPECT_NE(dark, firefx::flameText("f", {0, 0, 0}, 2.0, 1.0, 1, 10, 0.3, 1.0, false)) << "leaning moves the flames";
}

// A coat with no pattern is not one flat colour: darker along the back, paler under the belly, the legs dark at the foot, and a herd is not all alike.
TEST(FilmReal, ANaturalCoatIsNotOneFlatColour) {
	namespace coats = cartoon::coats;
	const V3 coat{0.4, 0.25, 0.14};
	const auto mean = [&](double ax, double ay) {
		double sum = 0.0;
		for (int k = 0; k < 20; ++k) {
			const V3 c = coats::colourAt("natural", 0, coat, coat, ax + 0.004 * k, ay);
			sum += c.x + c.y + c.z;
		}
		return sum / 20.0;
	};
	EXPECT_GT(mean(0.2, 0.01), 1.4 * mean(0.2, 0.25)) << "a paler belly (the seam) than back (half way round)";
	EXPECT_LT(mean(0.4, 0.56), 0.8 * mean(0.05, 0.56)) << "a leg darker towards the foot";
	std::map<int, int> colours;
	for (int v = 0; v < coats::variantsOf("natural"); ++v) ++colours[static_cast<int>(coats::individual("horses", v, coat).x * 100.0)];
	EXPECT_GE(colours.size(), 5u) << "no two horses of a herd of six alike";
	EXPECT_EQ(coats::defaultFor("horses"), "natural");
	EXPECT_EQ(coats::defaultFor("zebras"), "zebra") << "a real pattern stays";
}

// A crown is grown, not filled: its branches fork, compete for the air and thicken towards the trunk, and no two trees' are alike.
TEST(FilmReal, ACrownIsGrownNotFilled) {
	namespace d = forest::detail;
	d::Crown c;
	c.root = {0.0, 2.5, 0.0}, c.centre = {0.0, 6.0, 0.0}, c.radii = {4.0, 3.2, 4.0};
	const std::vector<d::Branch> a = d::grownBranches(c, 11), b = d::grownBranches(c, 12);
	ASSERT_GT(a.size(), 200u);
	int forks = 0, tips = 0;
	for (const d::Branch &n : a) forks += n.children >= 2, tips += n.children == 0;
	EXPECT_GT(forks, 15);
	EXPECT_GT(tips, 30);
	for (std::size_t k = 1; k < a.size(); ++k) ASSERT_LE(a[k].radius, a[static_cast<std::size_t>(a[k].parent)].radius + 1e-9) << "thinner than what it grows from";
	EXPECT_GT(a[0].radius, 5.0 * a.back().radius);
	EXPECT_NE(a.size(), b.size()) << "another tree's is its own";
	// the same crown comes out the same, and it is not a ball: seen from above its edge wanders
	EXPECT_EQ(d::grownBranches(c, 11).size(), a.size());
	double most[16] = {0.0};
	for (const d::Branch &n : a) {
		const int at = static_cast<int>((std::atan2(n.at.z, n.at.x) + cartoon::kPi) / (2.0 * cartoon::kPi) * 15.999);
		most[at] = std::max(most[at], std::hypot(n.at.x, n.at.z));
	}
	const auto [lo, hi] = std::minmax_element(most, most + 16);
	EXPECT_GT(*hi - *lo, 0.5) << "lobes and hollows";
	// bare in winter its twigs show; a leafy tree's twigs hold leaves
	const auto summer = d::grow("broadleaf", 2, false, false), winter = d::grow("broadleaf", 2, true, false);
	EXPECT_TRUE(winter.foliage.tris.empty());
	EXPECT_GT(summer.foliage.tris.size(), 10000u);
	EXPECT_GT(winter.wood.tris.size(), summer.wood.tris.size());
}

// Bark tiles with no seam, stands in ridges where it should and is smooth where it should, and every part of a tree carries it.
TEST(FilmReal, BarkTilesAndEveryPartOfATreeCarriesIt) {
	namespace d = forest::detail;
	for (const std::string sp : {"broadleaf", "conifer", "acacia", "birch", "palm", "kapok", "snag"}) {
		for (int k = 0; k < 40; ++k) {
			const double t = k / 40.0;
			EXPECT_NEAR(d::barkHeight(sp, 0.0, t), d::barkHeight(sp, 1.0, t), 1e-6) << sp;
			EXPECT_NEAR(d::barkHeight(sp, t, 0.0), d::barkHeight(sp, t, 1.0), 1e-6) << sp;
		}
		const d::Grown g = d::grow(sp, 0, false, false);
		EXPECT_EQ(g.wood.uvs.size(), g.wood.verts.size()) << sp << ": a mesh with too few is shown with none";
	}
	double oakLow = 1.0, birchLow = 1.0;
	for (int k = 0; k < 200; ++k) {
		const double u = k / 200.0, v = 0.37 * k / 200.0;
		oakLow = std::min(oakLow, d::barkHeight("broadleaf", u, v)), birchLow = std::min(birchLow, d::barkHeight("birch", u, v));
	}
	EXPECT_LT(oakLow, 0.2) << "deep furrows";
	EXPECT_GT(birchLow, 0.5) << "smooth";
	const std::string text = forest::objectText("photonsmith-test-bark", "birch", 0, "summer", true);
	EXPECT_NE(text.find("\"texture displacement\""), std::string::npos) << "a relief the low sun catches";
}

// A wood is not scattered evenly: groves and glades, seedlings by their parents, the old and the young, and its dead.
TEST(FilmReal, AWoodFormsAsWoodsDo) {
	forest::Forest f;
	f.kind = "mixed", f.count = 900, f.area = true, f.x0 = -200, f.z0 = -200, f.x1 = 200, f.z1 = 200;
	const std::vector<forest::Tree> trees = forest::plant(f, nullptr, -1e30, {});
	ASSERT_GT(trees.size(), 600u);
	int cells[10][10] = {{0}}, snags = 0, logs = 0;
	double smallest = 1e30, largest = 0.0;
	for (const forest::Tree &t : trees) {
		++cells[std::clamp(static_cast<int>((t.at.x + 200.0) / 40.0), 0, 9)][std::clamp(static_cast<int>((t.at.z + 200.0) / 40.0), 0, 9)];
		snags += t.species == "snag", logs += t.species == "log";
		smallest = std::min(smallest, t.scale), largest = std::max(largest, t.scale);
	}
	const double mean = static_cast<double>(trees.size()) / 100.0;
	double spread = 0.0;
	for (const auto &row : cells)
		for (const int n : row) spread += (n - mean) * (n - mean) / 100.0;
	EXPECT_GT(spread / mean, 3.0) << "evenly scattered trees give about 1: these stand in groves";
	EXPECT_GT(snags, 3);
	EXPECT_GT(logs, 3);
	EXPECT_GT(largest / smallest, 3.0) << "saplings and giants";
}

// Close to the camera an animal is drawn with real hair, wherever it has got to and with no line in the film saying so; in rain a coat goes dark and glossy; a nose is dark.
TEST(FilmReal, AnAnimalCloseToTheCameraHasRealHairAndRainWetsItsCoat) {
	namespace wildlife = cartoon::wildlife;
	namespace animals = cartoon::animals;
	wildlife::Herd h;
	h.kind = "horses", h.count = 4, h.fx = -5, h.tx = 40, h.name = "herd1";
	const auto draw = [&](const V3 *eye, double rain) {
		animals::Poses own;
		return wildlife::herdText(h, 2.0, nullptr, own, {}, {}, -1e30, eye, rain);
	};
	const V3 far_{0.0, 2.0, 500.0}, by{0.0, 2.0, 5.0};
	const std::string distant = draw(&far_, 0.0), close = draw(&by, 0.0);
	EXPECT_EQ(distant.find("\"hair\""), std::string::npos) << "ribbons of fur far off";
	EXPECT_NE(close.find("\"hair\""), std::string::npos) << "real strands close up";
	EXPECT_EQ(draw(nullptr, 0.0).find("\"hair\""), std::string::npos);
	EXPECT_NE(draw(&far_, 0.8).find("_wet"), std::string::npos) << "out in the rain";
	EXPECT_EQ(distant.find("_wet"), std::string::npos);
	namespace coats = cartoon::coats;
	const V3 cheek = coats::colourAt("natural", 0, {0.4, 0.25, 0.14}, {0.4, 0.25, 0.14}, 0.8, 0.1), nose = coats::colourAt("natural", 0, {0.4, 0.25, 0.14}, {0.4, 0.25, 0.14}, 0.995, 0.1);
	EXPECT_LT(nose.x + nose.y + nose.z, 0.4 * (cheek.x + cheek.y + cheek.z));
}

// An animal moves as a body with weight: the hips and shoulders tip against each other as it goes, its head steadier than its neck, its tail alive, and it breathes standing still.
TEST(FilmReal, TheAnimalsClipsMoveTheWholeBody) {
	const auto model = cartoon::animals::modelOf("horses");
	ASSERT_TRUE(model);
	const auto trackOf = [&](const char *clip, const char *joint) {
		const int c = model->clip(clip);
		EXPECT_GE(c, 0) << clip;
		int count = 0;
		for (const auto &ch : model->rig.clips[static_cast<std::size_t>(c)].channels) count += model->rig.nodes[static_cast<std::size_t>(ch.node)].name == joint;
		return count;
	};
	for (const char *gait : {"walk", "trot", "run"}) {
		EXPECT_GE(trackOf(gait, "head"), 1) << gait << ": its head";
		EXPECT_GE(trackOf(gait, "tail"), 1) << gait;
		EXPECT_GE(trackOf(gait, "chest"), 1) << gait;
	}
	EXPECT_GE(trackOf("idle", "chest"), 1) << "it breathes";
	EXPECT_GE(trackOf("idle", "pelvis"), 1) << "and eases its weight";
}

// A leaf is waxy, so it shines, and what it gives back does not go up with the wax: the green underneath loses a share as the wax gains one. Rain makes more of it, smoother.
TEST(FilmReal, LeavesAreWaxyAndRainMakesThemShine) {
	namespace d = forest::detail;
	const std::string dry = d::leafMaterial("photonsmith-test-wax", "wax_test", "broadleaf", 0, "summer", std::string(), 0.0, 0.0);
	const std::string wet = d::leafMaterial("photonsmith-test-wax", "wax_test", "broadleaf", 0, "summer", std::string(), 0.0, 1.0);
	EXPECT_NE(dry.find("\"coateddiffuse\""), std::string::npos);
	EXPECT_NE(dry.find("Material \"mix\""), std::string::npos);
	EXPECT_NE(dry.find("\"float amount\" [ 0.400 ]"), std::string::npos);
	EXPECT_NE(wet.find("\"float amount\" [ 0.650 ]"), std::string::npos);
	EXPECT_NE(dry.find("\"float roughness\" [ 0.320 ]"), std::string::npos);
	EXPECT_NE(wet.find("\"float roughness\" [ 0.100 ]"), std::string::npos);
}

// A leaf is not flat: it folds along its midrib, each its own way, and keeps its size and its place in the mesh (a far tree's and a season's leaves are found by their six points).
TEST(FilmReal, LeavesFoldAlongTheirMidribs) {
	Mesh m;
	for (int k = 0; k < 40; ++k) cartoon::nature::addLeaf(m, V3{static_cast<double>(k), 0.0, 0.0}, V3{0.0, 0.0, 1.0}, V3{0.0, 1.0, 0.0}, 0.2, 0.1);
	const Mesh flat = m;
	forest::detail::shapeLeaves(m, 0, 11);
	ASSERT_EQ(m.verts.size(), flat.verts.size());
	std::set<int> folds;
	for (int k = 0; k < 40; ++k) {
		const std::size_t f = static_cast<std::size_t>(k) * 6;
		EXPECT_EQ(m.verts[f].x, flat.verts[f].x) << "the stalk stays where it grew";
		const double lift = m.verts[f + 1].y - flat.verts[f + 1].y, other = m.verts[f + 5].y - flat.verts[f + 5].y;
		EXPECT_GT(lift + other, 0.0) << "both sides rise from the midrib";
		EXPECT_LT(m.verts[f + 3].y, flat.verts[f + 3].y + 1e-9) << "the tip hangs";
		EXPECT_NEAR(length(m.normals[f + 1]), 1.0, 1e-9);
		EXPECT_GT(std::abs(m.normals[f + 1].x - m.normals[f + 5].x), 0.01) << "the two halves face apart";
		folds.insert(static_cast<int>(lift * 1e4));
	}
	EXPECT_GT(folds.size(), 20u) << "each its own";
}

// Up close a tree's leaves are tufts of smaller leaves, as much leaf as there was, every one in some tuft and near it, and a conifer's dark core stays out of them.
TEST(FilmReal, ACloseTreesLeavesAreTufts) {
	namespace d = forest::detail;
	Mesh m;
	m.verts = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
	m.normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
	m.uvs = {{0, 0}, {0, 0}, {0, 0}};
	m.tris = {{0, 1, 2}};
	for (int k = 0; k < 60; ++k) cartoon::nature::addLeaf(m, V3{k < 30 ? -5.0 : 5.0, 0.1 * k, 0.0}, V3{0.0, 0.0, 1.0}, V3{0.0, 1.0, 0.0}, 0.2, 0.1);
	m.uvs.resize(m.verts.size());
	const std::vector<V3> at = {{-5.0, 1.0, 0.0}, {5.0, 4.0, 0.0}};
	const std::vector<Mesh> cut = d::cutIntoTufts(m, 3, 1, at);
	ASSERT_EQ(cut.size(), 3u);
	EXPECT_EQ(cut[2].tris.size(), 1u) << "the core, as it was";
	EXPECT_EQ(cut[0].tris.size(), 30u * 3u * 4u);
	EXPECT_EQ(cut[1].tris.size(), 30u * 3u * 4u);
	for (const V3 &p : cut[0].verts) EXPECT_LT(p.x, -4.0);
	for (const V3 &p : cut[1].verts) EXPECT_GT(p.x, 4.0);
	const std::vector<V3> &oak = d::tuftsOf("broadleaf", 0);
	EXPECT_GE(oak.size(), 6u);
	EXPECT_LE(static_cast<int>(oak.size()), d::kMostTufts);
	EXPECT_TRUE(d::tuftsOf("cactus", 0).empty());
}

// A tree close to the camera moves in parts, and further off it is one object; its tufts turn each their own way and quicker than the trunk leans.
TEST(FilmReal, ACloseTreeMovesInParts) {
	forest::Forest f;
	forest::Tree tree;
	tree.species = "broadleaf", tree.at = {0.0, 0.0, -10.0}, tree.scale = 1.0, tree.turn = 30.0;
	const std::vector<forest::Tree> trees = {tree};
	const auto frame = [&](double t, const V3 &eye) { return forest::text("photonsmith-test-tufts", trees, f, t, t, eye); };
	const std::string close = frame(1.0, {0.0, 2.0, 0.0}), later = frame(1.04, {0.0, 2.0, 0.0}), far = frame(1.0, {0.0, 2.0, 60.0});
	EXPECT_NE(close.find("ObjectInstance \"forest_broadleaf_0_summer_near_close_t0\""), std::string::npos);
	EXPECT_NE(close.find("_cut\""), std::string::npos) << "the leaves have their own outline";
	EXPECT_EQ(far.find("_close"), std::string::npos) << "and not out here";
	EXPECT_NE(far.find("ObjectInstance \"forest_broadleaf_0_summer_near\""), std::string::npos);
	const auto placing = [](const std::string &text, const std::string &object) {
		const std::size_t at = text.find("ObjectInstance \"" + object + "\"");
		const std::size_t from = at == std::string::npos ? at : text.rfind("ConcatTransform", at);
		return from == std::string::npos ? std::string() : text.substr(from, at - from);
	};
	const std::string tuft = "forest_broadleaf_0_summer_near_close_t3";
	ASSERT_FALSE(placing(close, tuft).empty());
	EXPECT_NE(placing(close, tuft), placing(later, tuft)) << "a twenty-fifth of a second on, a tuft has moved";
	EXPECT_NE(placing(close, tuft), placing(close, "forest_broadleaf_0_summer_near_close_t4")) << "and not as its neighbour has";
	std::size_t tufts = 0;
	for (std::size_t p = close.find("ObjectInstance \"forest_broadleaf_0_summer_near_close_t"); p != std::string::npos; p = close.find("ObjectInstance \"forest_broadleaf_0_summer_near_close_t", p + 1)) ++tufts;
	EXPECT_GE(tufts, 6u);
}

// A forest's objects are said once a frame however many forests share them.
TEST(FilmReal, ForestsShareTheirObjects) {
	forest::Forest f;
	forest::Tree tree;
	tree.species = "birch", tree.at = {0.0, 0.0, -10.0};
	const std::vector<forest::Tree> trees = {tree};
	forest::Written written;
	const std::string one = forest::text("photonsmith-test-shared", trees, f, 1.0, 1.0, {0.0, 2.0, 0.0}, {}, 0.0, written);
	const std::string two = forest::text("photonsmith-test-shared", trees, f, 1.0, 1.0, {0.0, 2.0, 0.0}, {}, 0.0, written);
	EXPECT_NE(one.find("ObjectBegin"), std::string::npos);
	EXPECT_EQ(two.find("ObjectBegin"), std::string::npos);
	EXPECT_NE(two.find("_close_t0\""), std::string::npos) << "the second still places its tufts";
}

// Moss lies on the wood, low down, and on one side of the sky whichever way a tree stands.
TEST(FilmReal, MossGrowsLowOnTheDampSide) {
	namespace moss = cartoon::moss;
	const Mesh trunk = cartoon::nature::trunk(9.0, 0.3, 0.0, 5);
	const Mesh skin = moss::trunkSkin(trunk);
	ASSERT_FALSE(skin.tris.empty());
	EXPECT_LT(skin.tris.size(), trunk.tris.size());
	for (const V3 &p : skin.verts) EXPECT_LT(p.y, moss::kReach + 0.02);
	ASSERT_EQ(skin.uvs.size(), skin.verts.size());
	for (const auto &t : skin.tris) {   // (no triangle stretched the whole way round across the seam of the angle)
		const double a = skin.uvs[static_cast<std::size_t>(t[0])].first, b = skin.uvs[static_cast<std::size_t>(t[1])].first, c = skin.uvs[static_cast<std::size_t>(t[2])].first;
		EXPECT_LT(std::max({a, b, c}) - std::min({a, b, c}), 0.3);
	}
	const V3 north{0.0, 0.0, -1.0};
	for (const double turn : {0.0, 70.0, 140.0, 233.0, 310.0}) {
		const cartoon::Mat4 place = cartoon::rotate({0, 1, 0}, turn);
		const double a = 2.0 * cartoon::kPi * moss::sideFor(place, north) / moss::kSides;
		EXPECT_GT(dot(cartoon::transformPoint(place, V3{std::cos(a), 0.0, std::sin(a)}), north), 0.7) << turn;
	}
	Mesh dome = cartoon::nature::rock(1.0, 3);
	const Mesh top = moss::topSkin(dome, 0.8, 0.0);
	ASSERT_FALSE(top.tris.empty());
	EXPECT_LT(top.tris.size(), dome.tris.size()) << "only what faces up";
}

// Grass is not a carpet: tussocks with thinner ground between them, some of it dead, some lying down, and close to the camera some gone to seed.
TEST(FilmReal, GrassGrowsInTussocks) {
	namespace cover = cartoon::cover;
	const Mesh plain = cover::detail::patchMesh(1.0, 900, 3, 0.0045, 0.32, 9), seeded = cover::detail::patchMesh(1.0, 900, 3, 0.0045, 0.32, 9, true);
	EXPECT_GT(seeded.tris.size(), plain.tris.size() + 200u) << "heads of grain";
	int cells[5][5] = {{0}}, straw = 0;
	for (std::size_t i = 0; i < plain.verts.size(); ++i) {
		++cells[std::clamp(static_cast<int>((plain.verts[i].x + 0.5) * 5.0), 0, 4)][std::clamp(static_cast<int>((plain.verts[i].z + 0.5) * 5.0), 0, 4)];
		straw += plain.uvs[i].first > (cover::kBladeCols - 2.0) / cover::kBladeCols;
	}
	int lo = 1 << 30, hi = 0;
	for (const auto &row : cells)
		for (const int c : row) lo = std::min(lo, c), hi = std::max(hi, c);
	EXPECT_GT(hi, 3 * lo) << "uneven";
	EXPECT_GT(straw, static_cast<int>(plain.verts.size() / 12));
	EXPECT_LT(straw, static_cast<int>(plain.verts.size() / 4));
}

// The grass keeps the forest's year, or its own, each through the change of season.
TEST(FilmReal, GrassKeepsTheYear) {
	namespace cover = cartoon::cover;
	forest::Forest f;
	f.season = "summer", f.seasonTo = "autumn", f.turn0 = 2.0, f.turn1 = 6.0;
	forest::Forest::Change winter;
	winter.to = "winter", winter.t0 = 8.0, winter.t1 = 12.0;
	f.then.push_back(winter);
	const cover::Cover own = [] {
		cover::Cover c;
		c.season = "spring";
		return c;
	}();
	EXPECT_DOUBLE_EQ(cover::yearAt({}, {}, 1.0), 0.0) << "no forest: summer";
	EXPECT_DOUBLE_EQ(cover::yearAt({}, {f}, 1.0), 0.0);
	EXPECT_DOUBLE_EQ(cover::yearAt({}, {f}, 7.0), 1.0);
	EXPECT_DOUBLE_EQ(cover::yearAt({}, {f}, 13.0), 1.6);
	EXPECT_LT(cover::yearAt(own, {f}, 13.0), 0.0) << "its own season, whatever the trees do";
}
