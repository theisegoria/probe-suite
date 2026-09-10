// ibl_specular.hlsl
//
// Split sum image based specular.
//
// The split sum approximation factors the specular integral into a
// prefiltered environment term and a scale and bias term held in a two
// channel lookup table. This file evaluates both and combines them.
//
// The prefiltered term is a single cube sample. Everything interesting about
// this pass is therefore in the choice of mip, which decides how wide a lobe
// the sample represents. Sampling a mip that is too low gives a rough surface
// a mirror reflection; sampling one that is too high gives a smooth surface a
// blurred one, and on a smooth surface that reads as a material change rather
// than as a filtering artefact.
//
// Copyright (c) Northlight Interactive.

#include "eh/shading/ibl_common.hlsli"

TextureCube<float3> g_SpecularCube    : register(t0);
TextureCube<float3> g_IrradianceCube  : register(t1);
Texture2D<float2>   g_SplitSumLut     : register(t2);

SamplerState g_LinearClamp : register(s0);

cbuffer IblConstants : register(b0)
{
    float3 g_CubeCentre;
    float  g_CubeRadius;
    float  g_IblIntensity;
    float  g_HorizonFade;
};

struct ShadingPoint
{
    float3 world_position;
    float3 world_normal;
    float3 vertex_normal;
    float3 world_view;
    float3 f0;
    float3 albedo;
    float  perceptual_roughness;
    float  occlusion;
};

// Mip level for one shading point.
float SelectIblMip(float perceptual_roughness)
{
    float mip = RoughnessToMipFactor(perceptual_roughness) * float(kIblMipCount - 1);
    mip = min(mip, float(kIblMipCount - 1));
    return mip;
}

float3 EvaluateSpecularIbl(ShadingPoint p)
{
    const float3 n = normalize(p.world_normal);
    const float3 v = normalize(p.world_view);
    const float3 r = reflect(-v, n);
    const float  n_dot_v = saturate(dot(n, v));

    const float3 direction = DominantSpecularDirection(n, r, p.perceptual_roughness);
    const float  mip = SelectIblMip(p.perceptual_roughness);

    float3 prefiltered = g_SpecularCube.SampleLevel(g_LinearClamp, direction, mip);
    prefiltered *= g_IblIntensity;

    const float2 scale_bias = g_SplitSumLut.SampleLevel(
        g_LinearClamp, float2(n_dot_v, p.perceptual_roughness), 0.0);

    float3 specular = prefiltered * (p.f0 * scale_bias.x + scale_bias.y);
    specular *= lerp(1.0, HorizonOcclusion(r, p.vertex_normal), g_HorizonFade);
    return specular;
}

float3 EvaluateIbl(ShadingPoint p)
{
    const float3 diffuse = EvaluateDiffuseIbl(g_IrradianceCube, g_LinearClamp,
                                              normalize(p.world_normal),
                                              p.albedo, p.occlusion);
    return diffuse + EvaluateSpecularIbl(p);
}

// Parallax corrected cube lookup for interior probes. The reflection ray is
// intersected against the probe bounding sphere and the hit point is used as
// the lookup direction, so a reflection inside a room tracks the walls.
float3 ParallaxCorrectDirection(float3 world_position, float3 reflection)
{
    const float3 to_centre = g_CubeCentre - world_position;
    const float  b = dot(to_centre, reflection);
    const float  c = dot(to_centre, to_centre) - g_CubeRadius * g_CubeRadius;
    const float  discriminant = max(b * b - c, 0.0);
    const float  t = b + sqrt(discriminant);
    return normalize(world_position + reflection * t - g_CubeCentre);
}

float4 PS_IblResolve(float4 position : SV_Position, ShadingPoint p) : SV_Target
{
    return float4(EvaluateIbl(p), 1.0);
}

// Debug view. Writes the selected mip directly so a mip that has run past the
// end of the chain shows as a flat plateau at the roughest mip.
float4 PS_DebugIblMip(float4 position : SV_Position, ShadingPoint p) : SV_Target
{
    const float mip = SelectIblMip(p.perceptual_roughness);
    return float4(mip.xxx / float(kIblMipCount - 1), 1.0);
}
