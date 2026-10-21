#pragma once
// metal_poc_material_params.h -- the host side of the per-family parameter blocks of TriangleMaterial.
//
// `conductorEta`, `conductorK` and `transmitColor` began as a conductor's complex IOR and a diffuse transmittance and became the parameter block of every later
// material family (the loader writes them, the shader reads them). The storage is unchanged; these setters are the only place the loader writes a non-conductor's
// parameters, and the shader reads the same slots through the matching views (the `mat...` functions after TriangleMaterial in metal_poc_types.metal, which also
// has the table of what each family keeps where). A new family adds its setter here and its view there. The texture family (materialType 25, mode in conductorK.y)
// is still written by metal_poc_pbrt_materials.mm directly: its slots mean something different in each mode.
#include "metal_poc_gpu_types.h"

namespace matparams {

// hair (31): betaN, alphaDeg; the fibre tangent (curves) is set per triangle, zero meaning "use the shading normal"
inline void setHair(TriangleMaterial& m, float betaN, float alphaDeg) { m.conductorEta = PackedFloat3{betaN, alphaDeg, 0.0f}; }
inline void setHairTangent(TriangleMaterial& m, float x, float y, float z) { m.conductorK = PackedFloat3{x, y, z}; }
// principled (24)
inline void setPrincipled(TriangleMaterial& m, float metallic, float clearcoat, float clearcoatRoughness) { m.conductorEta = PackedFloat3{metallic, clearcoat, clearcoatRoughness}; }
// dispersive dielectrics (22, 23): the Cauchy pair
inline void setCauchy(TriangleMaterial& m, float a, float b) { m.conductorEta = PackedFloat3{a, b, 0.0f}; }
// measured BRDF (32): the table descriptor's index
inline void setMeasuredTable(TriangleMaterial& m, int index) { m.conductorEta = PackedFloat3{(float)index, 0.0f, 0.0f}; }
// subsurface (34): the BSSRDF table's offset in the shared float buffer (an integer-valued float: a bit-cast int would be a denormal, which the GPU may flush to zero;
// exact below 2^24 floats) and the GGX alpha of a rough entry interface (0 keeps it smooth)
// (a subsurface material whose reflectance is an image sets conductorEta.z = 1, color = the mean free path and transmitColor.x = the picture scale: see PbrtSceneLoader::mapPbrtSubsurfaceMaterial)
inline void setSubsurface(TriangleMaterial& m, int tableOffset, float entryAlpha) { m.conductorEta.x = (float)tableOffset; m.conductorEta.y = entryAlpha; }
// a layered coat over a picture (27): the coat's alpha
inline void setCoatAlpha(TriangleMaterial& m, float alpha) { m.conductorEta = PackedFloat3{alpha, 0.0f, 0.0f}; }
// a coated material's layer (coateddiffuse 19, coatedconductor 20, image clearcoat 27): pbrt's thickness, the layer medium's mean albedo and g, the walk's maximum depth and the samples
// per estimate. These have fields of their own (TriangleMaterial::layer*), not the conductor slots.
inline void setLayerMedium(TriangleMaterial& m, double thickness, double albedo, double g, int maxDepth, int nSamples) {
    m.layerThickness = (float)thickness; m.layerAlbedo = (float)albedo; m.layerG = (float)g; m.layerMaxDepth = maxDepth; m.layerNSamples = nSamples;
}
// the reflectance picture is looked up by a position-based mapping stored at `offset` in rgbGridData (PbrtSceneLoader::claimPositionMapping)
inline void setPositionMapping(TriangleMaterial& m, int offset) { m.posMapIndex = offset + 1; }
// a cloud (29) or RGB grid (30) medium: the index into cloudMediums / rgbGridMediums
inline void setMediumBufferIndex(TriangleMaterial& m, int index) { m.conductorEta = PackedFloat3{(float)index, 0.0f, 0.0f}; }
// a homogeneous medium (28) with per-channel extinction: the hero-channel path (chromatic flag in conductorK.z)
inline void setChromaticMedium(TriangleMaterial& m, float sigmaTr, float sigmaTg, float sigmaTb) {
    m.conductorEta = PackedFloat3{sigmaTr, sigmaTg, sigmaTb};
    m.conductorK = PackedFloat3{0.0f, 0.0f, 1.0f};
}
// a pure absorber (28 with no scattering): per-channel absorption, the chromatic flag left as it is
inline void setMediumAbsorption(TriangleMaterial& m, float sigmaAr, float sigmaAg, float sigmaAb) { m.conductorEta = PackedFloat3{sigmaAr, sigmaAg, sigmaAb}; }
// glass that bounds a medium (dielectric, rough dielectric, thin dielectric on a sphere): per-channel sigma_t, g, has-medium = 1, chromatic flag
inline void setGlassMedium(TriangleMaterial& m, float sigmaTr, float sigmaTg, float sigmaTb, float g, bool chromatic) {
    m.conductorEta = PackedFloat3{sigmaTr, sigmaTg, sigmaTb};
    m.conductorK = PackedFloat3{g, 1.0f, chromatic ? 1.0f : 0.0f};
}

// the views the host needs (the shader's counterparts are matMediumInGlass / matMediumChromatic)
inline bool glassHasMedium(const TriangleMaterial& m) { return m.conductorK.y > 0.5f; }
inline bool mediumIsChromatic(const TriangleMaterial& m) { return m.conductorK.z > 0.5f; }

}  // namespace matparams
