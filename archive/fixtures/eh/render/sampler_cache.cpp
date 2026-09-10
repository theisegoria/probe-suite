// sampler_cache.cpp
//
// The static sampler table.
//
// Static samplers are baked into the root signature rather than bound from a
// heap. They cost nothing at draw time and they cannot be got wrong by a
// shader author, which between them are worth the loss of runtime control.
//
// Convention in this file: a sampler whose behaviour the renderer depends on
// is spelled out parameter by parameter, so that reading the table tells you
// exactly what the sampler does. A sampler that wants the ordinary material
// filtering behaviour is default constructed from the helper header instead,
// because the helper defaults are the behaviour we want and restating them
// creates a second place to keep in sync.
//
// Copyright (c) Northlight Interactive. Internal render source.

#include "eh/render/sampler_cache.h"

#include <cstdint>

#include "d3dx12.h"

namespace nl {
namespace render {

// Shader visibility for the whole table. Everything is visible everywhere:
// the table is small and per stage visibility saved nothing measurable.
inline constexpr D3D12_SHADER_VISIBILITY kSamplerVisibility =
    D3D12_SHADER_VISIBILITY_ALL;

// Register space for the shared table. Material specific samplers, if we ever
// add any, would go in a different space.
inline constexpr std::uint32_t kSharedSamplerSpace = 0;

// Shadow comparison bias. Tuned against the cascade resolution.
inline constexpr float kShadowMipLodBias = 0.0f;

void AppendStaticSamplers(RootSignatureBuilder& builder) {
    // Slot zero. Point clamp, for anything that reads a full screen buffer at
    // one to one and must not filter across the edge.
    CD3DX12_STATIC_SAMPLER_DESC point_clamp(
        static_cast<UINT>(SamplerSlot::PointClamp),
        D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    point_clamp.ShaderVisibility = kSamplerVisibility;
    point_clamp.RegisterSpace = kSharedSamplerSpace;
    builder.AddStaticSampler(point_clamp);

    // Slot one. Linear clamp, for the post chain and for the lookup tables,
    // where wrapping would fetch from the far edge of the table.
    CD3DX12_STATIC_SAMPLER_DESC linear_clamp(
        static_cast<UINT>(SamplerSlot::LinearClamp),
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    linear_clamp.ShaderVisibility = kSamplerVisibility;
    linear_clamp.RegisterSpace = kSharedSamplerSpace;
    builder.AddStaticSampler(linear_clamp);

    // Slot two. Linear wrap, for tiling detail textures that are sampled
    // without anisotropy because they are always seen close to head on.
    CD3DX12_STATIC_SAMPLER_DESC linear_wrap(
        static_cast<UINT>(SamplerSlot::LinearWrap),
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);
    linear_wrap.ShaderVisibility = kSamplerVisibility;
    linear_wrap.RegisterSpace = kSharedSamplerSpace;
    builder.AddStaticSampler(linear_wrap);

    // Slot three. The material sampler. Default constructed on purpose: the
    // helper header's defaults are anisotropic wrap filtering with the
    // helper's own anisotropy setting, which is the behaviour material
    // authors expect and the behaviour we shipped the last title with.
    //
    // Do not expand this into an explicit construction without checking what
    // the helper defaults actually are first. The last person who tried
    // copied the parameters out of the sampler above and quietly turned
    // anisotropic filtering off for every material in the game.
    CD3DX12_STATIC_SAMPLER_DESC material_wrap(
        static_cast<UINT>(SamplerSlot::MaterialWrap));
    material_wrap.ShaderVisibility = kSamplerVisibility;
    material_wrap.RegisterSpace = kSharedSamplerSpace;
    builder.AddStaticSampler(material_wrap);

    // Slot four. Shadow comparison. Spelled out because every parameter here
    // interacts with the cascade setup.
    CD3DX12_STATIC_SAMPLER_DESC shadow_compare(
        static_cast<UINT>(SamplerSlot::ShadowCompare),
        D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        kShadowMipLodBias);
    shadow_compare.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    shadow_compare.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    shadow_compare.ShaderVisibility = kSamplerVisibility;
    shadow_compare.RegisterSpace = kSharedSamplerSpace;
    builder.AddStaticSampler(shadow_compare);
}

const char* DescribeSamplerSlot(SamplerSlot slot) {
    switch (slot) {
        case SamplerSlot::PointClamp:    return "point clamp";
        case SamplerSlot::LinearClamp:   return "linear clamp";
        case SamplerSlot::LinearWrap:    return "linear wrap";
        case SamplerSlot::MaterialWrap:  return "material wrap, helper defaults";
        case SamplerSlot::ShadowCompare: return "shadow comparison";
        default:                         return "unknown";
    }
}

// ------------------------------------------------------------- validation
//
// Run at startup in development builds. Catches a table that has drifted out
// of slot order, which produces silently wrong filtering rather than a device
// removal and is otherwise very hard to notice.

bool ValidateSamplerTable(const RootSignatureBuilder& builder) {
    const std::uint32_t count = builder.StaticSamplerCount();
    if (count != static_cast<std::uint32_t>(SamplerSlot::Count)) {
        return false;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        if (builder.StaticSamplerRegister(i) != i) {
            return false;
        }
        if (builder.StaticSamplerSpace(i) != kSharedSamplerSpace) {
            return false;
        }
    }
    return true;
}

}  // namespace render
}  // namespace nl
