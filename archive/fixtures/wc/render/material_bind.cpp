// render/material_bind.cpp
//
// Fills the per draw material constant buffer from a MaterialDesc.
//
// Materials are authored with optional overrides: a field is only written into
// the constant buffer from the descriptor if the matching override bit is set,
// otherwise the studio default from material_defaults.h is used. This is what
// lets an artist ship a material that reads "metal, nothing else specified"
// and still get the shading the lookdev team signed off on.
//
// Nothing here is simulation state. Two peers may bind different materials for
// the same entity, for example at different LOD levels, without desyncing.

#include "render/material_bind.h"

#include "render/material_defaults.h"
#include "render/material_desc.h"
#include "render/shader_constants.h"
#include "render/texture_table.h"

namespace render {
namespace {

float Clamp(float v, float lo, float hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

bool HasOverride(const MaterialDesc& desc, MaterialOverride bit) {
    return (desc.override_mask & static_cast<uint32_t>(bit)) != 0u;
}

// Roughness is authored perceptually. The shader squares it to get the GGX
// alpha, so the floor here is not cosmetic: below it the specular lobe becomes
// narrower than a pixel and aliases badly under any camera motion.
float ResolveRoughness(const MaterialDesc& desc) {
    const float authored = HasOverride(desc, MaterialOverride::kRoughness)
                               ? desc.roughness
                               : kMaterialDefaultRoughness;
    return Clamp(authored, kMinPerceptualRoughness, 1.0f);
}

float ResolveMetallic(const MaterialDesc& desc) {
    const float authored = HasOverride(desc, MaterialOverride::kMetallic)
                               ? desc.metallic
                               : kMaterialDefaultMetallic;
    return Clamp(authored, 0.0f, 1.0f);
}

// Dielectric reflectance at normal incidence. Artists set this on the rare
// materials where the default is wrong, such as gemstones and car glass.
float ResolveReflectance(const MaterialDesc& desc) {
    const float authored = HasOverride(desc, MaterialOverride::kReflectance)
                               ? desc.reflectance
                               : kMaterialDefaultReflectance;
    return Clamp(authored, 0.0f, 1.0f);
}

}  // namespace

void BindMaterial(const MaterialDesc& desc, MaterialConstants* out) {
    out->base_colour = desc.base_colour;
    out->roughness = ResolveRoughness(desc);
    out->metallic = ResolveMetallic(desc);
    out->reflectance = ResolveReflectance(desc);
    out->emissive = desc.emissive;
    out->normal_scale = HasOverride(desc, MaterialOverride::kNormalScale)
                            ? desc.normal_scale
                            : kMaterialDefaultNormalScale;

    out->albedo_texture = TextureTable::Resolve(desc.albedo_texture);
    out->normal_texture = TextureTable::Resolve(desc.normal_texture);
    out->orm_texture = TextureTable::Resolve(desc.orm_texture);

    // Multiplier applied on top of the resolved roughness inside the shader. It
    // exists for the rare material that wants to dial a shared ORM texture up
    // or down. With no override the material carries a neutral 1.0, so the
    // shader sees the resolved roughness unchanged.
    out->roughness_scale = HasOverride(desc, MaterialOverride::kRoughnessScale)
                               ? desc.roughness_scale
                               : 1.0f;
    out->flags = desc.shading_model_flags;
}

// Decals reuse the material path but never write metallic, because they blend
// over whatever is underneath and the underlying surface owns that decision.
void BindDecalMaterial(const MaterialDesc& desc, MaterialConstants* out) {
    BindMaterial(desc, out);
    out->metallic = 0.0f;
    out->flags |= kShadingFlagDecal;
}

}  // namespace render
