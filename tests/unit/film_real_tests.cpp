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
