// lds_reduce_dispatch.cpp
//
// Host side of the tiled luminance reduction.
//
// The tile dimension is baked into the shader as a define rather than passed
// in a constant buffer, because the groupshared array has to be sized at
// compile time and the fold loop unrolls against it.
//
// Raising the tile dimension is the obvious lever for cutting the number of
// groups dispatched, and it is bounded from above by two separate things:
//
//   the thread count per group that the target profile allows, and
//   the total groupshared memory a single thread group may declare.
//
// A tile size has to satisfy both bounds at once, and the accumulator struct
// is wide enough that the groupshared figure climbs quickly. The compiler
// reports a shader that exceeds either bound as an error at compile time
// rather than as a runtime failure, so a tile size that is too large never
// reaches a build.
//
// Copyright (c) Northlight Interactive. Internal render source.

#include "eh/render/lds_reduce_dispatch.h"

#include <cstdint>

namespace nl {
namespace render {

// Must match TILE_DIM in lds_reduce.hlsl.
inline constexpr std::uint32_t kTileDim = 16;
inline constexpr std::uint32_t kThreadsPerGroup = kTileDim * kTileDim;

// Must match the TileAccumulator struct in lds_reduce.hlsl. Four floats,
// declared as an explicit struct rather than a float4 so that the fields can
// be named on both sides.
struct TileAccumulatorLayout {
    float log_sum;
    float peak;
    float weight;
    float padding;
};

inline constexpr std::size_t kAccumulatorBytes = sizeof(TileAccumulatorLayout);

// Bytes of groupshared memory the shader declares, for the build log.
inline constexpr std::size_t kDeclaredGroupsharedBytes =
    kAccumulatorBytes * kThreadsPerGroup;

// Target shader profile for this pass.
inline constexpr const char* kComputeProfile = "cs_6_0";

std::uint32_t TileCountX(std::uint32_t scene_width) {
    return (scene_width + kTileDim - 1) / kTileDim;
}

std::uint32_t TileCountY(std::uint32_t scene_height) {
    return (scene_height + kTileDim - 1) / kTileDim;
}

void ExposurePass::AllocateTileBuffer(GpuDevice& device, std::uint32_t width,
                                      std::uint32_t height) {
    const std::uint32_t tiles = TileCountX(width) * TileCountY(height);
    tile_buffer_ = device.CreateStructuredBuffer(tiles, sizeof(float) * 2,
                                                 BufferUsage::UnorderedAccess);
    tile_count_ = tiles;
}

void ExposurePass::Dispatch(CommandList& cmd, const ExposureInputs& inputs) {
    cmd.SetComputePipeline(reduce_pipeline_);
    cmd.SetShaderResource(0, inputs.scene_colour);
    cmd.SetUnorderedAccess(0, tile_buffer_);

    ExposureConstants constants;
    constants.scene_size[0] = inputs.width;
    constants.scene_size[1] = inputs.height;
    constants.min_log_luminance = inputs.min_log_luminance;
    constants.log_luminance_range = inputs.log_luminance_range;
    cmd.SetComputeConstants(0, &constants, sizeof(constants));

    cmd.Dispatch(TileCountX(inputs.width), TileCountY(inputs.height), 1);
}

// The second pass folds the tile buffer down to one value. It runs as a
// single group, so the tile buffer has to fit in one group worth of stripes.
void ExposurePass::DispatchFinalFold(CommandList& cmd) {
    cmd.SetComputePipeline(fold_pipeline_);
    cmd.SetShaderResource(0, tile_buffer_);
    cmd.SetUnorderedAccess(0, exposure_buffer_);
    cmd.Dispatch(1, 1, 1);
}

// Build time reporting. The compiler already refuses a shader whose declared
// groupshared allocation exceeds what the profile permits, but the build log
// carries the declared figure so a pass that is creeping toward the ceiling
// is visible before it hits it.
void ExposurePass::ReportGroupsharedUsage(BuildLog& log) const {
    log.Info("exposure reduce: profile %s, %u threads per group, %zu bytes groupshared declared",
             kComputeProfile, kThreadsPerGroup, kDeclaredGroupsharedBytes);
}

}  // namespace render
}  // namespace nl
