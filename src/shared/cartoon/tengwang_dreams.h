#pragma once
// tengwang_dreams.h -- the world inside Wang Bo's mind (tengwang.h, scenes 7 to 10), and the opening's sea of clouds:
//   the sea of clouds     a layer of cloud over the southern mountains at sunrise, peaks standing out of it (a cloud medium cut by a huge sphere whose top is under the camera)
//   the ink world         a sheet of paper with strokes of wet ink that spread (a river, a ridge, a dot that is a bird, a wash that is a storm); the land itself drawn as an
//                         ink painting (no colour, white mist, a paper-white sky), which turns back into the world
//   the pass              a narrow road between rock walls in the rain, pines clinging to them, a small figure on the road; the crossroads on a high plateau, a stone at the fork
//   the palace gate       a great red gate with rows of gold studs at the top of steps in the dark, its leaves closing on an old official, light from within narrowing
//   the general           alone under his great banner in the snow, soldiers marching past
//   the deep water        a stone sinking through shafts of light into the dark
//   the cliff             the crag the eagle rises from
// Each is a set of its own (its own lights and camera medium), apart from the ink world's painted land, which is the real one in other clothes.

#include "tengwang_sets.h"

namespace cartoon {
namespace tengwang {

constexpr int kDreamsVersion = 2;

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The sea of clouds and the peaks of the opening
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The peaks: a tile of high mountains in the south, finer than the valley's own mesh, standing out of the clouds.
constexpr double kPeaksX0 = -24000.0, kPeaksX1 = -10000.0, kPeaksZ0 = -8000.0, kPeaksZ1 = 6000.0;
inline double peakHeight(double x, double z) {
	const double inside = smoothstep(kPeaksX1, kPeaksX1 - 2500.0, x) * smoothstep(kPeaksX0, kPeaksX0 + 2500.0, x) * smoothstep(kPeaksZ0, kPeaksZ0 + 2500.0, z) * smoothstep(kPeaksZ1, kPeaksZ1 - 2500.0, z);
	const double r = ridged(x / 3200.0 + 7.0, z / 3200.0, 61);
	return (std::max)(groundHeight(x, z), 200.0 + inside * (300.0 + 1250.0 * std::pow(r, 1.6) + 300.0 * nature::fbm2(x / 900.0, z / 900.0, 62)));
}
inline Mesh peaksMesh() {
	Mesh m;
	const int nx = 900, nz = 900;
	for (int j = 0; j <= nz; ++j)
		for (int i = 0; i <= nx; ++i) {
			const double x = lerp(kPeaksX0, kPeaksX1, static_cast<double>(i) / nx), z = lerp(kPeaksZ0, kPeaksZ1, static_cast<double>(j) / nz);
			m.verts.push_back({x, peakHeight(x, z), z});
		}
	for (int j = 0; j < nz; ++j)
		for (int i = 0; i < nx; ++i) {
			const int q = j * (nx + 1) + i;
			m.tris.push_back({q, q + nx + 1, q + 1});
			m.tris.push_back({q + 1, q + nx + 1, q + nx + 2});
		}
	for (auto &t : m.tris) {
		const Vec3 n = cross(m.verts[static_cast<std::size_t>(t[1])] - m.verts[static_cast<std::size_t>(t[0])], m.verts[static_cast<std::size_t>(t[2])] - m.verts[static_cast<std::size_t>(t[0])]);
		if (n.y < 0.0) std::swap(t[1], t[2]);
	}
	sdf::computeNormals(m);
	return m;
}
// The peaks: rock above, woods below, a little snow-pale stone on the highest.
inline std::string peaksText() {
	static const std::array<std::string, 3> files = [] {
		std::array<std::string, 3> f;
		static Mesh all, parts[3];
		static bool made = false;
		const auto make = [&]() {
			if (made) return;
			all = peaksMesh();
			for (const auto &t : all.tris) {
				const Vec3 c = (all.verts[static_cast<std::size_t>(t[0])] + all.verts[static_cast<std::size_t>(t[1])] + all.verts[static_cast<std::size_t>(t[2])]) * (1.0 / 3.0);
				const Vec3 n = normalize(all.normals[static_cast<std::size_t>(t[0])] + all.normals[static_cast<std::size_t>(t[1])] + all.normals[static_cast<std::size_t>(t[2])]);
				const int k = c.y > 1350.0 ? 2 : ((n.y < 0.72 || c.y > 900.0 + 120.0 * nature::fbm2(c.x / 500.0, c.z / 500.0, 63)) ? 1 : 0);
				Mesh &m = parts[k];
				const int b = static_cast<int>(m.verts.size());
				for (int c3 = 0; c3 < 3; ++c3) {
					m.verts.push_back(all.verts[static_cast<std::size_t>(t[c3])]);
					m.normals.push_back(all.normals[static_cast<std::size_t>(t[c3])]);
				}
				m.tris.push_back({b, b + 1, b + 2});
			}
			made = true;
		};
		for (int k = 0; k < 3; ++k)
			f[static_cast<std::size_t>(k)] = film_cache::ply(kFilmKey, versioned("peaks_" + std::to_string(k), kDreamsVersion), [&]() {
				make();
				return parts[k];
			});
		return f;
	}();
	return attr(diffuseMaterial({0.07, 0.085, 0.045}), film_cache::plyShape(files[0])) + attr(diffuseMaterial({0.24, 0.21, 0.19}), film_cache::plyShape(files[1])) +
		   attr(diffuseMaterial({0.52, 0.50, 0.48}), film_cache::plyShape(files[2]));
}
// The sea of clouds: a cloud medium in a box 26 km across, `top` metres at its upper edge, cut by a sphere whose top is at `top` (the camera must be above it).
inline std::string cloudSeaText(double top, double density, double scatter, double frequency, Vec3 centre = {-17000.0, 0.0, -1000.0}, const std::string &name = "sea") {
	const double w = 26000.0, depth = top + 250.0;
	char buf[700];
	std::snprintf(buf, sizeof buf,
				  "AttributeBegin\n  Translate %.1f %.1f %.1f\n  Scale %.1f %.1f %.1f\n  MakeNamedMedium \"%s\" \"string type\" [ \"cloud\" ] \"rgb sigma_a\" [ 0 0 0 ] \"rgb sigma_s\" [ %.5f %.5f %.5f ] \"float g\" [ 0.8 ] \"float density\" [ %.3f ] \"float wispiness\" [ 1.2 ] \"float frequency\" [ %.2f ]\nAttributeEnd\n",
				  -(centre.x + w / 2.0), -250.0, centre.z - w / 2.0, w, depth / 0.9, w, name.c_str(), scatter, scatter, scatter, density, frequency);
	// (the box's height: the cloud's density falls with height in the box, (1 - y) * 4.5 * density, so a box whose top is a little above the sphere's leaves the cloud thinning
	// out into billows just under the sphere, rather than cut off flat by it)
	std::string out = buf;
	const double R = 40000.0;
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.1f %.1f %.1f\n  Material \"interface\"\n  MediumInterface \"%s\" \"\"\n  Shape \"sphere\" \"float radius\" [ %.1f ]\nAttributeEnd\n", -centre.x, top - R,
				  centre.z, name.c_str(), R);
	return out + buf;
}

// The sea of clouds as a surface: the tops of the clouds, billowing (domain-warped noise in rounded heaps), soft and translucent, a kilometre up; seen from above, as the
// opening sees them, this reads as cloud and costs a fraction of a cloud medium.
inline double cloudTopHeight(double x, double z) {
	const double wx = nature::fbm2(x / 2600.0 + 3.0, z / 2600.0, 191) - 0.5, wz = nature::fbm2(x / 2600.0, z / 2600.0 + 7.0, 192) - 0.5;
	const double u = x / 900.0 + 1.8 * wx, v = z / 900.0 + 1.8 * wz;
	double h = 0.0;
	// heaps: the max of rounded cells, so the tops are domes and the troughs creases
	for (int o = 0; o < 3; ++o) {
		const double s = std::pow(2.1, o), a = std::pow(0.45, o);
		const double n = nature::fbm2(u * s, v * s, 193 + o);
		h += a * std::pow(n, 1.6);
	}
	return 980.0 + 260.0 * h;
}
inline std::string cloudSurfaceText() {
	static const std::string file = film_cache::ply(kFilmKey, versioned("cloudtops", kDreamsVersion), [] {
		Mesh m;
		const int n = 700;
		for (int j = 0; j <= n; ++j)
			for (int i = 0; i <= n; ++i) {
				const double x = lerp(kPeaksX0 - 6000.0, kPeaksX1 + 4000.0, static_cast<double>(i) / n), z = lerp(kPeaksZ0 - 6000.0, kPeaksZ1 + 6000.0, static_cast<double>(j) / n);
				m.verts.push_back({x, cloudTopHeight(x, z), z});
			}
		for (int j = 0; j < n; ++j)
			for (int i = 0; i < n; ++i) {
				const int q = j * (n + 1) + i;
				m.tris.push_back({q, q + n + 1, q + 1});
				m.tris.push_back({q + 1, q + n + 1, q + n + 2});
			}
		for (auto &t : m.tris) {
			const Vec3 nn = cross(m.verts[static_cast<std::size_t>(t[1])] - m.verts[static_cast<std::size_t>(t[0])], m.verts[static_cast<std::size_t>(t[2])] - m.verts[static_cast<std::size_t>(t[0])]);
			if (nn.y < 0.0) std::swap(t[1], t[2]);
		}
		sdf::computeNormals(m);
		return m;
	});
	return attr("Material \"diffusetransmission\" \"rgb reflectance\" [ 0.92 0.90 0.92 ] \"rgb transmittance\" [ 0.55 0.52 0.58 ]", film_cache::plyShape(file));
}

// Ridges without end: layer after layer of mountain ridges going away into the mist, the way a painter of the south sees the hills (a set of its own, seen from the origin
// looking along -z: layer k is a long band of ridged hills k kilometres and more away, each a little higher than the one before it).
inline std::string ridgeLayersText() {
	static const std::string file = film_cache::ply(kFilmKey, versioned("ridgelayers", kDreamsVersion), [] {
		Mesh all;
		const double distances[] = {900.0, 1700.0, 2700.0, 4000.0, 5800.0, 8200.0, 11500.0};
		for (int k = 0; k < 7; ++k) {
			const double d = distances[k], width = d * 3.2, depth = d * 0.25;
			const int nx = 360, nz = 12;
			Mesh m;
			for (int j = 0; j <= nz; ++j)
				for (int i = 0; i <= nx; ++i) {
					const double u = static_cast<double>(i) / nx, v = static_cast<double>(j) / nz;
					const double x = lerp(-width / 2.0, width / 2.0, u), z = -d - depth * v;
					const double crest = (0.10 + 0.012 * k) * d * (0.45 + 0.55 * ridged(x / (d * 0.35) + 13.0 * k, 0.5 * k, 211 + k));
					// (each band rises from the valley floor to its crest and falls behind it)
					m.verts.push_back({x, crest * std::sin(kPi * std::pow(v, 0.6)) - 30.0, z});
				}
			for (int j = 0; j < nz; ++j)
				for (int i = 0; i < nx; ++i) {
					const int q = j * (nx + 1) + i;
					m.tris.push_back({q, q + nx + 1, q + 1});
					m.tris.push_back({q + 1, q + nx + 1, q + nx + 2});
				}
			for (auto &t : m.tris) {
				const Vec3 n = cross(m.verts[static_cast<std::size_t>(t[1])] - m.verts[static_cast<std::size_t>(t[0])], m.verts[static_cast<std::size_t>(t[2])] - m.verts[static_cast<std::size_t>(t[0])]);
				if (n.y < 0.0) std::swap(t[1], t[2]);
			}
			sdf::computeNormals(m);
			append(all, m, identity());
		}
		return all;
	});
	// (and the floor of the valley between them, so the foot of the picture is dark land in the mist, not the sky)
	Mesh floor;
	floor.verts = {{-40000.0, -25.0, 2000.0}, {40000.0, -25.0, 2000.0}, {40000.0, -25.0, -40000.0}, {-40000.0, -25.0, -40000.0}};
	floor.normals.assign(4, Vec3{0.0, 1.0, 0.0});
	floor.tris = {{0, 2, 1}, {0, 3, 2}};
	return attr(diffuseMaterial({0.05, 0.06, 0.07}), film_cache::plyShape(file)) + attr(diffuseMaterial({0.035, 0.04, 0.045}), emitMesh(floor, identity()));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The ink world
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The sheet: a big plane of warm white paper (its fibres a faint mottle) at y = 0, seen from above.
inline std::string inkPaperText() {
	Mesh sheet;
	const double s = 40.0;
	sheet.verts = {{-s, 0.0, -s}, {s, 0.0, -s}, {s, 0.0, s}, {-s, 0.0, s}};
	sheet.normals.assign(4, Vec3{0.0, 1.0, 0.0});
	sheet.uvs = {{0.0, 0.0}, {60.0, 0.0}, {60.0, 60.0}, {0.0, 60.0}};
	sheet.tris = {{0, 2, 1}, {0, 3, 2}};
	return "AttributeBegin\n  Texture \"fibre\" \"spectrum\" \"checkerboard\" \"rgb tex1\" [ 0.86 0.83 0.76 ] \"rgb tex2\" [ 0.83 0.80 0.73 ]\n  Material \"diffuse\" \"texture reflectance\" [ \"fibre\" ]\n" +
		   emitMesh(sheet, identity()) + "AttributeEnd\n";
}
// A stroke of ink along `path` (on the paper, y = 0), `width(u)` wide, laid as far as `grow` (0..1) along it, its wet edge a little ragged. `tone` is how dark the ink is (1
// black, 0.2 a pale wash of watered ink); `wet` how glossy it still is.
template <typename Path, typename Width>
inline std::string inkStroke(Path path, Width width, double grow, int salt, double wet = 1.0, double tone = 1.0, double lift = 0.002) {
	if (grow <= 0.0) return std::string();
	Mesh m;
	const int n = 160;
	const int last = (std::max)(1, static_cast<int>(n * grow));
	for (int k = 0; k <= last; ++k) {
		const double u = static_cast<double>(k) / n;
		const Vec3 p = path(u), q = path((std::min)(1.0, u + 0.01));
		const Vec3 tdir = normalize(q - p + Vec3{1e-9, 0.0, 0.0});
		const Vec3 side{-tdir.z, 0.0, tdir.x};
		const double ragged = 1.0 + 0.25 * (nature::noise2(u * 40.0, 0.5, salt) - 0.5) + 0.15 * (nature::noise2(u * 140.0, 1.5, salt + 1) - 0.5);
		const double w = width(u) * ragged * (k == last ? 0.6 : 1.0);
		m.verts.push_back(p + side * (w / 2.0) + Vec3{0.0, lift, 0.0});
		m.verts.push_back(p - side * (w / 2.0) + Vec3{0.0, lift, 0.0});
	}
	for (int k = 0; k < last; ++k) {
		const int a = 2 * k;
		m.tris.push_back({a, a + 2, a + 1});
		m.tris.push_back({a + 1, a + 2, a + 3});
	}
	for (auto &t : m.tris) {
		const Vec3 nn = cross(m.verts[static_cast<std::size_t>(t[1])] - m.verts[static_cast<std::size_t>(t[0])], m.verts[static_cast<std::size_t>(t[2])] - m.verts[static_cast<std::size_t>(t[0])]);
		if (nn.y < 0.0) std::swap(t[1], t[2]);
	}
	m.normals.assign(m.verts.size(), Vec3{0.0, 1.0, 0.0});
	const double g = lerp(0.82, 0.012, clamp01(tone));
	char mat[220];
	std::snprintf(mat, sizeof mat, "Material \"coateddiffuse\" \"rgb reflectance\" [ %.4f %.4f %.4f ] \"float roughness\" [ %.3f ]", g, g * 0.97, g * 0.95, lerp(0.4, 0.04, wet));
	return attr(mat, emitMesh(m, identity()));
}
// A soft wash of ink spreading in the paper, `r` across, `darkness` 0..1 at its heart: rings from a pale ragged edge in to the dark middle, so it fades out at its edge as
// watered ink does.
inline std::string inkWash(Vec3 c, double r, double darkness, int salt) {
	std::string out;
	const int rings = 14;
	for (int k = 0; k < rings; ++k) {
		const double s = 1.0 - 0.055 * k;   // each ring a little smaller and darker, laid on the one before
		Mesh m;
		const int n = 72;
		m.verts.push_back(c + Vec3{0.0, 0.0010 + 0.00008 * k, 0.0});
		for (int j = 0; j <= n; ++j) {
			const double a = 2.0 * kPi * j / n;
			const double rr = r * s * (0.78 + 0.4 * nature::fbm2(std::cos(a) * 2.2 + salt + 0.7 * k, std::sin(a) * 2.2, salt));
			m.verts.push_back(c + Vec3{rr * std::cos(a), 0.0010 + 0.00008 * k, rr * std::sin(a)});
		}
		for (int j = 0; j < n; ++j) m.tris.push_back({0, j + 2, j + 1});
		m.normals.assign(m.verts.size(), Vec3{0.0, 1.0, 0.0});
		const double g = lerp(0.84, lerp(0.82, 0.02, darkness), std::pow((k + 1.0) / rings, 1.4));
		out += attr("Material \"diffuse\" \"rgb reflectance\" [ " + fmt3({g, g * 0.98, g * 0.96}) + " ]", emitMesh(m, identity()));
	}
	return out;
}
// A brush stroke as a painter makes it: a bundle of bristle trails, dense in the middle of the stroke and breaking up towards its end (the "flying white" of a dry brush),
// over a pale wet bleed into the paper round it. `path(u)` is the stroke's line on the paper, `width(u)` its width, laid as far as `grow` along it.
template <typename Path, typename Width>
inline std::string inkBrush(Path path, Width width, double grow, int salt, double darkness = 1.0) {
	std::string out = inkStroke(path, [&](double u) { return width(u) * 1.6; }, grow, salt + 1, 0.0, 0.25 * darkness, 0.0015);   // (the wet bleed)
	const int bristles = 9;
	for (int b = 0; b < bristles; ++b) {
		const double across = (b + 0.5) / bristles - 0.5;   // -0.5 .. 0.5 across the stroke
		const double ends = 1.0 - 0.4 * hash01(b, salt + 2) * (std::abs(across) * 2.0);   // the edge bristles run dry first
		const auto bristlePath = [&](double u) {
			const Vec3 p = path(u), q = path((std::min)(1.0, u + 0.01));
			const Vec3 d = normalize(q - p + Vec3{1e-9, 0.0, 0.0});
			return p + Vec3{-d.z, 0.0, d.x} * (across * width(u));
		};
		const auto bristleWidth = [&](double u) {
			const double dry = 1.0 - smoothstep(ends - 0.3, ends, u);
			const double flicker = 0.7 + 0.6 * nature::noise2(u * 30.0, b * 3.1, salt + 3);
			return width(u) / bristles * 1.35 * dry * flicker;
		};
		out += inkStroke(bristlePath, bristleWidth, (std::min)(grow, ends), salt + 10 + b, darkness, darkness * (0.85 + 0.15 * hash01(b, salt + 4)), 0.002 + 0.0001 * b);
	}
	return out;
}
// The paper's light: an even white light from above (a studio), so the paper is white and the ink black.
inline std::string inkStudioLight() { return "LightSource \"infinite\" \"rgb L\" [ 0.95 0.93 0.88 ]\n"; }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The pass in the rain, and the crossroads
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The pass: a valley between two rock walls (story space of its own: the road runs along -z from the origin), the road a pale strip on its floor.
inline double passHeight(double x, double z) {
	const double road = std::abs(x - 6.0 * std::sin(z / 40.0));
	const double walls = std::pow(smoothstep(8.0, 70.0, road), 1.3) * (180.0 + 120.0 * ridged(x / 90.0, z / 90.0, 71));
	return walls + 1.5 * nature::fbm2(x / 6.0, z / 6.0, 72) + 4.0 * nature::fbm2(x / 30.0, z / 30.0, 73) * smoothstep(5.0, 20.0, road);
}
inline std::string passText(double t, double rain) {
	static const std::array<std::string, 2> files = [] {
		std::array<std::string, 2> f;
		static Mesh rock, road;
		static bool made = false;
		const auto make = [&]() {
			if (made) return;
			const int n = 260;
			Mesh all;
			for (int j = 0; j <= n; ++j)
				for (int i = 0; i <= n; ++i) {
					const double x = lerp(-260.0, 260.0, static_cast<double>(i) / n), z = lerp(60.0, -600.0, static_cast<double>(j) / n);
					all.verts.push_back({x, passHeight(x, z), z});
				}
			for (int j = 0; j < n; ++j)
				for (int i = 0; i < n; ++i) {
					const int q = j * (n + 1) + i;
					all.tris.push_back({q, q + n + 1, q + 1});
					all.tris.push_back({q + 1, q + n + 1, q + n + 2});
				}
			for (auto &tr : all.tris) {
				const Vec3 nn = cross(all.verts[static_cast<std::size_t>(tr[1])] - all.verts[static_cast<std::size_t>(tr[0])], all.verts[static_cast<std::size_t>(tr[2])] - all.verts[static_cast<std::size_t>(tr[0])]);
				if (nn.y < 0.0) std::swap(tr[1], tr[2]);
			}
			sdf::computeNormals(all);
			for (const auto &tr : all.tris) {
				const Vec3 c = (all.verts[static_cast<std::size_t>(tr[0])] + all.verts[static_cast<std::size_t>(tr[1])] + all.verts[static_cast<std::size_t>(tr[2])]) * (1.0 / 3.0);
				Mesh &m = std::abs(c.x - 6.0 * std::sin(c.z / 40.0)) < 2.2 ? road : rock;
				const int b = static_cast<int>(m.verts.size());
				for (int k = 0; k < 3; ++k) {
					m.verts.push_back(all.verts[static_cast<std::size_t>(tr[k])]);
					m.normals.push_back(all.normals[static_cast<std::size_t>(tr[k])]);
				}
				m.tris.push_back({b, b + 1, b + 2});
			}
			made = true;
		};
		f[0] = film_cache::ply(kFilmKey, versioned("pass_rock", kDreamsVersion), [&]() { make(); return rock; });
		f[1] = film_cache::ply(kFilmKey, versioned("pass_road", kDreamsVersion), [&]() { make(); return road; });
		return f;
	}();
	std::string out = attr(diffuseMaterial({0.14, 0.14, 0.15}), film_cache::plyShape(files[0])) + attr("Material \"coateddiffuse\" \"rgb reflectance\" [ 0.30 0.28 0.25 ] \"float roughness\" [ 0.1 ]", film_cache::plyShape(files[1]));
	// pines clinging to the walls
	out += treeObjects().back().text();
	for (int k = 0; k < 120; ++k) {
		const double z = lerp(40.0, -580.0, hash01(k, 141)), side = hash01(k, 142) < 0.5 ? -1.0 : 1.0, x = 6.0 * std::sin(z / 40.0) + side * (14.0 + 50.0 * hash01(k, 143));
		const double y = passHeight(x, z);
		if (y > 120.0) continue;
		out += nature::placeObject("tree_pine", mul(translate(x, y - 0.5, z), rotate({0, 1, 0}, 360.0 * hash01(k, 144)), rotate({0, 0, 1}, side * 12.0), scale(0.6 + 0.6 * hash01(k, 145))));
	}
	// the rain: streaks falling past, slanted by the wind
	if (rain > 0.0) {
		std::string drops;
		for (int k = 0; k < 2200; ++k) {
			const double x = -40.0 + 80.0 * hash01(k, 146), z = 30.0 - 160.0 * hash01(k, 147);
			const double y = std::fmod(60.0 - (t * 14.0 + 60.0 * hash01(k, 148)), 60.0);
			const Vec3 a{x + 0.15 * y, y, z}, b = a + Vec3{0.25, -1.1, 0.0};
			drops += bar(a, b, 0.006, 3);
		}
		out += attr("Material \"dielectric\" \"float eta\" [ 1.33 ]", drops);
	}
	return out;
}
// The crossroads: a high grassy plateau, one road coming from the camera and dividing into two (towards the capital, far to the north-west; and south), a stone at the fork.
inline double plateauHeight(double x, double z) { return 3.0 * nature::fbm2(x / 40.0, z / 40.0, 151) + 60.0 * std::pow(smoothstep(400.0, 1600.0, std::hypot(x, z)), 2.0) * ridged(x / 500.0, z / 500.0, 152); }
inline std::string crossroadsText() {
	static const std::array<std::string, 2> files = [] {
		std::array<std::string, 2> f;
		static Mesh grass, road;
		static bool made = false;
		const auto onRoad = [](double x, double z) {
			if (z > 0.0) return std::abs(x) < 1.6;
			const double left = std::abs(x + std::pow(-z, 1.15) * 0.55), right = std::abs(x - std::pow(-z, 1.1) * 0.35);
			return left < 1.4 || right < 1.4;
		};
		const auto make = [&]() {
			if (made) return;
			const int n = 300;
			for (int j = 0; j < n; ++j)
				for (int i = 0; i < n; ++i) {
					// a grid finer near the fork
					const auto at = [](int k) {
						const double u = static_cast<double>(k) / 300.0 * 2.0 - 1.0;
						return 2200.0 * u * std::abs(u) * std::abs(u);
					};
					const double x0 = at(i), x1 = at(i + 1), z0 = at(j), z1 = at(j + 1);
					const Vec3 p[4] = {{x0, plateauHeight(x0, z0), z0}, {x1, plateauHeight(x1, z0), z0}, {x1, plateauHeight(x1, z1), z1}, {x0, plateauHeight(x0, z1), z1}};
					Mesh &m = onRoad((x0 + x1) / 2.0, (z0 + z1) / 2.0) ? road : grass;
					const int b = static_cast<int>(m.verts.size());
					for (const Vec3 &v : p) {
						m.verts.push_back(v);
						m.normals.push_back(Vec3{0.0, 1.0, 0.0});
					}
					m.tris.push_back({b, b + 2, b + 1});
					m.tris.push_back({b, b + 3, b + 2});
				}
			sdf::computeNormals(grass);
			sdf::computeNormals(road);
			made = true;
		};
		f[0] = film_cache::ply(kFilmKey, versioned("cross_grass", kDreamsVersion), [&]() { make(); return grass; });
		f[1] = film_cache::ply(kFilmKey, versioned("cross_road", kDreamsVersion), [&]() { make(); return road; });
		return f;
	}();
	std::string out = attr(diffuseMaterial({0.20, 0.19, 0.10}), film_cache::plyShape(files[0])) + attr(diffuseMaterial({0.36, 0.32, 0.26}), film_cache::plyShape(files[1]));
	out += attr(diffuseMaterial({0.30, 0.29, 0.27}), emitMesh(roundedBox(0.5, 1.6, 0.25, 0.05, 2), translate(1.6, plateauHeight(1.6, -1.0) + 0.75, -1.0)));
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The palace gate (its own place: the gate's middle at the origin, its face towards -z, steps down towards the camera)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline std::string palaceGateText(double close, double t) {
	std::string out;
	// the steps, the platform, the walls either side, the columns and the dark eave above
	Parts p;
	for (int k = 0; k < 12; ++k) addBox(p["marble"], {0.0, 0.18 * k + 0.09, -2.0 - 0.45 * (11 - k) - 0.225}, {16.0, 0.18 * (k + 1), 0.45});
	addBox(p["stone"], {0.0, 1.1, 2.0}, {30.0, 2.2, 8.0});
	addBox(p["red"], {-9.5, 2.2 + 4.5, 0.6}, {11.0, 9.0, 1.2});
	addBox(p["red"], {9.5, 2.2 + 4.5, 0.6}, {11.0, 9.0, 1.2});
	for (const double x : {-4.4, 4.4}) addCylinder(p["red"], {x, 2.2, -0.6}, 8.0, 0.5, 0.46);
	addBrackets(p, 14.0, 1.4, 10.2, 1.0, 1.4);
	addSkirt(p, {13.0, 1.0, 14.0, 17.0, 4.5, 11.4, 1.2, 0.9});
	for (const auto &[key, m] : p.by) out += attr(pavilionMaterial(key, 0.0), emitMesh(m, identity()));
	// the two leaves, turning on their outer edges: open (90 degrees) to closed (0)
	const double angle = 85.0 * (1.0 - close);
	for (const double side : {-1.0, 1.0}) {
		const Mat4 leaf = mul(translate(side * 3.9, 2.2, 0.0), rotate({0, 1, 0}, side * angle));
		std::string door = emitMesh(box(3.8, 7.6, 0.3), mul(leaf, translate(-side * 1.9, 3.8, 0.0)));
		out += attr(lacquer({0.58, 0.05, 0.03}, 0.12), door);
		std::string studs;
		for (int r = 0; r < 9; ++r)
			for (int c = 0; c < 7; ++c) studs += emitMesh(uvSphere(0.075, 10, 6), mul(leaf, translate(-side * (0.4 + 0.5 * c), 0.6 + 0.8 * r, -0.17)));
		studs += emitMesh(torus(0.22, 0.04, 24, 8), mul(leaf, translate(-side * 0.55, 3.6, -0.22), rotate({1, 0, 0}, 90.0)));
		out += attr(goldMat(), studs);
	}
	// the light inside, spilling out through the gap (a warm glow behind the leaves), and the cold of the night outside
	out += emissiveSphere({0.0, 5.0, 6.0}, 1.2, Vec3{1.0, 0.70, 0.35} * (40.0 * (1.0 - 0.8 * close)));
	char buf[200];
	std::snprintf(buf, sizeof buf, "LightSource \"point\" \"point3 from\" [ 0 5 4 ] \"rgb I\" [ %.3f %.3f %.3f ]\n", 260.0 * (1.0 - close), 170.0 * (1.0 - close), 80.0 * (1.0 - close));
	out += buf;
	(void)t;
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The general in the snow (a snowy plain of its own)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline std::string snowFieldText(double t) {
	Mesh ground;
	const double s = 3000.0;
	ground.verts = {{-s, 0.0, -s}, {s, 0.0, -s}, {s, 0.0, s}, {-s, 0.0, s}};
	ground.normals.assign(4, Vec3{0.0, 1.0, 0.0});
	ground.tris = {{0, 2, 1}, {0, 3, 2}};
	std::string out = attr(diffuseMaterial({0.80, 0.82, 0.86}), emitMesh(ground, identity()));
	// the banner: his name on crimson, on a tall pole, cracking in the wind
	if (const std::string image = film_cache::asset("tengwang/banner_li.jpg"); !image.empty()) {
		Mesh cloth;
		const int nu = 16, nv = 20;
		for (int j = 0; j <= nv; ++j)
			for (int i = 0; i <= nu; ++i) {
				const double u = static_cast<double>(i) / nu, v = static_cast<double>(j) / nv;
				cloth.verts.push_back({1.5 + 2.6 * u, 9.6 - 3.9 * v, 0.35 * u * std::sin(u * 6.0 - t * 5.0 + v)});
				cloth.uvs.push_back({u, 1.0 - v});
			}
		for (int j = 0; j < nv; ++j)
			for (int i = 0; i < nu; ++i) {
				const int q = j * (nu + 1) + i;
				cloth.tris.push_back({q, q + 1, q + nu + 2});
				cloth.tris.push_back({q, q + nu + 2, q + nu + 1});
			}
		sdf::computeNormals(cloth);
		out += "AttributeBegin\n  Texture \"banner\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + image + "\" ]\n  Material \"diffuse\" \"texture reflectance\" [ \"banner\" ]\n" + emitMesh(cloth, identity()) +
			   "AttributeEnd\n";
	}
	out += attr(lacquer({0.25, 0.12, 0.05}, 0.3), bar({1.45, 0.0, 0.0}, {1.45, 10.0, 0.0}, 0.07, 8));
	// the snow: flakes drifting down and across
	std::string flakes;
	for (int k = 0; k < 2600; ++k) {
		const double x = -18.0 + 36.0 * hash01(k, 161), z = -24.0 + 40.0 * hash01(k, 162);
		const double y = std::fmod(14.0 - (t * 1.3 + 14.0 * hash01(k, 163)), 14.0);
		flakes += "  AttributeBegin\n  Translate " + fmt3({-(x + 0.6 * std::sin(t * 0.7 + k) + 1.5 * (14.0 - y) / 14.0), y, z}) + "\n  Shape \"sphere\" \"float radius\" [ " +
				  std::to_string(0.018 + 0.02 * hash01(k, 164)) + " ]\n  AttributeEnd\n";
	}
	return out + "AttributeBegin\n  Material \"diffuse\" \"rgb reflectance\" [ 0.95 0.95 0.97 ]\n" + flakes + "AttributeEnd\n";
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The deep water: a stone sinking (a place of its own: the surface at y = 0, the camera below it)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline std::string deepWaterText(Vec3 stone, double t, double spin) {
	std::string out;
	Mesh surface;
	const double s = 400.0;
	surface.verts = {{-s, 0.0, -s}, {s, 0.0, -s}, {s, 0.0, s}, {-s, 0.0, s}};
	surface.normals.assign(4, Vec3{0.0, 1.0, 0.0});
	surface.tris = {{0, 2, 1}, {0, 3, 2}};
	out += attr("Material \"dielectric\" \"float eta\" [ 1.33 ] \"float roughness\" [ 0.06 ]", emitMesh(surface, identity()));
	out += attr(diffuseMaterial({0.02, 0.03, 0.035}), emitMesh(surface, translate(0.0, -60.0, 0.0)));
	out += attr(diffuseMaterial({0.16, 0.15, 0.14}), emitMesh(nature::rock(0.5, 7), mul(translate(stone), rotate({1, 0.3, 0.2}, spin))));
	// the bubbles trailing from it
	std::string bubbles;
	for (int k = 0; k < 40; ++k) {
		const double age = std::fmod(t * 1.5 + k * 0.37, 3.0);
		const Vec3 at = stone + Vec3{0.15 * std::sin(k * 2.1 + t), 0.4 + age * 1.6, 0.15 * std::cos(k * 1.7 + t)};
		if (at.y > -0.1) continue;
		bubbles += "  AttributeBegin\n  Translate " + fmt3({-at.x, at.y, at.z}) + "\n  Shape \"sphere\" \"float radius\" [ " + std::to_string(0.01 + 0.025 * hash01(k, 171)) + " ]\n  AttributeEnd\n";
	}
	return out + "AttributeBegin\n  Material \"dielectric\" \"float eta\" [ 0.75 ]\n" + bubbles + "AttributeEnd\n";
}
// The water as the camera's medium: clear blue-green, darker with depth (the stone vanishes into it).
inline nature::Mist deepWaterMedium(Vec3 eye) {
	nature::Mist m;
	m.centre = eye;
	m.radius = 70.0;
	m.density = 0.03;
	m.tint = {0.55, 0.85, 1.0};
	m.absorption = 0.012;
	m.g = 0.85;
	return m;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The eagle's cliff: a crag standing out into the sky (its own place: the crag's top at the origin)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline std::string cliffText() {
	static const std::string file = film_cache::ply(kFilmKey, versioned("cliff", kDreamsVersion), [] {
		using namespace sdf;
		const auto f = [](Vec3 p) {
			double d = roundCone(p, {0.0, -120.0, 4.0}, {0.0, -1.0, 0.0}, 22.0, 3.5);
			d = smin(d, ell(p, {-6.0, -20.0, 6.0}, {10.0, 22.0, 9.0}), 4.0);
			d += 1.2 * (nature::fbm2(p.x / 4.0 + p.y / 7.0, p.z / 4.0, 181) - 0.5) + 0.5 * (nature::fbm2(p.x, p.z + p.y, 182) - 0.5);
			return d;
		};
		return surfaceNets(f, {-30.0, -125.0, -25.0}, {25.0, 3.0, 30.0}, 0.45);
	});
	return attr(diffuseMaterial({0.20, 0.18, 0.17}), film_cache::plyShape(file));
}

}  // namespace tengwang
}  // namespace cartoon
