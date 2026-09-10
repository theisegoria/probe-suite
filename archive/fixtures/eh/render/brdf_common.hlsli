// brdf_common.hlsli
//
// Shared BRDF terms. Included by the forward pass, the deferred lighting pass
// and the image based lighting resolve, so anything added here is paid for in
// three places and should earn it.
//
// Conventions used throughout:
//   perceptual roughness is what artists author and what is stored in the
//   material texture. It is in [0, 1] and it is not linear in anything.
//   alpha is perceptual roughness squared. It is what the GGX distribution
//   actually takes. Mixing the two up is the most common shading bug in the
//   codebase, so every function below names which one it expects.
//
// Copyright (c) Northlight Interactive.

#ifndef NL_BRDF_COMMON_INCLUDED
#define NL_BRDF_COMMON_INCLUDED

static const float kPi = 3.14159265358979f;
static const float kInvPi = 0.31830988618379f;

// The lowest alpha the specular lobe is allowed to reach. A perfectly smooth
// surface produces a delta function that a single sample cannot resolve, and
// the result is a flickering pixel.
static const float kMinAlpha = 0.0064;

// Converts authored perceptual roughness into the GGX alpha parameter.
float AlphaFromPerceptualRoughness(float perceptual_roughness)
{
    return perceptual_roughness * perceptual_roughness;
}

// Inverse of the above. Takes alpha, returns perceptual roughness.
float PerceptualRoughnessFromAlpha(float alpha)
{
    return sqrt(alpha);
}

// Trowbridge Reitz, the GGX normal distribution function. Takes alpha.
float D_GGX(float n_dot_h, float alpha)
{
    const float a2 = alpha * alpha;
    const float d = (n_dot_h * a2 - n_dot_h) * n_dot_h + 1.0;
    return a2 / max(kPi * d * d, 1e-7);
}

// Height correlated Smith visibility. This returns the visibility term, which
// is the geometry term already divided by four times n_dot_l times n_dot_v.
// Multiply it directly with D and F and do not divide again.
float V_SmithGGXCorrelated(float n_dot_v, float n_dot_l, float alpha)
{
    const float a2 = alpha * alpha;
    const float lambda_v = n_dot_l * sqrt(n_dot_v * n_dot_v * (1.0 - a2) + a2);
    const float lambda_l = n_dot_v * sqrt(n_dot_l * n_dot_l * (1.0 - a2) + a2);
    return 0.5 / max(lambda_v + lambda_l, 1e-7);
}

// Schlick Fresnel.
float3 F_Schlick(float3 f0, float v_dot_h)
{
    const float f = pow(1.0 - v_dot_h, 5.0);
    return f0 + (1.0 - f0) * f;
}

float3 DiffuseLambert(float3 albedo)
{
    return albedo * kInvPi;
}

// Direct specular for one light. Takes perceptual roughness and converts.
float3 DirectSpecular(float3 f0, float perceptual_roughness,
                      float n_dot_v, float n_dot_l, float n_dot_h, float v_dot_h)
{
    const float alpha = max(AlphaFromPerceptualRoughness(perceptual_roughness), kMinAlpha);
    const float d = D_GGX(n_dot_h, alpha);
    const float v = V_SmithGGXCorrelated(n_dot_v, n_dot_l, alpha);
    const float3 f = F_Schlick(f0, v_dot_h);
    return d * v * f;
}

#endif  // NL_BRDF_COMMON_INCLUDED
