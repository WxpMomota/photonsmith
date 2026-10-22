#pragma once
// gpu_tessellate.h -- turns the shapes a GPU backend has no intersection routine for into triangles, once, before it converts the scene.
//
// Both GPU backends read the same FlatScene (src/shared/pbrt_flatten.h) and neither can ray-trace every shape in it: Metal has only triangles, spheres, disks and
// cylinders, OptiX adds bilinear patches (and dices curves into patches itself) but has no cone or paraboloid. This is the step that rewrites those shapes as ordinary
// triangles appended to the scene, so area lights, materials, instancing and the scene bounds all see them like any other mesh. Which shapes are rewritten is the
// backend's TessellationCaps: Metal asks for all four kinds, OptiX only for cones and paraboloids (its native patches and curve dicing stay). Moved here from
// gpu/metal/host/metal_poc_pbrt_loader.mm (tessellateUnsupportedShapes) with the numbers unchanged, so a seeded Metal render is byte-identical.
//
// The tessellation is a close approximation, not exact geometry: smooth analytic vertex normals for the quadrics and patches, flat-shaded 8-sided tubes for curves
// (the same dicing density as src/shared/curve_tessellate.h gives OptiX). A shape with Material "interface" only bounds a medium and is skipped (transparent).
//
// Standard library only, so the CPU unit tests can reach it.

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "curve_tessellate.h"
#include "pbrt_flatten.h"

