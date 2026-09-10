// render/ibl_prefilter.cpp
//
// Reflection probe prefiltering.
//
// Each mip of a probe's chain holds the radiance convolved with the GGX lobe
// for one roughness, so that the runtime lookup is a single trilinear sample
// rather than an integral. The mapping from roughness to mip is linear in
// perceptual roughness, which is close enough to linear in lobe width for the
// mip counts we ship and keeps the runtime side trivial.
//
// The sky probe chain is built elsewhere. See sky_prefilter.cpp.

#include "render/ibl_prefilter.h"

#include "render/cube_image.h"
#include "render/sampling.h"

namespace render {
namespace {

// Reflection probe captures. Face size and mip count are fixed because the
// probes live in one cube array that is allocated at level load.
constexpr int kProbeFaceSize = 256;
constexpr int kProbeMipCount = 8;

// Samples per texel in the convolution. Sample count is raised for the rougher
// mips because the lobe covers more of the hemisphere there.
constexpr int kBaseSampleCount = 64;
constexpr int kMaxSampleCount = 512;

float RoughnessForMip(int mip) {
    return static_cast<float>(mip) / static_cast<float>(kProbeMipCount - 1);
}

int SampleCountForMip(int mip) {
    const int count = kBaseSampleCount << mip;
    return count > kMaxSampleCount ? kMaxSampleCount : count;
}

// Van der Corput radical inverse, the second dimension of the Hammersley set.
float RadicalInverseVdc(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

Vec2 Hammersley(int i, int n) {
    return Vec2{static_cast<float>(i) / static_cast<float>(n),
                RadicalInverseVdc(static_cast<uint32_t>(i))};
}

// GGX importance sample around the +Z axis, in alpha, not perceptual space.
Vec3 ImportanceSampleGgx(Vec2 xi, float alpha) {
    const float phi = 2.0f * kPi * xi.x;
    const float cos_theta = Sqrt((1.0f - xi.y) / (1.0f + (alpha * alpha - 1.0f) * xi.y));
    const float sin_theta = Sqrt(1.0f - cos_theta * cos_theta);
    return Vec3{sin_theta * Cos(phi), sin_theta * Sin(phi), cos_theta};
}

}  // namespace

void PrefilterReflectionProbe(const CubeImage& captured, CubeImage* out_chain) {
    out_chain->Allocate(kProbeFaceSize, kProbeMipCount);
    out_chain->CopyMip(0, captured);

    for (int mip = 1; mip < kProbeMipCount; ++mip) {
        const float perceptual = RoughnessForMip(mip);
        const float alpha = perceptual * perceptual;
        const int samples = SampleCountForMip(mip);

        for (int face = 0; face < 6; ++face) {
            const int size = kProbeFaceSize >> mip;
            for (int y = 0; y < size; ++y) {
                for (int x = 0; x < size; ++x) {
                    const Vec3 n = DirectionForTexel(face, x, y, size);
                    Vec3 sum{0.0f, 0.0f, 0.0f};
                    float weight = 0.0f;
                    for (int i = 0; i < samples; ++i) {
                        const Vec3 h = TangentToWorld(n, ImportanceSampleGgx(Hammersley(i, samples), alpha));
                        const Vec3 l = Reflect(n, h);
                        const float n_dot_l = Dot(n, l);
                        if (n_dot_l <= 0.0f) {
                            continue;
                        }
                        sum = sum + captured.SampleDirection(l) * n_dot_l;
                        weight += n_dot_l;
                    }
                    out_chain->Store(face, mip, x, y, weight > 0.0f ? sum / weight : Vec3{});
                }
            }
        }
    }
}

// Linear in perceptual roughness across the probe chain: roughness 0 lands on
// mip 0, roughness 1 lands on the last mip, and the runtime trilinear filter
// covers everything between.
float PrefilterMipForRoughness(float perceptual_roughness) {
    const float mip = perceptual_roughness * static_cast<float>(kProbeMipCount - 1);
    return mip < 0.0f ? 0.0f : mip;
}

}  // namespace render
