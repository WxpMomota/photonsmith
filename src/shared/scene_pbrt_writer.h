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

namespace scene_doc {

namespace detail {

// `textureName`, when not empty, is a Texture already declared: it replaces the flat reflectance of a diffuse or coated material. `displacement`, when not empty, is a Texture
// already declared that bumps (a height map) or perturbs the normal (a normal map) of a diffuse, coated or conductor surface.
inline void writeMaterial(std::ostringstream& os, const Material& m, const std::string& ind, const std::string& textureName = std::string(),
                          const std::string& displacement = std::string()) {
	const auto reflectance = [&]() {
		return textureName.empty() ? "\"rgb reflectance\" [ " + rgb(m.color) + " ]" : "\"texture reflectance\" \"" + textureName + "\"";
	};
	const std::string bump = displacement.empty() ? std::string() : " \"texture displacement\" \"" + displacement + "\"";
	switch (m.kind) {
		case MaterialKind::Diffuse:
			os << ind << "Material \"diffuse\" " << reflectance() << bump << "\n";
			break;
		case MaterialKind::Conductor:
			os << ind << "Material \"conductor\" \"rgb reflectance\" [ " << rgb(m.color) << " ] \"float roughness\" [ " << num(m.roughness) << " ]" << bump << "\n";
			break;
		case MaterialKind::Dielectric:
			os << ind << "Material \"dielectric\" \"float eta\" [ " << num(m.ior) << " ]";
			if (m.roughness > 0.0) os << " \"float roughness\" [ " << num(m.roughness) << " ]";
			os << "\n";
			break;
		case MaterialKind::CoatedDiffuse:
			os << ind << "Material \"coateddiffuse\" " << reflectance() << " \"float eta\" [ " << num(m.ior) << " ] \"float roughness\" [ "
			   << num(m.roughness) << " ]" << bump << "\n";
			break;
		case MaterialKind::Subsurface:
			// pbrt's subsurface from a colour and a mean free path: the renderer inverts the colour into scattering coefficients (the same on every backend).
			os << ind << "Material \"subsurface\" \"rgb reflectance\" [ " << rgb(m.color) << " ] \"rgb mfp\" [ " << rgb(m.scatterDistance) << " ] \"float eta\" [ " << num(m.ior) << " ]";
			if (m.roughness > 0.0) os << " \"float roughness\" [ " << num(m.roughness) << " ]";
			os << "\n";
			break;
		case MaterialKind::DiffuseTransmission:
			os << ind << "Material \"diffusetransmission\" \"rgb reflectance\" [ " << rgb(m.color) << " ] \"rgb transmittance\" [ " << rgb(m.transmittance)
			   << " ]\n";
			break;
	}
}

// Box faces (outward winding), 4 vertices each with their own uv so a texture can be applied per face.
inline void writeBox(std::ostringstream& os, const Float3& s, const std::string& ind) {
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
	os << " ]\n" << ind << "  \"point2 uv\" [";
	for (int f = 0; f < 6; ++f) os << " 0 0 1 0 1 1 0 1";
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
		case ShapeKind::Tube: return tubeMesh(o.radius, o.radius2, o.height);
		default: break;
	}
	return ShapeMesh();
}

inline void writeGenerated(std::ostringstream& os, const ShapeMesh& m, const std::string& ind) {
	os << ind << "Shape \"trianglemesh\" \"integer indices\" [";
	for (std::size_t i = 0; i < m.indices.size(); ++i) os << (i % 12 == 0 ? "\n" + ind + "    " : " ") << m.indices[i];
	os << " ]\n" << ind << "  \"point3 P\" [";
	for (std::size_t i = 0; i < m.P.size(); ++i) os << (i % 12 == 0 ? "\n" + ind + "    " : " ") << num(m.P[i]);
	os << " ]\n" << ind << "  \"normal N\" [";
	for (std::size_t i = 0; i < m.N.size(); ++i) os << (i % 12 == 0 ? "\n" + ind + "    " : " ") << num(m.N[i]);
	os << " ]\n" << ind << "  \"point2 uv\" [";
	for (std::size_t i = 0; i < m.UV.size(); ++i) os << (i % 12 == 0 ? "\n" + ind + "    " : " ") << num(m.UV[i]);
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
			writeBox(os, o.size, ind);
			break;
		case ShapeKind::Quad: {
			const double hx = o.size.x / 2, hz = o.size.z / 2;
			// Winding chosen so the front (and the emitting side) faces +Y.
			os << ind << "Shape \"trianglemesh\" \"integer indices\" [ 0 2 1 0 3 2 ]\n"
			   << ind << "  \"point3 P\" [ " << num(-hx) << " 0 " << num(-hz) << "  " << num(hx) << " 0 " << num(-hz) << "  " << num(hx) << " 0 " << num(hz)
			   << "  " << num(-hx) << " 0 " << num(hz) << " ]\n"
			   << ind << "  \"point2 uv\" [ " << (pictured ? "0 1 1 1 1 0 0 0" : "0 0 1 0 1 1 0 1") << " ]\n";
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
		case ShapeKind::Cone:
			os << ind << "Rotate -90 1 0 0\n" << ind << "Translate 0 0 " << num(-o.height / 2) << "\n"
			   << ind << "Shape \"cone\" \"float radius\" [ " << num(o.radius) << " ] \"float height\" [ " << num(o.height) << " ]\n";
			break;
		case ShapeKind::Pyramid:
		case ShapeKind::Wedge:
		case ShapeKind::Stairs:
		case ShapeKind::Torus:
		case ShapeKind::Capsule:
		case ShapeKind::Dome:
		case ShapeKind::Tube:
			writeGenerated(os, meshOfShape(o), ind);
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

inline std::string toPbrt(const Document& d) {
	std::ostringstream os;
	os.imbue(std::locale::classic());
	os << "# " << detail::commentText(d.title) << "\n";
	os << "# Written by the ray_tracer Scene Builder. It is an ordinary pbrt-v4 scene; the line below lets the Scene Builder open it again for editing.\n";
	if (const double size = documentSize(d); size > 0.0) os << "# @rt-size " << detail::num(size) << "\n";
	os << "# @rt-builder-doc " << toJson(d) << "\n\n";

	const Camera& c = d.camera;
	os << "LookAt " << detail::vec(c.position) << "  " << detail::vec(c.target) << "  " << detail::vec(c.up) << "\n";
	os << "Camera \"perspective\" \"float fov\" [ " << detail::num(c.fov) << " ]";
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

	int textureIndex = 0, mediumIndex = 0;
	for (const Object& o : d.objects) {
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
		std::string textureName;
		const bool pictured = !o.material.imageFile.empty() && (o.material.kind == MaterialKind::Diffuse || o.material.kind == MaterialKind::CoatedDiffuse);
		if (pictured) {
			textureName = "picture-" + std::to_string(++textureIndex);
			os << "Texture \"" << textureName << "\" \"spectrum\" \"imagemap\" \"string filename\" [ " << detail::quoted(detail::pbrtPath(o.material.imageFile))
			   << " ]\n";
		} else if (o.material.pattern != Pattern::None && !o.material.checker && (o.material.kind == MaterialKind::Diffuse || o.material.kind == MaterialKind::CoatedDiffuse)) {
			textureName = "pattern-" + std::to_string(++textureIndex);
			if (o.material.pattern == Pattern::Marble)
				os << "Texture \"" << textureName << "\" \"spectrum\" \"marble\" \"float scale\" [ " << detail::num(o.material.patternScale) << " ] \"float variation\" [ "
				   << detail::num(o.material.patternVariation) << " ]\n";
			else
				os << "Texture \"" << textureName << "\" \"spectrum\" \"fbm\"\n";
		} else if (o.material.checker && o.material.kind == MaterialKind::Diffuse && o.shape != ShapeKind::Box) {
			textureName = "checks-" + std::to_string(++textureIndex);
			os << "Texture \"" << textureName << "\" \"spectrum\" \"checkerboard\" \"float uscale\" [ " << detail::num(o.material.checkerCount)
			   << " ] \"float vscale\" [ " << detail::num(o.material.checkerCount) << " ] \"rgb tex1\" [ " << detail::rgb(o.material.color)
			   << " ] \"rgb tex2\" [ " << detail::rgb(o.material.color2) << " ]\n";
		}
		// A bump slot: a height map is a float image scaled to its height; a normal map is the colour image itself (the renderer reads either as the material's displacement).
		std::string displacement;
		const bool canBump = o.material.kind == MaterialKind::Diffuse || o.material.kind == MaterialKind::CoatedDiffuse || o.material.kind == MaterialKind::Conductor;
		if (canBump && !o.material.bumpFile.empty()) {
			displacement = "bump-" + std::to_string(++textureIndex);
			if (o.material.bumpKind == BumpKind::Height) {
				os << "Texture \"" << displacement << "-image\" \"float\" \"imagemap\" \"string filename\" [ " << detail::quoted(detail::pbrtPath(o.material.bumpFile)) << " ]\n";
				os << "Texture \"" << displacement << "\" \"float\" \"scale\" \"texture tex\" [ \"" << displacement << "-image\" ] \"float scale\" [ " << detail::num(o.material.bumpStrength) << " ]\n";
			} else {
				os << "Texture \"" << displacement << "\" \"spectrum\" \"imagemap\" \"string filename\" [ " << detail::quoted(detail::pbrtPath(o.material.bumpFile)) << " ]\n";
			}
		}
		os << "AttributeBegin\n";
		const std::string ind = "  ";
		os << ind << "Translate " << detail::vec(o.position) << "\n";
		if (o.rotation.z != 0.0) os << ind << "Rotate " << detail::num(o.rotation.z) << " 0 0 1\n";
		if (o.rotation.y != 0.0) os << ind << "Rotate " << detail::num(o.rotation.y) << " 0 1 0\n";
		if (o.rotation.x != 0.0) os << ind << "Rotate " << detail::num(o.rotation.x) << " 1 0 0\n";
		detail::writeMaterial(os, o.material, ind, textureName, displacement);
		if (o.emissive) {
			os << ind << "AreaLightSource \"diffuse\" \"rgb L\" [ " << detail::rgb(detail::scaled(o.emission, o.emissionStrength)) << " ]";
			if (o.twoSided) os << " \"bool twosided\" true";
			os << "\n";
		}
		detail::writeShape(os, o, ind, pictured);
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
