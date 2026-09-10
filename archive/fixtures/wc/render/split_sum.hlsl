// render/split_sum.hlsl
//
// Runtime half of the split sum approximation.
//
// The specular integral is evaluated as a prefiltered radiance sample times a
// scale and bias pair fetched from the DFG lookup table. The mip to sample is
// not computed here: the CPU picks it with PrefilterMipForRoughness, which has
// one overload per prefiltered chain, and passes it in. Two chains with
// different mip counts therefore share this code unchanged.

#include "render/shader_constants.hlsli"

TextureCube<float3> g_prefiltered : register(t4);
Texture2D<float2>   g_dfg_lut     : register(t5);
SamplerState        g_trilinear   : register(s0);

// Scale and bias for the split sum specular term. The lookup table is
// parameterised by n.v on x and perceptual roughness on y.
float2 SampleDfg(float n_dot_v, float perceptual_roughness)
{
    return g_dfg_lut.SampleLevel(g_trilinear, float2(n_dot_v, perceptual_roughness), 0.0f);
}

// Prefiltered radiance for a reflection direction at an explicit mip.
float3 SamplePrefiltered(float3 reflection_dir, float mip)
{
    return g_prefiltered.SampleLevel(g_trilinear, reflection_dir, mip);
}

// Full specular IBL term.
//
// mip is whatever the CPU side decided for the chain currently bound to t4.
// Passing a mip from the wrong chain is the classic way to get specular that
// is either too sharp or a flat grey, so the binding and the mip are always
// set together.
float3 SpecularIbl(float3 n, float3 v, float perceptual_roughness, float3 f0, float mip)
{
    const float  n_dot_v = saturate(dot(n, v)) + 1e-5f;
    const float3 r = reflect(-v, n);

    const float3 prefiltered = SamplePrefiltered(r, mip);
    const float2 dfg = SampleDfg(n_dot_v, perceptual_roughness);

    return prefiltered * (f0 * dfg.x + dfg.y.xxx);
}

// Diffuse irradiance comes from the three band spherical harmonic set that the
// same capture path produces, evaluated per pixel rather than sampled.
float3 DiffuseIbl(float3 n, float3 albedo, float ao)
{
    const float3 irradiance = EvaluateShIrradiance(n);
    return irradiance * albedo * ao;
}
