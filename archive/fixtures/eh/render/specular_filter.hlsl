// specular_filter.hlsl
//
// Specular antialiasing and prefiltered environment lookup.
//
// A pixel that covers many normals cannot be shaded with one normal. The
// standard fix is to widen the specular lobe by the variance of the normal
// distribution inside the pixel, which turns geometric aliasing into a
// slightly blurrier highlight. The widening happens in alpha, not in
// perceptual roughness, because alpha is the parameter the lobe is linear in.
//
// The chain, in the order the code runs it:
//
//   perceptual roughness  -> alpha                       (square)
//   alpha                 -> widened alpha               (add variance term)
//   widened alpha         -> filtered perceptual roughness (square root)
//   filtered roughness    -> mip level                   (scale and bias)
//
// Every step is reversible on paper and none of them commute. Squaring after
// adding the variance term is not the same as adding it after squaring, and
// the difference is visible on a distant handrail.
//
// Copyright (c) Northlight Interactive.

#include "eh/render/brdf_common.hlsli"

// Number of mips in the prefiltered specular cube. Mip zero is the mirror
// mip, so the roughest mip index is one less than this.
static const uint kSpecularMipCount = 10;

// How hard the normal variance widens the lobe. Tuned by eye on the foliage
// and handrail test scenes.
static const float kSpecularAAStrength = 3.0;

// Sharpening bias subtracted from the computed mip. The prefilter convolution
// is slightly wider than the analytic lobe, so sampling a fraction of a mip
// sharper than the analytic answer matches the reference better.
static const float kSpecularMipBias = 0.25;

TextureCube<float3> g_SpecularCube : register(t0);
SamplerState        g_TrilinearClamp : register(s0);

Texture2D<float2>   g_BrdfLut : register(t1);
SamplerState        g_LutClamp : register(s1);

struct SurfaceInput
{
    float3 world_normal;
    float3 world_view;
    float3 f0;
    float  perceptual_roughness;
    float  normal_variance;   // second moment of the normal inside this pixel
};

// Widen the lobe by the normal variance and return the filtered perceptual
// roughness. Saturating the widened alpha is what keeps a pixel full of
// wildly varying normals from producing an alpha above one, which the GGX
// distribution does not accept.
float FilteredPerceptualRoughness(float perceptual_roughness, float normal_variance)
{
    float alpha = AlphaFromPerceptualRoughness(perceptual_roughness);
    alpha = saturate(alpha + kSpecularAAStrength * normal_variance);
    return PerceptualRoughnessFromAlpha(alpha);
}

// Map a filtered perceptual roughness onto an absolute mip level in the
// prefiltered cube, then bias and clamp it into range.
float SpecularMipLevel(float filtered_perceptual_roughness)
{
    float mip = filtered_perceptual_roughness * float(kSpecularMipCount - 1);
    mip = mip - kSpecularMipBias;
    return clamp(mip, 0.0, float(kSpecularMipCount - 1));
}

// The whole chain for one surface point.
float SelectSpecularMip(SurfaceInput s)
{
    const float filtered = FilteredPerceptualRoughness(s.perceptual_roughness,
                                                       s.normal_variance);
    return SpecularMipLevel(filtered);
}

// Split sum image based lighting. The prefiltered cube supplies the first
// factor and the lookup table supplies the second.
float3 EvaluateSpecularIBL(SurfaceInput s)
{
    const float3 n = normalize(s.world_normal);
    const float3 v = normalize(s.world_view);
    const float3 r = reflect(-v, n);
    const float  n_dot_v = saturate(dot(n, v));

    const float  mip = SelectSpecularMip(s);
    const float3 prefiltered = g_SpecularCube.SampleLevel(g_TrilinearClamp, r, mip);

    const float filtered = FilteredPerceptualRoughness(s.perceptual_roughness,
                                                       s.normal_variance);
    const float2 ab = g_BrdfLut.SampleLevel(g_LutClamp, float2(n_dot_v, filtered), 0.0);

    return prefiltered * (s.f0 * ab.x + ab.y);
}

// Debug visualisation. Writes the selected mip as a normalised grey so the
// artists can see the transition bands in the viewport.
float4 PS_DebugMip(float4 position : SV_Position, SurfaceInput s) : SV_Target
{
    const float mip = SelectSpecularMip(s);
    const float grey = mip / float(kSpecularMipCount - 1);
    return float4(grey, grey, grey, 1.0);
}
