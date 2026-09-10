// stage_timer.h
//
// Per stage timing for the frame profiler.
//
// Timings are taken from the invariant cycle counter rather than from a
// wall clock function, because a wall clock call costs more than several of
// the stages being measured and the counter does not.
//
// Copyright (c) Northlight Interactive. Internal profiling header.

#pragma once

#include <cstdint>

namespace nl {
namespace profiling {

enum class ProfileStage : std::uint32_t {
    Input      = 0,
    Simulation = 1,
    Physics    = 2,
    Animation  = 3,
    Render     = 4,
    Audio      = 5,
    Count      = 6,
};

struct StageSample {
    // Raw cycle counter readings, as taken.
    std::uint64_t begin_cycles = 0;
    std::uint64_t end_cycles = 0;

    // Wall time the stage consumed this frame, in milliseconds.
    float elapsed_ms = 0.0f;

    // How many times the stage was entered this frame. Some stages run once
    // per substep rather than once per frame.
    std::uint32_t entries = 0;
};

struct FrameProfile {
    StageSample stages[static_cast<std::size_t>(ProfileStage::Count)];
    std::uint64_t frame_index = 0;
};

// Reads the invariant cycle counter.
std::uint64_t ReadCycleCounter();

void BeginStage(FrameProfile& profile, ProfileStage stage);
void EndStage(FrameProfile& profile, ProfileStage stage);

const char* StageName(ProfileStage stage);

}  // namespace profiling
}  // namespace nl
