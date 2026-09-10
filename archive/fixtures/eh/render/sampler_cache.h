// sampler_cache.h
//
// Static sampler table for the shared root signature.
//
// Copyright (c) Northlight Interactive. Internal render header.

#pragma once

#include <cstdint>

namespace nl {
namespace render {

// Sampler slots in the shared root signature. Every shader in the engine sees
// the same table at the same registers, so a shader author never declares a
// sampler and never has to think about a descriptor heap.
enum class SamplerSlot : std::uint32_t {
    PointClamp      = 0,
    LinearClamp     = 1,
    LinearWrap      = 2,
    MaterialWrap    = 3,   // the one materials use for albedo and normal maps
    ShadowCompare   = 4,
    Count           = 5,
};

class RootSignatureBuilder;

// Appends every static sampler to the builder, in slot order.
void AppendStaticSamplers(RootSignatureBuilder& builder);

// Human readable description of one slot, for the graphics debugger overlay.
const char* DescribeSamplerSlot(SamplerSlot slot);

}  // namespace render
}  // namespace nl
