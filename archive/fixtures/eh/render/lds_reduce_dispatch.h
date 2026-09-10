// lds_reduce_dispatch.h
//
// Host side declarations for the tiled luminance reduction.

#pragma once

#include <cstddef>
#include <cstdint>

namespace nl {
namespace render {

class GpuDevice;
class CommandList;
class BuildLog;

enum class BufferUsage : std::uint32_t {
    ShaderResource = 1,
    UnorderedAccess = 2,
};

struct GpuBuffer {
    std::uint64_t handle = 0;
};

struct ExposureInputs {
    GpuBuffer scene_colour;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    float min_log_luminance = -10.0f;
    float log_luminance_range = 22.0f;
};

struct ExposureConstants {
    std::uint32_t scene_size[2];
    float min_log_luminance;
    float log_luminance_range;
};

class ExposurePass {
public:
    void AllocateTileBuffer(GpuDevice& device, std::uint32_t width, std::uint32_t height);
    void Dispatch(CommandList& cmd, const ExposureInputs& inputs);
    void DispatchFinalFold(CommandList& cmd);
    void ReportGroupsharedUsage(BuildLog& log) const;

private:
    GpuBuffer tile_buffer_;
    GpuBuffer exposure_buffer_;
    std::uint64_t reduce_pipeline_ = 0;
    std::uint64_t fold_pipeline_ = 0;
    std::uint32_t tile_count_ = 0;
};

}  // namespace render
}  // namespace nl
