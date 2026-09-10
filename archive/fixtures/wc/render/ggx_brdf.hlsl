// render/ggx_brdf.hlsl
//
// The standard opaque BRDF. Cook Torrance with a GGX normal distribution, the
// height correlated Smith visibility term, and a Schlick Fresnel.
//
// Roughness arrives in the material constant buffer as an authored perceptual
// value in the range [0, 1]. Every term below works in alpha, which is that
// value squared. Do not pass perceptual roughness to these functions directly.

#include "render/shader_constants.hlsli"

static const float kPi = 3.14159265359f;

struct MaterialConstants
{
    float3 base_colour;
    float  roughness;        // perceptual, as bound by material_bind.cpp
    float  metallic;
    float  reflectance;
    float  normal_scale;
    float  roughness_scale;
    uint   flags;
};

ConstantBuffer<MaterialConstants> g_material : register(b2);

// Perceptual roughness to GGX alpha.
float GgxAlpha(float perceptual_roughness)
{
    return perceptual_roughness * perceptual_roughness;
}

// GGX / Trowbridge Reitz normal distribution.
float DistributionGgx(float n_dot_h, float alpha)
{
    const float a2 = alpha * alpha;
    const float d = (n_dot_h * a2 - n_dot_h) * n_dot_h + 1.0f;
    return a2 / (kPi * d * d);
}

// Height correlated Smith visibility, already divided by the 4 (n.l)(n.v) of
// the Cook Torrance denominator.
float VisibilitySmithGgxCorrelated(float n_dot_v, float n_dot_l, float alpha)
{
    const float a2 = alpha * alpha;
    const float lambda_v = n_dot_l * sqrt(n_dot_v * n_dot_v * (1.0f - a2) + a2);
    const float lambda_l = n_dot_v * sqrt(n_dot_l * n_dot_l * (1.0f - a2) + a2);
    return 0.5f / max(lambda_v + lambda_l, 1e-5f);
}

float3 FresnelSchlick(float u, float3 f0)
{
    const float f = pow(1.0f - u, 5.0f);
    return f0 + (1.0f.xxx - f0) * f;
}

float3 F0For(float3 base_colour, float metallic, float reflectance)
{
    const float3 dielectric = (0.16f * reflectance * reflectance).xxx;
    return lerp(dielectric, base_colour, metallic);
}

float3 EvaluateBrdf(float3 n, float3 v, float3 l, float3 light_colour)
{
    const float3 h = normalize(v + l);
    const float n_dot_v = saturate(dot(n, v)) + 1e-5f;
    const float n_dot_l = saturate(dot(n, l));
    const float n_dot_h = saturate(dot(n, h));
    const float l_dot_h = saturate(dot(l, h));

    // One place, one conversion. Every specular term below is in alpha.
    const float alpha = GgxAlpha(g_material.roughness * g_material.roughness_scale);

    const float  d = DistributionGgx(n_dot_h, alpha);
    const float  vis = VisibilitySmithGgxCorrelated(n_dot_v, n_dot_l, alpha);
    const float3 f = FresnelSchlick(l_dot_h, F0For(g_material.base_colour,
                                                   g_material.metallic,
                                                   g_material.reflectance));

    const float3 specular = d * vis * f;
    const float3 diffuse = (1.0f.xxx - f) * g_material.base_colour * (1.0f - g_material.metallic) / kPi;

    return (diffuse + specular) * light_colour * n_dot_l;
}
