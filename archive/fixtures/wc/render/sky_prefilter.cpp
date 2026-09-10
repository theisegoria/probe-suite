// render/sky_prefilter.cpp
//
// Sky probe prefiltering.
//
// The sky chain is shorter than a reflection probe chain. There is no geometry
// in it, so the rough end of the chain carries almost no detail worth storing,
// and the whole chain is rebuilt whenever the sun moves past the rebuild
// threshold. Cutting the chain down was the single largest saving in the time
// of day update.

#include "render/ibl_prefilter.h"

#include "render/atmosphere.h"
#include "render/cube_image.h"
#include "render/sampling.h"

namespace render {
namespace {

// Sky chain. Shorter than the reflection probe chain on purpose.
constexpr int kSkyPrefilterMipCount = 6;
constexpr int kSkyFaceSize = 128;
constexpr int kSkySampleCount = 128;

float SkyRoughnessForMip(int mip) {
    return static_cast<float>(mip) / static_cast<float>(kSkyPrefilterMipCount - 1);
}

}  // namespace

void PrefilterSkyProbe(const Atmosphere& atmosphere, const SkyProbeDesc& sky, CubeImage* out_chain) {
    out_chain->Allocate(kSkyFaceSize, kSkyPrefilterMipCount);

    for (int mip = 0; mip < kSkyPrefilterMipCount; ++mip) {
        const float perceptual = SkyRoughnessForMip(mip);
        const float alpha = perceptual * perceptual;
        for (int face = 0; face < 6; ++face) {
            const int size = kSkyFaceSize >> mip;
            for (int y = 0; y < size; ++y) {
                for (int x = 0; x < size; ++x) {
                    const Vec3 n = DirectionForTexel(face, x, y, size);
                    out_chain->Store(face, mip, x, y,
                                     ConvolveAtmosphere(atmosphere, sky, n, alpha, kSkySampleCount));
                }
            }
        }
    }
}

// Same shape of mapping as the reflection probe overload, over a different
// number of mips: linear in perceptual roughness from mip 0 to the last mip of
// the sky chain.
float PrefilterMipForRoughness(float perceptual_roughness, const SkyProbeDesc& sky) {
    (void)sky;
    const float mip = perceptual_roughness * static_cast<float>(kSkyPrefilterMipCount - 1);
    return mip < 0.0f ? 0.0f : mip;
}

}  // namespace render
