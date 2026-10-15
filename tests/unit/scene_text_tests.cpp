/**
 * @file scene_text_tests.cpp
 * @brief The Scene Builder's 3D text: triangulating letter outlines (with holes, islands, any winding) and extruding them into a solid. std-only, no font needed: the outlines
 *        here are made by hand, and a fake provider stands in for the GUI's.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "../../src/shared/scene_text.h"

using namespace scene_doc;

namespace {

TextContour rect(double x0, double y0, double x1, double y1) { return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}; }
TextContour circle(double cx, double cy, double r, int n = 48) {
	TextContour c;
	for (int i = 0; i < n; ++i) c.push_back({cx + r * std::cos(2 * 3.14159265358979323846 * i / n), cy + r * std::sin(2 * 3.14159265358979323846 * i / n)});
	return c;
}
double polygonArea(const TextContour& c) { return std::fabs(text_detail::signedArea(c)); }

// The total area of the face's triangles, and whether every one is counter-clockwise.
double faceArea(const TextFace& f, bool* allCounterClockwise = nullptr) {
	double sum = 0;
	bool ccw = true;
	for (std::size_t t = 0; t + 2 < f.triangles.size(); t += 3) {
		const TextPoint &a = f.points[static_cast<std::size_t>(f.triangles[t])], &b = f.points[static_cast<std::size_t>(f.triangles[t + 1])], &c = f.points[static_cast<std::size_t>(f.triangles[t + 2])];
		const double area = 0.5 * ((b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1]));
		if (area < 0) ccw = false;
		sum += std::fabs(area);
	}
	if (allCounterClockwise) *allCounterClockwise = ccw;
	return sum;
}

// The volume of a mesh by the divergence theorem: positive when every triangle faces outward.
double volumeOf(const ShapeMesh& m) {
	double v = 0;
	for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3) {
		const double* a = &m.P[static_cast<std::size_t>(m.indices[t]) * 3];
		const double* b = &m.P[static_cast<std::size_t>(m.indices[t + 1]) * 3];
		const double* c = &m.P[static_cast<std::size_t>(m.indices[t + 2]) * 3];
		v += (a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6.0;
	}
	return v;
}

}  // namespace

TEST(SceneTextTriangulation, ASquareIsTwoTriangles) {
	const TextFace f = triangulateOutlines({rect(0, 0, 2, 2)});
	EXPECT_EQ(f.triangles.size(), 6u);
	EXPECT_NEAR(faceArea(f), 4.0, 1e-9);
}

TEST(SceneTextTriangulation, AConcaveShapeIsFilledExactly) {
	// An L: 3 x 3 with the top right 2 x 2 taken out (area 5), and a comb.
	const TextContour l = {{0, 0}, {3, 0}, {3, 1}, {1, 1}, {1, 3}, {0, 3}};
	bool ccw = false;
	EXPECT_NEAR(faceArea(triangulateOutlines({l}), &ccw), 5.0, 1e-9);
	EXPECT_TRUE(ccw);
	const TextContour comb = {{0, 0}, {5, 0}, {5, 3}, {4, 3}, {4, 1}, {3, 1}, {3, 3}, {2, 3}, {2, 1}, {1, 1}, {1, 3}, {0, 3}};
	EXPECT_NEAR(faceArea(triangulateOutlines({comb})), polygonArea(comb), 1e-9);
}

TEST(SceneTextTriangulation, TheWindingOfAnOutlineDoesNotMatter) {
	TextContour l = {{0, 0}, {3, 0}, {3, 1}, {1, 1}, {1, 3}, {0, 3}};
	const double forward = faceArea(triangulateOutlines({l}));
	std::reverse(l.begin(), l.end());
	bool allCcw = false;
	EXPECT_NEAR(faceArea(triangulateOutlines({l}), &allCcw), forward, 1e-9);
	EXPECT_TRUE(allCcw) << "the triangles are counter-clockwise whichever way the outline ran";
}

TEST(SceneTextTriangulation, AHoleIsLeftOut) {
	// An "O": a ring with a smaller circle inside, wound either way.
	for (int reverseHole = 0; reverseHole < 2; ++reverseHole) {
		TextContour outer = circle(0, 0, 2, 64), hole = circle(0.2, 0, 1, 48);
		if (reverseHole) std::reverse(hole.begin(), hole.end());
		const TextFace f = triangulateOutlines({outer, hole});
		EXPECT_NEAR(faceArea(f), polygonArea(outer) - polygonArea(hole), 1e-9) << "reverseHole " << reverseHole;
		// No triangle's centre is in the hole.
		for (std::size_t t = 0; t + 2 < f.triangles.size(); t += 3) {
			double cx = 0, cy = 0;
			for (int k = 0; k < 3; ++k) {
				cx += f.points[static_cast<std::size_t>(f.triangles[t + static_cast<std::size_t>(k)])][0] / 3;
				cy += f.points[static_cast<std::size_t>(f.triangles[t + static_cast<std::size_t>(k)])][1] / 3;
			}
			EXPECT_FALSE(text_detail::pointInContour(hole, cx, cy)) << "a triangle fills the hole";
		}
	}
}

TEST(SceneTextTriangulation, SeveralHolesAndAnIslandInAHole) {
	// A big rectangle with two square holes, and in the first hole an island.
	const TextContour outer = rect(0, 0, 10, 6), hole1 = rect(1, 1, 4, 5), hole2 = rect(6, 1, 9, 5), island = rect(2, 2, 3, 4);
	const TextFace f = triangulateOutlines({outer, hole1, hole2, island});
	EXPECT_NEAR(faceArea(f), polygonArea(outer) - polygonArea(hole1) - polygonArea(hole2) + polygonArea(island), 1e-9);
}

TEST(SceneTextTriangulation, TwoLettersSideBySideAreBothFilled) {
	const TextFace f = triangulateOutlines({rect(0, 0, 1, 2), circle(3, 1, 1, 32), circle(3, 1, 0.5, 32)});
	EXPECT_NEAR(faceArea(f), 2.0 + (polygonArea(circle(3, 1, 1, 32)) - polygonArea(circle(3, 1, 0.5, 32))), 1e-9);
}

TEST(SceneTextTriangulation, DegenerateOutlinesAreDropped) {
	// a two-point line, a flat triangle, a square
	const TextFace f = triangulateOutlines({TextContour{{0, 0}, {1, 1}}, TextContour{{0, 0}, {1, 0}, {2, 0}}, rect(0, 0, 1, 1)});
	EXPECT_NEAR(faceArea(f), 1.0, 1e-9);
	EXPECT_TRUE(triangulateOutlines({}).triangles.empty());
}

TEST(SceneTextExtrusion, ASquareExtrudesToABoxOfTheRightVolume) {
	TextOutline o;
	o.contours = {rect(0, 0, 2, 2)};
	o.capHeight = 2.0;   // the square is "a capital letter" 2 tall: asked for 1 tall, it is scaled by a half
	const ShapeMesh m = extrudeOutlines(o, 1.0, 1.0, 0.4);
	EXPECT_NEAR(volumeOf(m), 1.0 * 1.0 * 0.4, 1e-9) << "outward facing triangles enclose the box (a negative volume means they face inward)";
	double lo[3] = {1e9, 1e9, 1e9}, hi[3] = {-1e9, -1e9, -1e9};
	for (std::size_t i = 0; i < m.P.size(); ++i) {
		lo[i % 3] = std::min(lo[i % 3], m.P[i]);
		hi[i % 3] = std::max(hi[i % 3], m.P[i]);
	}
	for (int a = 0; a < 3; ++a) EXPECT_NEAR(lo[a], -hi[a], 1e-9) << "centred on axis " << a;
	EXPECT_NEAR(hi[2], 0.2, 1e-9);
	EXPECT_EQ(m.N.size(), m.P.size());
	EXPECT_EQ(m.UV.size() / 2, m.vertexCount());
}

TEST(SceneTextExtrusion, ARingExtrudesToATubeOfTheRightVolume) {
	TextOutline o;
	const TextContour outer = circle(0, 0, 2, 96), hole = circle(0, 0, 1, 96);
	o.contours = {outer, hole};
	o.capHeight = 4.0;
	const ShapeMesh m = extrudeOutlines(o, 4.0, 1.0, 0.5);
	EXPECT_NEAR(volumeOf(m), (polygonArea(outer) - polygonArea(hole)) * 0.5, 1e-6);
	// A hole's wall faces into the hole: some wall vertex has a normal pointing to the centre.
	bool inward = false;
	for (std::size_t v = 0; v < m.vertexCount(); ++v) {
		const double x = m.P[v * 3], y = m.P[v * 3 + 1], nx = m.N[v * 3], ny = m.N[v * 3 + 1];
		if (std::fabs(m.N[v * 3 + 2]) < 1e-9 && std::hypot(x, y) < 1.5 && x * nx + y * ny < -0.5 * std::hypot(x, y)) inward = true;
	}
	EXPECT_TRUE(inward);
}

TEST(SceneTextExtrusion, WidthFollowsTheStretch) {
	TextOutline o;
	o.contours = {rect(0, 0, 4, 2)};
	o.capHeight = 2.0;
	const auto widthOf = [&](double stretch) {
		const ShapeMesh m = extrudeOutlines(o, 2.0, stretch, 1.0);
		double lo = 1e9, hi = -1e9;
		for (std::size_t i = 0; i < m.P.size(); i += 3) {
			lo = std::min(lo, m.P[i]);
			hi = std::max(hi, m.P[i]);
		}
		return hi - lo;
	};
	EXPECT_NEAR(widthOf(1.0), 4.0, 1e-9);
	EXPECT_NEAR(widthOf(1.5), 6.0, 1e-9);
}

TEST(SceneTextExtrusion, SharpCornersKeepTheirOwnNormalsAndCurvesShareThem) {
	TextOutline o;
	o.contours = {rect(0, 0, 2, 2)};
	const ShapeMesh box = extrudeOutlines(o, 2.0, 1.0, 1.0);
	// Every side-wall normal of a square is one of the four axis directions: no corner is averaged.
	for (std::size_t v = 0; v < box.vertexCount(); ++v) {
		if (std::fabs(box.N[v * 3 + 2]) > 0.5) continue;
		EXPECT_TRUE(std::fabs(std::fabs(box.N[v * 3]) - 1.0) < 1e-9 || std::fabs(std::fabs(box.N[v * 3 + 1]) - 1.0) < 1e-9);
	}
	o.contours = {circle(0, 0, 1, 64)};
	const ShapeMesh round = extrudeOutlines(o, 2.0, 1.0, 1.0);
	int smooth = 0;
	for (std::size_t v = 0; v < round.vertexCount(); ++v) {
		if (std::fabs(round.N[v * 3 + 2]) > 0.5) continue;
		const double x = round.P[v * 3], y = round.P[v * 3 + 1];
		if (x * round.N[v * 3] + y * round.N[v * 3 + 1] > 0.999 * std::hypot(x, y)) ++smooth;   // the normal is the radial direction
	}
	EXPECT_GT(smooth, 64 * 3) << "the wall of a circle is shaded smoothly";
}

namespace {
bool fakeProvider(const TextParams& p, TextOutline& out) {
	if (p.text.empty() || p.text == " ") return false;
	// One block per character, 1 wide and 2 tall (a capital letter), with a gap of 0.5.
	for (std::size_t i = 0; i < p.text.size(); ++i) out.contours.push_back(rect(i * 1.5, 0, i * 1.5 + 1.0, 2.0));
	out.capHeight = 2.0;
	return true;
}
}  // namespace

TEST(SceneTextMesh, NothingWithoutAProviderAndSomethingWithOne) {
	setTextOutlineProvider(nullptr);
	EXPECT_EQ(textMesh(TextParams{}, 1.0, 1.0, 0.3).triangleCount(), 0u);
	setTextOutlineProvider(&fakeProvider);
	TextParams p;
	p.text = "AB";
	const ShapeMesh m = textMesh(p, 1.0, 1.0, 0.3);
	EXPECT_GT(m.triangleCount(), 0u);
	EXPECT_NEAR(volumeOf(m), 2 * (0.5 * 1.0 * 0.3), 1e-9) << "two blocks 0.5 wide and 1 tall";
	p.text = " ";
	EXPECT_EQ(textMesh(p, 1.0, 1.0, 0.3).triangleCount(), 0u) << "no outlines, no mesh";
	setTextOutlineProvider(nullptr);
}

TEST(SceneTextMesh, ALongTextIsCut) {
	setTextOutlineProvider(&fakeProvider);
	TextParams p;
	p.text = std::string(static_cast<std::size_t>(kMaxTextLength) + 50, 'x');
	const ShapeMesh m = textMesh(p, 1.0, 1.0, 0.3);
	EXPECT_NEAR(volumeOf(m), kMaxTextLength * (0.5 * 1.0 * 0.3), 1e-6);
	setTextOutlineProvider(nullptr);
}

// The cap counts characters, not bytes: a text of 3-byte characters keeps all kMaxTextLength of them and is never cut inside one.
TEST(SceneTextMesh, TheLengthCapCountsCharactersNotBytes) {
	std::string wide;
	for (int i = 0; i < kMaxTextLength + 50; ++i) wide += "\xE6\x97\xA5";   // (a CJK character, 3 bytes)
	EXPECT_EQ(textCharacterCount(wide), kMaxTextLength + 50);
	const std::string cut = cutTextToCharacters(wide, kMaxTextLength);
	EXPECT_EQ(textCharacterCount(cut), kMaxTextLength);
	EXPECT_EQ(cut.size(), static_cast<std::size_t>(kMaxTextLength) * 3) << "ends on a character boundary";
	EXPECT_EQ(cutTextToCharacters("short", kMaxTextLength), "short");
	EXPECT_EQ(cutTextToCharacters("abc", 3), "abc");
	EXPECT_EQ(cutTextToCharacters("abcd", 3), "abc");
}

// A deterministic jagged outline with many points and deep notches, like a heavily curved letter: the area must still come out exact.
TEST(SceneTextTriangulation, JaggedAndManyPointedOutlinesAreFilledExactly) {
	unsigned state = 12345u;
	const auto next = [&]() { state = state * 1664525u + 1013904223u; return static_cast<double>(state >> 8) / 16777216.0; };
	for (int n : {7, 31, 128, 400}) {
		TextContour c;
		for (int i = 0; i < n; ++i) {
			const double r = 1.0 + 4.0 * next();   // star-shaped, so the outline is simple
			c.push_back({r * std::cos(2 * 3.14159265358979323846 * i / n), r * std::sin(2 * 3.14159265358979323846 * i / n)});
		}
		bool ccw = false;
		const TextFace f = triangulateOutlines({c});
		EXPECT_NEAR(faceArea(f, &ccw), polygonArea(c), 1e-7 * polygonArea(c)) << n << " points";
		EXPECT_TRUE(ccw);
		EXPECT_EQ(f.triangles.size(), (c.size() - 2) * 3) << "a simple polygon of n points is n - 2 triangles";
	}
}

TEST(SceneTextTriangulation, ARingOfHolesLikeAnEightOrAColander) {
	TextContour outer = circle(0, 0, 10, 96);
	std::vector<TextContour> all = {outer};
	double holes = 0;
	for (int i = 0; i < 8; ++i) {
		const double a = 2 * 3.14159265358979323846 * i / 8;
		all.push_back(circle(6 * std::cos(a), 6 * std::sin(a), 1.5, 24));
		holes += polygonArea(all.back());
	}
	all.push_back(circle(0, 0, 3, 32));   // and one in the middle
	holes += polygonArea(all.back());
	const TextFace f = triangulateOutlines(all);
	EXPECT_NEAR(faceArea(f), polygonArea(outer) - holes, 1e-7);
}

// A text that makes no letters is reported once, with the reason (the GUI sends it to its log); blank text is meant to be empty and is not.
namespace {
std::vector<std::string>& textLogLines() {
	static std::vector<std::string> lines;
	return lines;
}
bool failingProvider(const TextParams&, TextOutline&) { return false; }
}  // namespace

TEST(SceneTextLog, ATextThatMakesNoLettersIsReportedOnce) {
	textLogLines().clear();
	setTextLog([](const std::string& message) { textLogLines().push_back(message); });
	setTextOutlineProvider(&failingProvider);
	TextParams p;
	p.text = "Hello";
	p.font = "Nowhere Sans";
	EXPECT_EQ(textMesh(p, 1.0, 1.0, 0.3).triangleCount(), 0u);
	EXPECT_EQ(textMesh(p, 1.0, 1.0, 0.3).triangleCount(), 0u);   // (cached: not reported again)
	ASSERT_EQ(textLogLines().size(), 1u);
	EXPECT_NE(textLogLines()[0].find("Hello"), std::string::npos);
	EXPECT_NE(textLogLines()[0].find("Nowhere Sans"), std::string::npos);
	p.text = "   ";
	textMesh(p, 1.0, 1.0, 0.3);
	EXPECT_EQ(textLogLines().size(), 1u) << "blank text is meant to be empty";
	setTextOutlineProvider(nullptr);
	setTextLog(nullptr);
}
