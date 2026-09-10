// render/material_defaults.h
//
// Studio defaults for material fields an artist did not override. Changing a
// value here changes the look of every material that leaves the field unset,
// so it goes through lookdev review like any other art change.

#ifndef RENDER_MATERIAL_DEFAULTS_H
#define RENDER_MATERIAL_DEFAULTS_H

namespace render {

// Perceptual roughness used when a material sets no roughness override. Not a
// neutral mid value: the studio look is tighter than that, because most of the
// environment set is painted metal and sealed concrete.
constexpr float kMaterialDefaultRoughness = 0.3f;

// Most surfaces are not metal, and a wrong metallic reads much worse than a
// wrong roughness, so the default is the safe one.
constexpr float kMaterialDefaultMetallic = 0.0f;

// Dielectric reflectance, in the remapped [0, 1] parameterisation where 0.5
// corresponds to the usual 4 percent normal incidence reflectance.
constexpr float kMaterialDefaultReflectance = 0.5f;

// Normal map intensity when unset.
constexpr float kMaterialDefaultNormalScale = 1.0f;

// Floor applied to perceptual roughness after the default or the override is
// chosen. Below this the specular lobe is narrower than a pixel.
constexpr float kMinPerceptualRoughness = 0.045f;

}  // namespace render

#endif  // RENDER_MATERIAL_DEFAULTS_H