namespace gpu_tessellate {

// Which kinds of shape to rewrite as triangles.
struct TessellationCaps {
	bool bilinearPatches = false;
	bool curves = false;
	bool cones = false;
	bool paraboloids = false;
	// A disk or cylinder whose material draws a picture, a checker or a bump map through the surface's (u, v), and a sphere whose material has a bump or normal map. Metal's analytic
	// disk and cylinder carry no coordinates, so a picture on one would not be drawn at all (and its repeats and shift could not apply), and its analytic sphere has no tangent
	// frame for a bump or normal map; as triangles they get the shape's own (u, v).
	bool texturedQuadrics = false;
	static TessellationCaps all() { return {true, true, true, true, true}; }            // Metal: only triangles and the three analytic shapes it has
	static TessellationCaps conesAndParaboloids() { return {false, false, true, true, false}; }   // OptiX: its patches and curve dicing are native
};

// Triangle index (in scene.triangles after the call) -> the fibre direction of the curve tube it came from, which a hair shader wants.
using FiberTangents = std::unordered_map<int, std::array<float, 3>>;

namespace tessellate_detail {

// Does the material look something up through the surface's (u, v)? A reflectance picture, a checker, a bump / normal map, or a parameter read per pixel (a roughness or a Mix amount
// that is a picture or a graph of textures).
inline bool usesSurfaceCoordinates(const pbrt_flatten::FlatScene& scene, int material) {
    if (material < 0 || material >= (int)scene.materials.size()) return false;
    const pbrt_flatten::Material& m = scene.materials[material];
    return !m.textureFilename.empty() || !m.displacementTextureFilename.empty() || m.hasCheckerReflectance ||
           !m.roughnessTextureFilename.empty() || !m.condRoughnessTextureFilename.empty() || !m.mixWeightTextureFilename.empty() ||
           !m.reflectanceGraph.empty() || !m.roughnessGraph.empty() || !m.condRoughnessGraph.empty() || !m.mixAmountGraph.empty();
}

// A disk or cylinder that Metal should draw as triangles: textured, not a light, not in a medium, not moving.
template <class Quadric>
inline bool tessellateAsTextured(const pbrt_flatten::FlatScene& scene, const Quadric& q) {
    if (q.areaLight >= 0 || q.medium >= 0 || !usesSurfaceCoordinates(scene, q.material)) return false;
    for (int i = 0; i < 16; ++i) if (q.xform[i] != q.xformEnd[i]) return false;
    return true;
}

// A full sphere with a bump or normal map that Metal should draw as triangles: not a light, not in a medium, not clipped, not moving. (A picture or a checker on a sphere is read from
// its position by the analytic sphere already.)
inline bool tessellateSphereWithMap(const pbrt_flatten::FlatScene& scene, const pbrt_flatten::Sphere& sp) {
    if (sp.areaLight >= 0 || sp.medium >= 0 || sp.clipped) return false;
    for (int a = 0; a < 3; ++a) if (sp.center[a] != sp.center1[a]) return false;
    if (sp.material < 0 || sp.material >= (int)scene.materials.size()) return false;
    return !scene.materials[sp.material].displacementTextureFilename.empty();
}

// A clipped sphere that is an area light, for Metal: not moving (a clipped sphere never is), not instanced (those are separate). Its medium, if any, was never drawn (an open shell
// cannot bound one: Sphere::cpuMediumUnsupported), so it does not matter here.
inline bool tessellateClippedSphereLight(const pbrt_flatten::Sphere& sp) {
    return sp.clipped && sp.areaLight >= 0;
}

using pbrt_flatten::quadric_tessellate::kPi;
using pbrt_flatten::quadric_tessellate::normalize3;
using pbrt_flatten::quadric_tessellate::tessellateGrid;

}  // namespace tessellate_detail

// pbrt makes a triangle's geometric normal face the same way as its authored vertex normals (SurfaceInteraction: n = FaceForward(n, shading.n)), whatever the winding, and an emissive mesh
// emits on that side. The GPU backends take the side from the winding, so an emissive triangle that has vertex normals and is wound against them (a lathe-turned sun, a bulb: the cartoon
// films have them) would light the inside of its own mesh. Swapping two of its corners puts the winding with the normals. Returns how many triangles were turned.
inline size_t windEmissiveTrianglesWithTheirNormals(pbrt_flatten::FlatScene& scene) {
	size_t turned = 0;
	for (pbrt_flatten::Triangle& t : scene.triangles) {
		if (t.areaLight < 0 || !t.hasNormals) continue;
		const double e1[3] = {t.v[3] - t.v[0], t.v[4] - t.v[1], t.v[5] - t.v[2]};
		const double e2[3] = {t.v[6] - t.v[0], t.v[7] - t.v[1], t.v[8] - t.v[2]};
		const double g[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
		double d = 0.0;
		for (int a = 0; a < 3; ++a) d += g[a] * (t.n[a] + t.n[3 + a] + t.n[6 + a]);
		if (d >= 0.0) continue;
		for (int a = 0; a < 3; ++a) { std::swap(t.v[3 + a], t.v[6 + a]); std::swap(t.n[3 + a], t.n[6 + a]); }
		std::swap(t.uv[2], t.uv[4]); std::swap(t.uv[3], t.uv[5]);
		++turned;
	}
	return turned;
}

// Rewrites the shapes `caps` names as triangles appended to scene.triangles (and removes them from the scene). Returns how many triangles were added (0 when the
// scene has none of those shapes). `fiberTangent`, if given, receives the fibre direction of each curve triangle.
inline size_t tessellateForBackend(pbrt_flatten::FlatScene& scene, const TessellationCaps& caps, FiberTangents* fiberTangent) {
    std::vector<pbrt_flatten::Triangle> out;
    std::vector<std::pair<size_t, std::array<float, 3>>> outTangents;   // (index into `out`, fibre direction)
    // A shape with `Material "interface"` only bounds a participating medium: skip it (transparent)
    // rather than turning it into an opaque gray mesh - same rule the sphere/disk/cylinder loaders use.
    auto isInterface = [&scene](int m) {
        return m >= 0 && m < (int)scene.materials.size() &&
               scene.materials[m].kind == pbrt_flatten::MaterialKind::Interface;
    };

    // Bilinear patch: p(u,v) = (1-u)(1-v)p00 + u(1-v)p10 + (1-u)v p01 + uv p11; normal = dpdu x dpdv.
    for (const pbrt_flatten::BilinearPatch& bp : scene.bilinearPatches) {
        if (!caps.bilinearPatches) break;
        if (isInterface(bp.material)) continue;
        const double (*P)[3] = bp.p;
        tessellate_detail::tessellateGrid(out, 16, 16, bp.material, bp.areaLight, [&](double u, double v, double* p, double* n) {
            double dpdu[3], dpdv[3];
            for (int a = 0; a < 3; ++a) {
                p[a] = (1 - u) * (1 - v) * P[0][a] + u * (1 - v) * P[1][a] + (1 - u) * v * P[2][a] + u * v * P[3][a];
                dpdu[a] = (1 - v) * (P[1][a] - P[0][a]) + v * (P[3][a] - P[2][a]);
                dpdv[a] = (1 - u) * (P[2][a] - P[0][a]) + u * (P[3][a] - P[1][a]);
            }
            n[0] = dpdu[1] * dpdv[2] - dpdu[2] * dpdv[1];
            n[1] = dpdu[2] * dpdv[0] - dpdu[0] * dpdv[2];
            n[2] = dpdu[0] * dpdv[1] - dpdu[1] * dpdv[0];
            tessellate_detail::normalize3(n);
        });
    }

    // Cone (open, no base cap) and paraboloid: quadric_tessellate.h.
    for (const pbrt_flatten::Cone& c : scene.cones) {
        if (!caps.cones) break;
        if (isInterface(c.material)) continue;
        pbrt_flatten::quadric_tessellate::tessellateCone(c, out);
    }
    for (const pbrt_flatten::Paraboloid& pa : scene.paraboloids) {
        if (!caps.paraboloids) break;
        if (isInterface(pa.material)) continue;
        pbrt_flatten::quadric_tessellate::tessellateParaboloid(pa, out);
    }

    // A textured disk or cylinder (caps.texturedQuadrics): triangles with pbrt's (u, v), so the picture is drawn and its repeats and shift apply; and a sphere with a bump or normal
    // map.
    std::vector<pbrt_flatten::Disk> keptDisks;
    std::vector<pbrt_flatten::Cylinder> keptCylinders;
    std::vector<pbrt_flatten::Sphere> keptSpheres;
    if (caps.texturedQuadrics) {
        for (const pbrt_flatten::Sphere& sp : scene.spheres) {
            if (!isInterface(sp.material) && tessellate_detail::tessellateClippedSphereLight(sp)) { pbrt_flatten::quadric_tessellate::tessellateClippedSphere(sp, out); continue; }
            if (isInterface(sp.material) || !tessellate_detail::tessellateSphereWithMap(scene, sp)) { keptSpheres.push_back(sp); continue; }
            pbrt_flatten::quadric_tessellate::tessellateSphere(sp, out);
        }
        for (const pbrt_flatten::Disk& d : scene.disks) {
            if (isInterface(d.material) || !tessellate_detail::tessellateAsTextured(scene, d)) { keptDisks.push_back(d); continue; }
            pbrt_flatten::quadric_tessellate::tessellateDisk(d, out);
        }
        for (const pbrt_flatten::Cylinder& cy : scene.cylinders) {
            if (isInterface(cy.material) || !tessellate_detail::tessellateAsTextured(scene, cy)) { keptCylinders.push_back(cy); continue; }
            pbrt_flatten::quadric_tessellate::tessellateCylinder(cy, out);
        }
    }

    // Curve: every Bezier segment becomes a tapered 8-sided tube (the same dicing density OptiX
    // uses); width interpolates across the WHOLE curve, as in pbrt-v4. Flat-shaded.
    for (const pbrt_flatten::Curve& cv : scene.curves) {
        if (!caps.curves) break;
        if (isInterface(cv.material)) continue;
        std::vector<curve_tessellate::Quad> quads;
        for (int seg = 0; seg < cv.nSegments; ++seg) {
            float cp[4][3];
            for (int i = 0; i < 4; ++i)
                for (int a = 0; a < 3; ++a) cp[i][a] = (float)cv.cp[((size_t)seg * 4 + i) * 3 + a];
            const double t0 = (double)seg / cv.nSegments, t1 = (double)(seg + 1) / cv.nSegments;
            quads.clear();
            curve_tessellate::tessellate(cp, 0.0f, 1.0f, (float)(cv.width0 + (cv.width1 - cv.width0) * t0),
                                         (float)(cv.width0 + (cv.width1 - cv.width0) * t1), 10, 8, quads);
            for (const curve_tessellate::Quad& q : quads) {
                const float* c4[4] = {q.p00, q.p10, q.p01, q.p11};
                const int tris[2][3] = {{0, 1, 3}, {0, 3, 2}};
                for (const auto& t : tris) {
                    pbrt_flatten::Triangle tri;
                    for (int c = 0; c < 3; ++c)
                        for (int a = 0; a < 3; ++a) tri.v[c * 3 + a] = c4[t[c]][a];
                    tri.material = cv.material;
                    tri.areaLight = cv.areaLight;
                    // Fibre direction = along the tube (ring i -> ring i+1), for the hair shader.
                    {
                        float tx = q.p10[0] - q.p00[0], ty = q.p10[1] - q.p00[1], tz = q.p10[2] - q.p00[2];
                        const float tl = std::sqrt(tx * tx + ty * ty + tz * tz);
                        if (tl > 1e-20f) outTangents.push_back({out.size(), std::array<float, 3>{tx / tl, ty / tl, tz / tl}});
                    }
                    out.push_back(tri);
                }
            }
        }
    }

    const size_t baseIndex = scene.triangles.size();
    if (fiberTangent)
        for (const auto& tp : outTangents) (*fiberTangent)[(int)(baseIndex + tp.first)] = tp.second;
    scene.triangles.insert(scene.triangles.end(), out.begin(), out.end());
    if (caps.bilinearPatches) scene.bilinearPatches.clear();
    if (caps.cones) scene.cones.clear();
    if (caps.paraboloids) scene.paraboloids.clear();
    if (caps.curves) scene.curves.clear();
    if (caps.texturedQuadrics) { scene.disks = std::move(keptDisks); scene.cylinders = std::move(keptCylinders); scene.spheres = std::move(keptSpheres); }
    return out.size();
}


}  // namespace gpu_tessellate
