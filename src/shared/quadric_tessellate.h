#pragma once
// quadric_tessellate.h -- the disk, cylinder, cone and paraboloid as triangles, one shape at a time.
//
// Two users. gpu_tessellate.h rewrites a GPU backend's scene so the shapes it has no intersection routine for (cones and paraboloids on both, everything on Metal)
// become ordinary triangles. The flattener itself uses the same functions for these shapes inside an ObjectBegin/ObjectEnd definition: an instance group holds
// triangles and spheres only, so a cylinder or a disk in an instanced object used to be skipped with a warning on every backend (found by
// scripts/consistency_sweep.py). The numbers are the ones gpu_tessellate.h had, moved unchanged, so a seeded Metal render is byte-identical.
//
// Approximations, not exact geometry: smooth analytic vertex normals; 48 steps round the axis (64 for a cylinder, whose silhouette is the whole shape).
//
// Not standalone: pbrt_flatten.h includes this INSIDE its namespace pbrt_flatten, after flatten_detail's transformPoint/transformNormal, which it uses.

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

// Opens namespace quadric_tessellate inside the namespace pbrt_flatten that includes it.
namespace quadric_tessellate {

constexpr double kPi = 3.14159265358979323846;

// A grid of nu x nv quads over (u, v) in [0,1]^2, two triangles each; `eval(u, v, p, n)` gives the point and its normal.
inline void tessellateGrid(std::vector<Triangle>& out, int nu, int nv, int material, int areaLight,
                           const std::function<void(double, double, double*, double*)>& eval) {
    for (int j = 0; j < nv; ++j) {
        for (int i = 0; i < nu; ++i) {
            const double u0 = (double)i / nu, u1 = (double)(i + 1) / nu;
            const double v0 = (double)j / nv, v1 = (double)(j + 1) / nv;
            double p[4][3], n[4][3];
            const double us[4] = {u0, u1, u0, u1}, vs[4] = {v0, v0, v1, v1};   // p00 p10 p01 p11
            for (int k = 0; k < 4; ++k) eval(us[k], vs[k], p[k], n[k]);
            const int tris[2][3] = {{0, 1, 3}, {0, 3, 2}};
            for (const auto& t : tris) {
                Triangle tri;
                for (int c = 0; c < 3; ++c) {
                    for (int a = 0; a < 3; ++a) { tri.v[c * 3 + a] = p[t[c]][a]; tri.n[c * 3 + a] = n[t[c]][a]; }
                    tri.uv[c * 2 + 0] = us[t[c]]; tri.uv[c * 2 + 1] = vs[t[c]];
                }
                tri.hasNormals = true;
                tri.hasUVs = true;
                tri.material = material;
                tri.areaLight = areaLight;
                out.push_back(tri);
            }
        }
    }
}

inline void normalize3(double* v) {
    const double l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-300) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

inline pbrt_scene::Matrix4 matrixOf(const double* m16) {
    pbrt_scene::Matrix4 xf;
    for (int i = 0; i < 16; ++i) xf.m[i] = m16[i];
    return xf;
}

// Cone (open, no base cap): object space z in [0,height], radius R*(1 - z/height).
inline void tessellateCone(const Cone& c, std::vector<Triangle>& out) {
    const pbrt_scene::Matrix4 xf = matrixOf(c.xform);
    const double phiMax = c.phiMaxDeg * kPi / 180.0;
    tessellateGrid(out, 48, 4, c.material, c.areaLight, [&](double u, double v, double* p, double* n) {
        const double ph = u * phiMax, rad = c.radius * (1.0 - v);
        const double po[3] = {rad * std::cos(ph), rad * std::sin(ph), v * c.height};
        flatten_detail::transformPoint(xf, po[0], po[1], po[2], p);
        flatten_detail::transformNormal(xf, c.height * std::cos(ph), c.height * std::sin(ph), c.radius, n);
        normalize3(n);
    });
}

// Paraboloid: z in [zMin,zMax], radius R*sqrt(z/zMax); outward normal is the gradient of x^2 + y^2 - (R^2/zMax) z.
inline void tessellateParaboloid(const Paraboloid& pa, std::vector<Triangle>& out) {
    const pbrt_scene::Matrix4 xf = matrixOf(pa.xform);
    const double phiMax = pa.phiMaxDeg * kPi / 180.0;
    tessellateGrid(out, 48, 24, pa.material, pa.areaLight, [&](double u, double v, double* p, double* n) {
        const double ph = u * phiMax;
        const double z = pa.zMin + v * (pa.zMax - pa.zMin);
        const double rad = pa.radius * std::sqrt(std::max(z, 0.0) / pa.zMax);
        const double x = rad * std::cos(ph), y = rad * std::sin(ph);
        flatten_detail::transformPoint(xf, x, y, z, p);
        flatten_detail::transformNormal(xf, 2.0 * x, 2.0 * y, -pa.radius * pa.radius / pa.zMax, n);
        normalize3(n);
    });
}

// Cylinder (an open tube): object space radius R about the z axis, z in [zMin,zMax], outward normals.
inline void tessellateCylinder(const Cylinder& cy, std::vector<Triangle>& out) {
    const pbrt_scene::Matrix4 xf = matrixOf(cy.xform);
    const double phiMax = cy.phiMaxDeg * kPi / 180.0;
    tessellateGrid(out, 64, 1, cy.material, cy.areaLight, [&](double u, double v, double* p, double* n) {
        const double ph = u * phiMax;
        flatten_detail::transformPoint(xf, cy.radius * std::cos(ph), cy.radius * std::sin(ph), cy.zMin + v * (cy.zMax - cy.zMin), p);
        flatten_detail::transformNormal(xf, std::cos(ph), std::sin(ph), 0.0, n);
        normalize3(n);
    });
}

// Disk (an annulus in the plane z = height, normal +z): inner radius to radius, phiMax round. As pbrt's own disk: u = phi / phiMax, v runs from 0 at the
// outer rim to 1 at the inner radius.
inline void tessellateDisk(const Disk& d, std::vector<Triangle>& out) {
    const pbrt_scene::Matrix4 xf = matrixOf(d.xform);
    const double phiMax = d.phiMaxDeg * kPi / 180.0;
    tessellateGrid(out, 48, 2, d.material, d.areaLight, [&](double u, double v, double* p, double* n) {
        const double ph = u * phiMax, rad = d.radius - v * (d.radius - d.innerRadius);
        // 1e-4 of the radius above the plane: a disk lying exactly on another surface (a Scene Builder disk on the floor) would tie with it triangle against triangle, and
        // the floor won in patches. The lift is far below anything visible.
        flatten_detail::transformPoint(xf, rad * std::cos(ph), rad * std::sin(ph), d.height + 1e-4 * d.radius, p);
        flatten_detail::transformNormal(xf, 0.0, 0.0, 1.0, n);
        normalize3(n);
    });
}

// A full sphere with pbrt's (u, v), taken about the sphere's own z axis. Sphere::uvRot carries a world direction into the frame of sphere.h's older get_sphere_uv, in which the poles are
// on the y axis, phi = 2 pi u = atan2(-z, x) + pi and theta = pi v = acos(-y / r), i.e. direction = (-sin(theta) cos(phi), -cos(theta), sin(theta) sin(phi)); the vertices take
// that direction back to the world through uvRot's transpose. 96 x 48 cells: the silhouette is 5e-4 of the radius off at worst. Not a clipped sphere (it has its own transform
// and parameters), not a moving one.
inline void tessellateSphere(const Sphere& sp, std::vector<Triangle>& out) {
    tessellateGrid(out, 96, 48, sp.material, sp.areaLight, [&](double u, double v, double* p, double* n) {
        const double phi = 2.0 * kPi * u, theta = kPi * v;
        const double s = std::sin(theta);
        const double d[3] = {-s * std::cos(phi), -std::cos(theta), s * std::sin(phi)};   // the direction in the frame the project's older (u, v) are defined in
        // Sphere::uvRot carries a world direction into that frame, so its transpose (it is a rotation or a mirror) carries the direction back
        for (int a = 0; a < 3; ++a) n[a] = sp.uvRot[a] * d[0] + sp.uvRot[3 + a] * d[1] + sp.uvRot[6 + a] * d[2];
        for (int a = 0; a < 3; ++a) p[a] = sp.center[a] + sp.radius * n[a];
    });
}

}  // namespace quadric_tessellate
