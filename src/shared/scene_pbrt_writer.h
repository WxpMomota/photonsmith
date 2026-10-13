#pragma once
// scene_pbrt_writer.h -- document -> pbrt-v4 text (toPbrt), and reading a builder file back (fromPbrt).
//
// Part of the Scene Builder's document (scene_document.h includes all of it); header-only, standard library only.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "scene_model.h"
#include "scene_json.h"
#include "scene_shapes.h"
#include "scene_projector.h"
#include "scene_instancing.h"

namespace scene_doc {

namespace detail {

// `normalMap`, when not empty, is the file of a normal map, written as pbrt's own `"string normalmap"` parameter (a plain pbrt-v4 reads that; it has no texture mapping, so the Builder writes it only
// where the placement is not needed or is already in the shape's texture coordinates).
// `textureName`, when not empty, is a Texture already declared: it replaces the flat reflectance of a diffuse or coated material. `displacement`, when not empty, is a Texture
// already declared that bumps (a height map) or perturbs the normal (a normal map) of a diffuse, coated or conductor surface.
// The pbrt type and the parameters (each starting with a space) of a single, unmixed material: what `Material "<type>" ...` and `MakeNamedMaterial "<name>" "string type" [ "<type>" ] ...`
// both take.
inline std::pair<std::string, std::string> materialTypeAndParams(const Material& m, const std::string& textureName, const std::string& displacement, const std::string& normalMap = std::string()) {
	const auto reflectance = [&]() {
		return textureName.empty() ? " \"rgb reflectance\" [ " + rgb(m.color) + " ]" : " \"texture reflectance\" \"" + textureName + "\"";
	};
	const std::string bump = !displacement.empty() ? " \"texture displacement\" \"" + displacement + "\""
	                         : !normalMap.empty() ? " \"string normalmap\" [ " + quoted(pbrtPath(normalMap)) + " ]" : std::string();
	std::string p;
	switch (m.kind) {
		case MaterialKind::Diffuse:
			return {"diffuse", reflectance() + bump};
		case MaterialKind::Conductor:
			return {"conductor", " \"rgb reflectance\" [ " + rgb(m.color) + " ] \"float roughness\" [ " + num(m.roughness) + " ]" + bump};
		case MaterialKind::Dielectric:
			p = " \"float eta\" [ " + num(m.ior) + " ]";
			if (m.roughness > 0.0) p += " \"float roughness\" [ " + num(m.roughness) + " ]";
			return {"dielectric", p};
		case MaterialKind::CoatedDiffuse:
			return {"coateddiffuse", reflectance() + " \"float eta\" [ " + num(m.ior) + " ] \"float roughness\" [ " + num(m.roughness) + " ]" + bump};
		case MaterialKind::Subsurface:
			// pbrt's subsurface from a colour and a mean free path: the renderer inverts the colour into scattering coefficients (the same on every backend).
			p = " \"rgb reflectance\" [ " + rgb(m.color) + " ] \"rgb mfp\" [ " + rgb(m.scatterDistance) + " ] \"float eta\" [ " + num(m.ior) + " ]";
			if (m.roughness > 0.0) p += " \"float roughness\" [ " + num(m.roughness) + " ]";
			return {"subsurface", p};
		case MaterialKind::DiffuseTransmission:
			return {"diffusetransmission", " \"rgb reflectance\" [ " + rgb(m.color) + " ] \"rgb transmittance\" [ " + rgb(m.transmittance) + " ]"};
	}
	return {"diffuse", reflectance()};
}

// A material, as the directive that sets the current one. A mix (Material::mixWith) is two named materials, `mix-<n>-a` (this one, with its picture, pattern and bump) and
// `mix-<n>-b` (the second, plain), and pbrt's "mix" material over them; `mixCounter` numbers them.
inline void writeMaterial(std::ostringstream& os, const Material& m, const std::string& ind, const std::string& textureName = std::string(),
                          const std::string& displacement = std::string(), int* mixCounter = nullptr, const std::string& normalMap = std::string()) {
	const auto first = materialTypeAndParams(m, textureName, displacement, normalMap);
	if (!isMix(m) || !mixCounter) {
		os << ind << "Material \"" << first.first << "\"" << first.second << "\n";
		return;
	}
	const std::string a = "mix-" + std::to_string(++*mixCounter) + "-a", b = "mix-" + std::to_string(*mixCounter) + "-b";
	const auto second = materialTypeAndParams(m.mixWith[0], std::string(), std::string());
	os << ind << "MakeNamedMaterial \"" << a << "\" \"string type\" [ \"" << first.first << "\" ]" << first.second << "\n";
	os << ind << "MakeNamedMaterial \"" << b << "\" \"string type\" [ \"" << second.first << "\" ]" << second.second << "\n";
	os << ind << "Material \"mix\" \"string materials\" [ \"" << a << "\" \"" << b << "\" ] \"float amount\" [ " << num(std::clamp(m.mixAmount, 0.0, 1.0)) << " ]\n";
}

// How an object places its texture coordinates (Material::uvScaleU...): (u, v) -> (u * scaleU - offsetU, v * scaleV - offsetV), only for a material that has a texture to place. The shift is
// in repeats of the texture, so 0.5 moves it half a picture whatever the repeats are.
struct UvPlace {
	double su = 1.0, sv = 1.0, ou = 0.0, ov = 0.0;
	void apply(double& u, double& v) const {
		u = u * su - ou;
		v = v * sv - ov;
	}
};
inline UvPlace uvPlaceOf(const Object& o) {
	UvPlace p;
	if (uvPlaced(o.material) && hasUvTexture(o.material)) p = {o.material.uvScaleU, o.material.uvScaleV, o.material.uvOffsetU, o.material.uvOffsetV};
	return p;
}
// Whether a shape writes its own texture coordinates, which is where the placement of a texture is put for it (UvPlace). The shapes the renderer makes the coordinates of (a sphere, disk,
// cylinder, cone and a mesh file) have the placement written on the Texture instead, as pbrt's imagemap uscale/vscale/udelta/vdelta (textureMappingParams); the renderers read those.
// (Baking stays for the rest because the renderers read that mapping for an imagemap only: a checker on a box, quad or ready-made shape gets its repeats and shift through the uv.)
inline bool uvIsBaked(ShapeKind k) { return k == ShapeKind::Box || k == ShapeKind::Quad || k == ShapeKind::Cone || isGeneratedShape(k); }

// The " \"float uscale\" [ ... ] ..." parameters of an imagemap Texture that place it as the object's material says, for a shape that does not write its own coordinates; empty otherwise.
// pbrt looks the texture up at (uscale * u + udelta, vscale * v + vdelta), and the Scene Builder's placement is u * repeats - shift, so udelta is minus the shift.
inline std::string textureMappingParams(const Object& o) {
	if (uvIsBaked(o.shape) || !uvPlaced(o.material)) return std::string();
	const Material& m = o.material;
	return " \"float uscale\" [ " + num(m.uvScaleU) + " ] \"float vscale\" [ " + num(m.uvScaleV) + " ] \"float udelta\" [ " + num(m.uvOffsetU == 0.0 ? 0.0 : -m.uvOffsetU) +
	       " ] \"float vdelta\" [ " + num(m.uvOffsetV == 0.0 ? 0.0 : -m.uvOffsetV) + " ]";
}

// Writes the pairs of uv numbers `uv` (u, v, u, v...) placed by `place`.
inline void writeUv(std::ostringstream& os, const double* uv, std::size_t count, const UvPlace& place) {
	for (std::size_t i = 0; i + 1 < count; i += 2) {
		double u = uv[i], v = uv[i + 1];
		place.apply(u, v);
		os << (i ? " " : "") << num(u) << " " << num(v);
	}
}

// Box faces (outward winding), 4 vertices each with their own uv so a texture can be applied per face.
inline void writeBox(std::ostringstream& os, const Float3& s, const std::string& ind, const UvPlace& place = UvPlace()) {
	const double hx = s.x / 2, hy = s.y / 2, hz = s.z / 2;
	struct Face { double v[4][3]; };
	const Face faces[6] = {
		{{{ hx, -hy, -hz}, { hx,  hy, -hz}, { hx,  hy,  hz}, { hx, -hy,  hz}}},   // +X
		{{{-hx, -hy,  hz}, {-hx,  hy,  hz}, {-hx,  hy, -hz}, {-hx, -hy, -hz}}},   // -X
		{{{-hx,  hy, -hz}, {-hx,  hy,  hz}, { hx,  hy,  hz}, { hx,  hy, -hz}}},   // +Y
		{{{-hx, -hy,  hz}, {-hx, -hy, -hz}, { hx, -hy, -hz}, { hx, -hy,  hz}}},   // -Y
		{{{-hx, -hy,  hz}, { hx, -hy,  hz}, { hx,  hy,  hz}, {-hx,  hy,  hz}}},   // +Z
		{{{ hx, -hy, -hz}, {-hx, -hy, -hz}, {-hx,  hy, -hz}, { hx,  hy, -hz}}},   // -Z
	};
	os << ind << "Shape \"trianglemesh\" \"integer indices\" [";
	for (int f = 0; f < 6; ++f) os << " " << f * 4 << " " << f * 4 + 1 << " " << f * 4 + 2 << " " << f * 4 << " " << f * 4 + 2 << " " << f * 4 + 3;
	os << " ]\n" << ind << "  \"point3 P\" [";
	for (const Face& f : faces) for (const auto& v : f.v) os << " " << num(v[0]) << " " << num(v[1]) << " " << num(v[2]);
	os << " ]\n" << ind << "  \"point2 uv\" [ ";
	static const double kFaceUv[8] = {0, 0, 1, 0, 1, 1, 0, 1};
	for (int f = 0; f < 6; ++f) {
		if (f) os << " ";
		writeUv(os, kFaceUv, 8, place);
	}
	os << " ]\n";
}

inline ShapeMesh meshOfShape(const Object& o) {
	switch (o.shape) {
		case ShapeKind::Pyramid: return pyramidMesh(o.size.x, o.height, o.size.z);
		case ShapeKind::Wedge: return wedgeMesh(o.size.x, o.size.y, o.size.z);
		case ShapeKind::Stairs: return stairsMesh(o.size.x, o.size.y, o.size.z, o.steps);
		case ShapeKind::Torus: return torusMesh(o.radius, o.radius2);
		case ShapeKind::Capsule: return capsuleMesh(o.radius, o.height);
		case ShapeKind::Dome: return domeMesh(o.radius);
		case ShapeKind::RoundedBox: return roundedBoxMesh(o.size.x, o.size.y, o.size.z, o.radius);
		case ShapeKind::Cyclorama: return cycloramaMesh(o.size.x, o.size.y, o.size.z, o.radius);
		case ShapeKind::Terrain: return terrainMesh(o.size.x, o.size.y, o.size.z, o.steps, o.radius, o.radius2, o.seed);
		case ShapeKind::Tube: return tubeMesh(o.radius, o.radius2, o.height);
		case ShapeKind::Cone: return coneMesh(o.radius, o.height);   // (pbrt-v4 has no cone shape: written as the mesh it is)
		default: break;
	}
	return ShapeMesh();
}

inline void writeGenerated(std::ostringstream& os, const ShapeMesh& m, const std::string& ind, const UvPlace& place = UvPlace()) {
	os << ind << "Shape \"trianglemesh\" \"integer indices\" [";
	for (std::size_t i = 0; i < m.indices.size(); ++i) os << (i % 12 == 0 ? "\n" + ind + "    " : " ") << m.indices[i];
	os << " ]\n" << ind << "  \"point3 P\" [";
	for (std::size_t i = 0; i < m.P.size(); ++i) os << (i % 12 == 0 ? "\n" + ind + "    " : " ") << num(m.P[i]);
	os << " ]\n" << ind << "  \"normal N\" [";
	for (std::size_t i = 0; i < m.N.size(); ++i) os << (i % 12 == 0 ? "\n" + ind + "    " : " ") << num(m.N[i]);
	os << " ]\n" << ind << "  \"point2 uv\" [";
	for (std::size_t i = 0; i + 1 < m.UV.size(); i += 2) {
		double u = m.UV[i], v = m.UV[i + 1];
		place.apply(u, v);
		os << (i % 12 == 0 ? "\n" + ind + "    " : " ") << num(u) << " " << num(v);
	}
	os << " ]\n";
}

// `pictured`: the surface is coloured by a picture. A quad's texture coordinates then run so the picture is upright and not mirrored when the quad is
// stood up facing +Z (rotation X = 90) and seen from +Z, i.e. its top is the quad's -Z edge. (Without a picture they keep the checker's orientation.)
inline void writeShape(std::ostringstream& os, const Object& o, const std::string& ind, bool pictured = false) {
	switch (o.shape) {
		case ShapeKind::Sphere:
			os << ind << "Shape \"sphere\" \"float radius\" [ " << num(o.radius) << " ]\n";
			break;
		case ShapeKind::Box:
			writeBox(os, o.size, ind, uvPlaceOf(o));
			break;
		case ShapeKind::Quad: {
			const double hx = o.size.x / 2, hz = o.size.z / 2;
			// Winding chosen so the front (and the emitting side) faces +Y.
			os << ind << "Shape \"trianglemesh\" \"integer indices\" [ 0 2 1 0 3 2 ]\n"
			   << ind << "  \"point3 P\" [ " << num(-hx) << " 0 " << num(-hz) << "  " << num(hx) << " 0 " << num(-hz) << "  " << num(hx) << " 0 " << num(hz)
			   << "  " << num(-hx) << " 0 " << num(hz) << " ]\n"
			   << ind << "  \"point2 uv\" [ ";
			static const double kPicturedUv[8] = {0, 1, 1, 1, 1, 0, 0, 0}, kPlainUv[8] = {0, 0, 1, 0, 1, 1, 0, 1};
			writeUv(os, pictured ? kPicturedUv : kPlainUv, 8, uvPlaceOf(o));
			os << " ]\n";
			break;
		}
		case ShapeKind::Disk:
			os << ind << "Rotate -90 1 0 0\n" << ind << "Shape \"disk\" \"float radius\" [ " << num(o.radius) << " ]\n";
			break;
		case ShapeKind::Cylinder:
			os << ind << "Rotate -90 1 0 0\n"
			   << ind << "Shape \"cylinder\" \"float radius\" [ " << num(o.radius) << " ] \"float zmin\" [ " << num(-o.height / 2) << " ] \"float zmax\" [ "
			   << num(o.height / 2) << " ]\n";
			break;
		case ShapeKind::Cone:   // (pbrt-v4 has no cone shape: written as the mesh coneMesh() makes of it, with the old cone's own texture coordinates)
		case ShapeKind::Pyramid:
		case ShapeKind::Wedge:
		case ShapeKind::Stairs:
		case ShapeKind::Torus:
		case ShapeKind::Capsule:
		case ShapeKind::Dome:
		case ShapeKind::RoundedBox:
		case ShapeKind::Cyclorama:
		case ShapeKind::Terrain:
		case ShapeKind::Tube:
			writeGenerated(os, meshOfShape(o), ind, uvPlaceOf(o));
			break;
		case ShapeKind::Mesh:
			os << ind << "Scale " << num(o.meshScale) << " " << num(o.meshScale) << " " << num(o.meshScale) << "\n"
			   << ind << "Shape \"plymesh\" \"string filename\" [ " << quoted(pbrtPath(o.meshFile)) << " ]\n";
			break;
	}
}

// A volume object (Volume in scene_model.h): its medium, named volume-N, and the shape carrying it. The shape has pbrt's "interface" material (a surface with no BSDF that
// light passes straight through) and a MediumInterface naming the medium inside and none outside, which is how the renderer's media are given a boundary. The medium's extinction
// is `density` split by the albedo: sigma_s = density * color and sigma_a = density * (1 - color), per channel.
inline void writeVolume(std::ostringstream& os, const Object& o, int n, const std::string& ind) {
	const Volume& v = o.volume;
	const std::string name = "volume-" + std::to_string(n);
	// A cloud only scatters, the same in every colour: the CPU's cloud medium has no absorption or tint (cloud_medium_hittable.h), so the Scene Builder does not offer them.
	const bool cloud = v.kind == VolumeKind::Cloud;
	const Rgb absorbed = cloud ? Rgb{0, 0, 0} : Rgb{v.density * (1.0 - v.color.r), v.density * (1.0 - v.color.g), v.density * (1.0 - v.color.b)};
	const Rgb scattered = cloud ? Rgb{v.density, v.density, v.density} : Rgb{v.density * v.color.r, v.density * v.color.g, v.density * v.color.b};
	if (v.kind == VolumeKind::Cloud) {
		// The cloud's lumpy density fills the unit cube of its medium space, so the medium is declared with the cube scaled and moved to just hold the sphere (the sphere then
		// cuts it round): from -r to r about the object's centre.
		const double r = o.radius;
		os << ind << "AttributeBegin\n"
		   << ind << "  Translate " << num(-r) << " " << num(-r) << " " << num(-r) << "\n"
		   << ind << "  Scale " << num(2 * r) << " " << num(2 * r) << " " << num(2 * r) << "\n"
		   << ind << "  MakeNamedMedium \"" << name << "\" \"string type\" [ \"cloud\" ] \"rgb sigma_a\" [ " << rgb(absorbed) << " ] \"rgb sigma_s\" [ " << rgb(scattered)
		   << " ] \"float g\" [ " << num(v.anisotropy) << " ] \"float density\" [ 1 ] \"float wispiness\" [ " << num(v.wispiness) << " ] \"float frequency\" [ "
		   << num(v.frequency) << " ]\n"
		   << ind << "AttributeEnd\n";
	} else {
		os << ind << "MakeNamedMedium \"" << name << "\" \"string type\" [ \"homogeneous\" ] \"rgb sigma_a\" [ " << rgb(absorbed) << " ] \"rgb sigma_s\" [ " << rgb(scattered)
		   << " ] \"float g\" [ " << num(v.anisotropy) << " ]\n";
	}
	os << ind << "Material \"interface\"\n" << ind << "MediumInterface \"" << name << "\" \"\"\n";
	writeShape(os, o, ind);
}

// The name an instance's template is declared and placed under.
inline std::string instanceName(int t) { return "instance-" + std::to_string(t + 1); }

// The Texture directives an object's material needs (a picture, a pattern or checker, a bump or normal map), written where the stream is, and the names to use for them.
struct ObjectTextures {
	std::string textureName, displacement, normalMapFile;
	bool pictured = false;
};
inline ObjectTextures writeObjectTextures(std::ostringstream& os, const Object& o, int& textureIndex) {
	ObjectTextures t;
	std::string& textureName = t.textureName;
	const bool pictured = !o.material.imageFile.empty() && (o.material.kind == MaterialKind::Diffuse || o.material.kind == MaterialKind::CoatedDiffuse);
	if (pictured) {
		textureName = "picture-" + std::to_string(++textureIndex);
		os << "Texture \"" << textureName << "\" \"spectrum\" \"imagemap\" \"string filename\" [ " << quoted(pbrtPath(o.material.imageFile))
		   << " ]" << textureMappingParams(o) << "\n";
	} else if (o.material.pattern != Pattern::None && !o.material.checker && (o.material.kind == MaterialKind::Diffuse || o.material.kind == MaterialKind::CoatedDiffuse)) {
		textureName = "pattern-" + std::to_string(++textureIndex);
		if (o.material.pattern == Pattern::Marble)
			os << "Texture \"" << textureName << "\" \"spectrum\" \"marble\" \"float scale\" [ " << num(o.material.patternScale) << " ] \"float variation\" [ "
			   << num(o.material.patternVariation) << " ]\n";
		else
			os << "Texture \"" << textureName << "\" \"spectrum\" \"fbm\"\n";
	} else if (o.material.checker && o.material.kind == MaterialKind::Diffuse && o.shape != ShapeKind::Box) {
		textureName = "checks-" + std::to_string(++textureIndex);
		// (on a shape that does not write its own coordinates the repeats multiply the checks; a checkerboard has no shift)
		const bool placedHere = uvPlaced(o.material) && !uvIsBaked(o.shape);
		os << "Texture \"" << textureName << "\" \"spectrum\" \"checkerboard\" \"float uscale\" [ " << num(o.material.checkerCount * (placedHere ? o.material.uvScaleU : 1.0))
		   << " ] \"float vscale\" [ " << num(o.material.checkerCount * (placedHere ? o.material.uvScaleV : 1.0)) << " ] \"rgb tex1\" [ " << rgb(o.material.color)
		   << " ] \"rgb tex2\" [ " << rgb(o.material.color2) << " ]\n";
	}
	// A bump slot: a height map is a float image scaled to its height; a normal map is the colour image itself (the renderer reads either as the material's displacement).
	std::string& displacement = t.displacement;
	std::string& normalMapFile = t.normalMapFile;
	t.pictured = pictured;
	const bool canBump = o.material.kind == MaterialKind::Diffuse || o.material.kind == MaterialKind::CoatedDiffuse || o.material.kind == MaterialKind::Conductor;
	// A normal map is pbrt's own `"string normalmap"` parameter wherever its placement is not needed (none set) or is already in the shape's texture coordinates (a box, a quad, a cone and
	// the ready-made shapes bake it): that is valid pbrt-v4, which a texture slot holding a colour picture is not. Only a sphere, disk, cylinder or mesh file with a placement keeps the
	// texture (this project's renderers read it; pbrt has no mapping for a normal map).
	const bool standardNormalMap = o.material.bumpKind == BumpKind::Normal && (!uvPlaced(o.material) || uvIsBaked(o.shape));
	if (canBump && !o.material.bumpFile.empty() && standardNormalMap) {
		normalMapFile = o.material.bumpFile;
	} else if (canBump && !o.material.bumpFile.empty()) {
		displacement = "bump-" + std::to_string(++textureIndex);
		if (o.material.bumpKind == BumpKind::Height) {
			os << "Texture \"" << displacement << "-image\" \"float\" \"imagemap\" \"string filename\" [ " << quoted(pbrtPath(o.material.bumpFile)) << " ]"
			   << textureMappingParams(o) << "\n";
			os << "Texture \"" << displacement << "\" \"float\" \"scale\" \"texture tex\" [ \"" << displacement << "-image\" ] \"float scale\" [ " << num(o.material.bumpStrength) << " ]\n";
		} else {
			os << "Texture \"" << displacement << "\" \"spectrum\" \"imagemap\" \"string filename\" [ " << quoted(pbrtPath(o.material.bumpFile)) << " ]"
			   << textureMappingParams(o) << "\n";
		}
	}
	return t;
}

// The material and shape of an object (and its area light), after whatever places it: the body of an AttributeBegin for a plain object and of an ObjectBegin for an instance's template.
inline void writeObjectBody(std::ostringstream& os, const Object& o, const std::string& ind, const ObjectTextures& t, int& mixIndex) {
	const std::string& textureName = t.textureName;
	const std::string& displacement = t.displacement;
	const std::string& normalMapFile = t.normalMapFile;
	const bool pictured = t.pictured;
	if (o.emissive) {
		// A pbrt light shape keeps its own material (the back of a one-sided panel, or a lit panel face, shows it), but the CPU and OptiX renderers treat the shape as an emitter only.
		// Writing a black body says what they do, so every renderer agrees (Metal follows pbrt and reflects: a coloured emitter was 15% off the CPU's picture).
		Material body;
		body.kind = MaterialKind::Diffuse;
		body.color = {0.0, 0.0, 0.0};
		writeMaterial(os, body, ind, std::string(), std::string(), &mixIndex);
	} else {
		writeMaterial(os, o.material, ind, textureName, displacement, &mixIndex, normalMapFile);
	}
	if (o.emissive) {
		os << ind << "AreaLightSource \"diffuse\" \"rgb L\" [ " << rgb(scaled(o.emission, o.emissionStrength)) << " ]";
		if (o.twoSided) os << " \"bool twosided\" true";
		os << "\n";
	}
	writeShape(os, o, ind, pictured);
}

}  // namespace detail

// The triangle mesh of one of the generated shapes (isGeneratedShape), in the object's own space; the GUI's previews draw it too.
inline ShapeMesh generatedMesh(const Object& o) { return detail::meshOfShape(o); }

// How big the scene is, in its own units, for the "# @rt-size" line: the largest side of the box around the objects, each counted as a sphere that holds it
// (rotation then cannot matter, and a mesh, whose size the document does not know, counts as its scale). 0 for a document with no objects. Rounded to three
// significant digits. The Live Preview's keyboard step is a fraction of this (src/shared/scene_size.h has the same measure for any pbrt file).
inline double documentSize(const Document& d) {
	if (d.objects.empty()) return 0.0;
	double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
	for (const Object& o : d.objects) {
		const double r = boundingRadius(o);
		const double p[3] = {o.position.x, o.position.y, o.position.z};
		for (int a = 0; a < 3; ++a) {
			lo[a] = std::min(lo[a], p[a] - r);
			hi[a] = std::max(hi[a], p[a] + r);
		}
	}
	const double size = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
	if (!(size > 1e-9) || !std::isfinite(size)) return 0.0;
	const double unit = std::pow(10.0, std::floor(std::log10(size)) - 2.0);
	return std::round(size / unit) * unit;
}

// `instancing` off writes every object on its own (the tests compare the two).
inline std::string toPbrt(const Document& d, bool instancing = true) {
	std::ostringstream os;
	os.imbue(std::locale::classic());
	os << "# " << detail::commentText(d.title) << "\n";
	os << "# Written by the ray_tracer Scene Builder. It is an ordinary pbrt-v4 scene; the line below lets the Scene Builder open it again for editing.\n";
	if (const double size = documentSize(d); size > 0.0) os << "# @rt-size " << detail::num(size) << "\n";
	os << "# @rt-builder-doc " << toJson(d) << "\n\n";

	const Camera& c = d.camera;
	// The document is right-handed (+x to the right of the picture, as the Builder's views draw it); pbrt is left-handed and would draw it mirrored. The mirror before LookAt is the
	// idiom every exporter from a right-handed tool writes: it turns the picture back, in real pbrt and (which reads the camera's own matrix) in this project's renderers.
	os << "Scale -1 1 1\n";
	os << "LookAt " << detail::vec(c.position) << "  " << detail::vec(c.target) << "  " << detail::vec(c.up) << "\n";
	// The document's field of view is the vertical one; pbrt's is the angle across the shorter side of the picture, which is the same on a landscape one and the horizontal angle on a portrait one.
	double writtenFov = c.fov;
	if (d.render.width > 0 && d.render.height > d.render.width)
		writtenFov = 2.0 * std::atan(std::tan(c.fov * 3.14159265358979323846 / 360.0) * static_cast<double>(d.render.width) / d.render.height) * 180.0 / 3.14159265358979323846;
	os << "Camera \"perspective\" \"float fov\" [ " << detail::num(writtenFov) << " ]";
	if (c.lensRadius > 0.0) os << " \"float lensradius\" [ " << detail::num(c.lensRadius) << " ] \"float focaldistance\" [ " << detail::num(c.focusDistance) << " ]";
	os << "\n";
	os << "Film \"rgb\" \"integer xresolution\" [ " << d.render.width << " ] \"integer yresolution\" [ " << d.render.height << " ]\n";
	os << "Sampler \"sobol\" \"integer pixelsamples\" [ " << d.render.samples << " ]\n";
	os << "Integrator \"volpath\" \"integer maxdepth\" [ " << d.render.maxDepth << " ]\n\n";
	os << "WorldBegin\n\n";

	for (const Light& l : d.lights) {
		os << "# " << detail::commentText(l.name) << "\n";
		const Rgb col = detail::scaled(l.color, l.intensity);
		switch (l.kind) {
			case LightKind::Point:
				os << "LightSource \"point\" \"point3 from\" [ " << detail::vec(l.position) << " ] \"rgb I\" [ " << detail::rgb(col) << " ]\n";
				break;
			case LightKind::Spot:
				os << "LightSource \"spot\" \"point3 from\" [ " << detail::vec(l.position) << " ] \"point3 to\" [ " << detail::vec(l.target)
				   << " ] \"float coneangle\" [ " << detail::num(l.coneAngle) << " ] \"float conedeltaangle\" [ " << detail::num(l.coneDelta)
				   << " ] \"rgb I\" [ " << detail::rgb(col) << " ]\n";
				break;
			case LightKind::Distant:
				os << "LightSource \"distant\" \"point3 from\" [ " << detail::vec(l.position) << " ] \"point3 to\" [ " << detail::vec(l.target)
				   << " ] \"rgb L\" [ " << detail::rgb(col) << " ]\n";
				break;
			case LightKind::Projector: {
				// The light looks along +Z of its own space, from the origin. pbrt's projection light puts the slide in its picture turned half way round (its screen runs up and to the
				// right, the picture's rows down and to the left), so the frame's X and Y axes are the slide's left and bottom: a Transform (column-major) puts that frame in the scene
				// and the slide lands as it looks (scene_projector_tests.cpp checks it with renders).
				const projector::Frame f = projector::frameOf(l);
				const Float3 left{-f.right.x, -f.right.y, -f.right.z}, bottom{-f.up.x, -f.up.y, -f.up.z};
				os << "AttributeBegin\n  Transform [ " << detail::vec(left) << " 0  " << detail::vec(bottom) << " 0  " << detail::vec(f.dir) << " 0  " << detail::vec(l.position) << " 1 ]\n"
				   << "  LightSource \"projection\" \"float scale\" [ " << detail::num(l.intensity) << " ] \"float fov\" [ " << detail::num(l.fov) << " ]";
				if (!l.imageFile.empty()) os << " \"string filename\" [ " << detail::quoted(detail::pbrtPath(l.imageFile)) << " ]";
				os << "\nAttributeEnd\n";
				break;
			}
			case LightKind::Infinite:
				if (!l.imageFile.empty()) {
					const bool spun = l.skySpin != 0.0 && !l.physicalSky;   // (the generated sky is tied to its sun: it is not turned)
					if (spun) os << "AttributeBegin\n  Rotate " << detail::num(l.skySpin) << " 0 1 0\n  ";
					os << "LightSource \"infinite\" \"string filename\" [ " << detail::quoted(detail::pbrtPath(l.imageFile)) << " ] \"float scale\" [ "
					   << detail::num(l.intensity) << " ]\n";
					if (spun) os << "AttributeEnd\n";
				}
				else
					os << "LightSource \"infinite\" \"rgb L\" [ " << detail::rgb(col) << " ]\n";
				break;
		}
	}
	if (!d.lights.empty()) os << "\n";

	int textureIndex = 0, mediumIndex = 0, mixIndex = 0;
	// Copies of one heavy shape (a forest from Array or Scatter) are written once, as an ObjectBegin, and each copy places it (scene_instancing.h).
	const InstancePlan plan = instancing ? planInstances(d) : InstancePlan{std::vector<int>(d.objects.size(), -1), std::vector<double>(d.objects.size(), 1.0), {}};
	for (int t = 0; t < plan.templates(); ++t) {
		const Object& src = d.objects[static_cast<std::size_t>(plan.source[static_cast<std::size_t>(t)])];
		int copies = 0;
		for (int id : plan.templateOf) copies += id == t ? 1 : 0;
		os << "# " << detail::commentText(src.name) << ", placed " << copies << " times below\n";
		const detail::ObjectTextures textures = detail::writeObjectTextures(os, src, textureIndex);
		os << "ObjectBegin \"" << detail::instanceName(t) << "\"\n";
		detail::writeObjectBody(os, src, "  ", textures, mixIndex);
		os << "ObjectEnd\n\n";
	}
	for (std::size_t index = 0; index < d.objects.size(); ++index) {
		const Object& o = d.objects[index];
		os << "# " << detail::commentText(o.name) << "\n";
		if (o.volume.kind != VolumeKind::None) {   // fog, smoke or a cloud: a medium, with no material or light of its own
			os << "AttributeBegin\n";
			os << "  Translate " << detail::vec(o.position) << "\n";
			if (o.rotation.z != 0.0) os << "  Rotate " << detail::num(o.rotation.z) << " 0 0 1\n";
			if (o.rotation.y != 0.0) os << "  Rotate " << detail::num(o.rotation.y) << " 0 1 0\n";
			if (o.rotation.x != 0.0) os << "  Rotate " << detail::num(o.rotation.x) << " 1 0 0\n";
			detail::writeVolume(os, o, ++mediumIndex, "  ");
			os << "AttributeEnd\n\n";
			continue;
		}
		const int instance = plan.templateOf[index];
		const detail::ObjectTextures textures = instance >= 0 ? detail::ObjectTextures() : detail::writeObjectTextures(os, o, textureIndex);
		os << "AttributeBegin\n";
		const std::string ind = "  ";
		os << ind << "Translate " << detail::vec(o.position) << "\n";
		if (o.rotation.z != 0.0) os << ind << "Rotate " << detail::num(o.rotation.z) << " 0 0 1\n";
		if (o.rotation.y != 0.0) os << ind << "Rotate " << detail::num(o.rotation.y) << " 0 1 0\n";
		if (o.rotation.x != 0.0) os << ind << "Rotate " << detail::num(o.rotation.x) << " 1 0 0\n";
		if (instance >= 0) {
			const double sc = plan.scale[index];
			if (std::fabs(sc - 1.0) > 1e-9) os << ind << "Scale " << detail::num(sc) << " " << detail::num(sc) << " " << detail::num(sc) << "\n";
			os << ind << "ObjectInstance \"" << detail::instanceName(instance) << "\"\n";
		} else {
			detail::writeObjectBody(os, o, ind, textures, mixIndex);
		}
		os << "AttributeEnd\n\n";
	}
	return os.str();
}

// Reads the document back out of text written by toPbrt(). A .pbrt file without the builder line is not a builder scene.
inline bool fromPbrt(const std::string& text, Document& out, std::string& err) {
	// The marker starts a line (toPbrt() never lets a name put it anywhere else; see detail::commentText).
	const std::string marker = "# @rt-builder-doc ";
	size_t at = text.compare(0, marker.size(), marker) == 0 ? 0 : text.find("\n" + marker);
	if (at != std::string::npos && at != 0) ++at;
	if (at == std::string::npos) {
		err = "this file was not made by the Scene Builder (it has no builder data), so it can be rendered but not edited here";
		return false;
	}
	size_t begin = at + marker.size();
	size_t end = text.find_first_of("\r\n", begin);
	if (end == std::string::npos) end = text.size();
	return fromJson(text.substr(begin, end - begin), out, err);
}

}  // namespace scene_doc
