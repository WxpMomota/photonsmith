#include "../../src/shared/cartoon/nomai_explorer.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <stdexcept>
using namespace cartoon;
using namespace cartoon::nomai;

static void checkMesh(const Mesh &m) {
    if (m.tris.empty() || m.normals.size() != m.verts.size()) throw std::runtime_error("invalid mesh buffers");
    for (const auto &t : m.tris) {
        for (int i : t) if (i < 0 || static_cast<size_t>(i) >= m.verts.size()) throw std::runtime_error("invalid index");
        const Vec3 a = m.verts[t[0]], b = m.verts[t[1]], c = m.verts[t[2]];
        if (!std::isfinite(length(a)) || length(cross(b-a,c-a)) < 1e-12) throw std::runtime_error("degenerate triangle");
        if (dot(normalize(cross(b-a,c-a)), m.normals[t[0]]) < 0.99) throw std::runtime_error("wrong normal");
    }
}
int main(int argc, char **argv) {
    checkMesh(maskIvoryMesh()); checkMesh(maskBronzeMesh()); checkMesh(maskLensMesh()); checkMesh(maskBackingMesh());
    std::cout << "PASS: angular mask vertices, indices, areas and face normals\n";
    for (double t : {0.0,0.5,1.0,1.5,2.0}) {
        const ExplorerBody b = explorerBody(walkPose(t,t));
        for (int s=0;s<2;++s) if (!std::isfinite(length(b.ball[s]))) throw std::runtime_error("invalid gait");
    }
    std::cout << "PASS: walking skeleton remains finite\n";
    if (argc < 2) return 0;
    const std::filesystem::path out = argv[1];
    std::filesystem::create_directories(out);
    ExplorerPose p = standPose(0.5);
    const std::string model = explorerText(p, identity(), 0.010);
    const char *views[] = {"front", "face", "back", "side"};
    const char *cameras[] = {"3 1.8 5  0 1.15 0", "0.65 1.55 1.7  0 1.47 0.05", "-3 1.8 -5  0 1.15 0", "5 1.8 0  0 1.15 0"};
    for (int i=0;i<4;++i) {
        std::ofstream f(out / (std::string("nomai-aligned-") + views[i] + ".pbrt"));
        f << "Film \"rgb\" \"integer xresolution\" [800] \"integer yresolution\" [800]\nLookAt " << cameras[i] << "  0 1 0\nCamera \"perspective\" \"float fov\" [30]\nWorldBegin\n"
          << "LightSource \"infinite\" \"rgb L\" [0.35 0.35 0.35]\nAttributeBegin\nLightSource \"distant\" \"point3 from\" [3 5 4] \"point3 to\" [0 1 0] \"rgb L\" [2.5 2.3 2.0]\nAttributeEnd\n"
          << "AttributeBegin\nMaterial \"diffuse\" \"rgb reflectance\" [0.22 0.24 0.27]\nShape \"trianglemesh\" \"integer indices\" [0 1 2 0 2 3] \"point3 P\" [-20 -0.01 -20 20 -0.01 -20 20 -0.01 20 -20 -0.01 20]\nAttributeEnd\n"
          << model << "\nWorldEnd\n";
        if (!f) throw std::runtime_error("could not write scene");
    }
    std::cout << "Wrote four studio scenes from the revised repository header\n";
}
