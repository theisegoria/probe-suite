// ibl_common.hlsli
//
// Shared helpers for the image based lighting resolve.
//
// The specular cube is prefiltered offline. Mip zero holds the unfiltered
// environment and each successive mip holds the environment convolved with a
// wider GGX lobe, so selecting a mip is the same thing as selecting a lobe
// width and the whole of the specular IBL resolve comes down to picking the
// right one.
//
// Copyright (c) Northlight Interactive.

#ifndef NL_IBL_COMMON_INCLUDED
#define NL_IBL_COMMON_INCLUDED

// Mips in the prefiltered specular cube. Mip zero is the mirror mip.
static const uint kIblMipCount = 9;

// Below this roughness the reflection is treated as a mirror and the lobe
// width is not widened any further.
static const float kMirrorRoughness = 0.02;

// Returns the mip factor for a perceptual roughness, normalised to the range
// zero to one. Zero is the mirror mip and one is the roughest mip. The caller
// scales it by the number of mip steps in the cube.
float RoughnessToMipFactor(float perceptual_roughness)
{
    return perceptual_roughness * float(kIblMipCount - 1);
}

// Horizon occlusion for a reflection vector that points below the shading
// normal. Cheap approximation, applied multiplicatively to the result.
float HorizonOcclusion(float3 reflection, float3 vertex_normal)
{
    const float horizon = saturate(1.0 + dot(reflection, vertex_normal));
    return horizon * horizon;
}

// Diffuse ambient from the irradiance cube. Sampled at mip zero because
// the irradiance cube is already fully convolved.
float3 EvaluateDiffuseIbl(TextureCube<float3> irradiance, SamplerState linear_clamp,
                          float3 normal, float3 albedo, float occlusion)
{
    const float3 e = irradiance.SampleLevel(linear_clamp, normal, 0.0);
    return e * albedo * occlusion;
}

// The dominant specular direction is not the mirror direction on a rough
// surface: the lobe leans toward the normal as roughness rises.
float3 DominantSpecularDirection(float3 normal, float3 reflection,
                                 float perceptual_roughness)
{
    const float lerp_factor = saturate(perceptual_roughness *
                                       (1.0 - perceptual_roughness * 0.5) * 2.0);
    return normalize(lerp(reflection, normal, lerp_factor));
}

#endif  // NL_IBL_COMMON_INCLUDED
