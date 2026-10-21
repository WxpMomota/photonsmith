// metal_poc_loader_tests.mm -- the Metal renderer's pbrt scene loader, run with no GPU.
//
// PbrtSceneLoader (metal_poc_scene_data.h) is the part of the renderer that turns a pbrt file into the host arrays the GPU upload copies (triangles, spheres, disks,
// cylinders, lights, media, camera): plain C++, no Metal device. Until it was split out of MetalPocApp, the only way to run it was a full render, so a loader bug showed
// up as a wrong picture (or not at all). These tests load small bundled scenes and look at the arrays directly: what was loaded, how the materials were mapped, and
// that a bad file leaves the loader empty instead of half-filled. Run from the source directory (the scenes are pbrt_scenes/*.pbrt).
#include "../host/metal_poc_scene_data.h"
#include "../shaders/metal_poc_material_ids.metal"

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::fprintf(stderr, "FAIL: "); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); ++g_failures; } } while (0)

static std::string scenePath(const char* name) { return std::string(RT_SOURCE_DIR) + "/pbrt_scenes/" + name; }

static PbrtSceneLoader* load(const char* name) {
    PbrtSceneLoader* l = new PbrtSceneLoader();
    l->width = 64; l->height = 64;
    l->pbrtScenePath = scenePath(name);
    l->loadPbrtScene();
    return l;
}

static int countMaterial(const std::vector<TriangleMaterial>& v, uint32_t type) {
    int n = 0;
    for (const TriangleMaterial& m : v) n += (m.materialType == type);
    return n;
}

int main() {
    {   // a file that is not there: nothing half-loaded
        PbrtSceneLoader l;
        l.pbrtScenePath = scenePath("no-such-scene.pbrt");
        l.loadPbrtScene();
        CHECK(!l.pbrtMissingAssetsError.empty() || !l.pbrtLoadError.empty(), "a missing file gave no error message (the renderer's @rt event is made from it)");
        CHECK(l.verts.empty() && l.spheres.empty() && l.lights.empty() && !l.havePbrtCamera, "a missing file left %zu vertices, %zu spheres, %zu lights, camera %d",
              l.verts.size(), l.spheres.size(), l.lights.size(), (int)l.havePbrtCamera);
    }
    {   // triangles with a layered material, an area light, a camera
        PbrtSceneLoader* l = load("cornell-coated-diffuse.pbrt");
        CHECK(l->havePbrtCamera, "cornell-coated-diffuse: no camera");
        CHECK(l->verts.size() == 24 * 3 && l->spheres.size() == 1 && l->lights.size() == 1, "cornell-coated-diffuse: %zu vertices, %zu spheres, %zu lights (72, 1, 1 expected)", l->verts.size(), l->spheres.size(), l->lights.size());
        CHECK(l->materials.size() * 3 == l->verts.size(), "cornell-coated-diffuse: %zu materials for %zu vertices (one per triangle)", l->materials.size(), l->verts.size());
        CHECK(countMaterial(l->materials, METAL_MAT_COATED_DIFFUSE) > 0, "cornell-coated-diffuse: no coated-diffuse triangle");
        CHECK(l->pbrtSceneScale > 0.0f && l->pbrtSceneScale != 1.0f, "cornell-coated-diffuse: scene scale %g (a 555-unit box is rescaled)", l->pbrtSceneScale);
        delete l;
    }
    {   // spheres, a participating medium sphere or glass with a medium
        PbrtSceneLoader* l = load("absorbing-fog.pbrt");
        CHECK(l->spheres.size() == 2 && l->sphereMaterials.size() == 2, "absorbing-fog: %zu spheres, %zu sphere materials (2 expected)", l->spheres.size(), l->sphereMaterials.size());
        CHECK(l->havePbrtCamera, "absorbing-fog: no camera");
        delete l;
    }
    {   // disks and cylinders, and a light on one of them
        PbrtSceneLoader* l = load("disk-cylinder-light.pbrt");
        CHECK(!l->disks.empty() && l->disks.size() == l->diskMaterials.size(), "disk-cylinder-light: %zu disks, %zu materials", l->disks.size(), l->diskMaterials.size());
        CHECK(!l->cylinders.empty() && l->cylinders.size() == l->cylinderMaterials.size(), "disk-cylinder-light: %zu cylinders, %zu materials", l->cylinders.size(), l->cylinderMaterials.size());
        delete l;
    }
    {   // hair: the material's parameters go through the named setters (matparams::setHair), the tessellated fibres carry a tangent
        PbrtSceneLoader* l = load("curve-hair-tuft.pbrt");
        CHECK(countMaterial(l->materials, METAL_MAT_HAIR) > 0, "curve-hair-tuft: no hair triangle");
        bool sawBeta = false;
        for (const TriangleMaterial& m : l->materials)
            if (m.materialType == METAL_MAT_HAIR && m.conductorEta.x > 0.0f && m.conductorEta.x <= 1.0f) sawBeta = true;
        CHECK(sawBeta, "curve-hair-tuft: no hair material with a betaN in (0, 1]");
        CHECK(!l->pbrtTriangleFiberTangent.empty(), "curve-hair-tuft: no fibre tangents");
        delete l;
    }
    if (g_failures == 0) std::printf("metal_poc_loader_tests: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
