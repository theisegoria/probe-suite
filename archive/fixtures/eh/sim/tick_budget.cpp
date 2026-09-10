// tick_budget.cpp
//
// Implementation of the fixed timestep pump.
//
// The shape is the usual accumulator loop with two guards bolted on. Both
// guards exist because of shipped bugs, and both change the arithmetic, so
// read them before predicting how many ticks a frame will run.
//
//   Guard one, the frame clamp. A frame delta longer than kMaxFrameDeltaUs is
//   clamped before it reaches the accumulator. This is a clamp, not a drop:
//   the clamped value is still added.
//
//   Guard two, the spiral guard. If the substep loop hits its ceiling and the
//   accumulator still holds a whole tick or more, the leftover is discarded
//   rather than carried. Carrying it means the next frame starts already
//   behind, which is how the death spiral begins. Discarding it means the
//   simulation clock silently falls behind the wall clock, which is the
//   lesser evil and is counted in stats so QA can see it happen.
//
// Copyright (c) Northlight Interactive. Internal engine source.

#include "eh/sim/tick_budget.h"

#include <cstdint>

namespace nl {
namespace sim {

std::int32_t TickBudget::Advance(std::int64_t frame_delta_us) {
    if (frame_delta_us < 0) {
        // A backwards clock reading. Treat it as a zero length frame.
        frame_delta_us = 0;
    }

    // Guard one. Clamp, then accumulate the clamped value.
    if (frame_delta_us > kMaxFrameDeltaUs) {
        frame_delta_us = kMaxFrameDeltaUs;
        ++stats_.clamped_frames;
    }

    accumulator_us_ += frame_delta_us;

    std::int32_t substeps = 0;
    while (accumulator_us_ >= kTickPeriodUs && substeps < kMaxSubstepsPerFrame) {
        accumulator_us_ -= kTickPeriodUs;
        ++substeps;
        ++stats_.tick_count;
    }

    // Guard two. The loop stopped. If it stopped because it ran out of
    // substeps rather than because the accumulator ran dry, throw the
    // remainder away.
    if (accumulator_us_ >= kTickPeriodUs) {
        accumulator_us_ = 0;
        ++stats_.dropped_frames;
    }

    stats_.last_substeps = substeps;
    return substeps;
}

float TickBudget::InterpolationAlpha() const {
    return static_cast<float>(accumulator_us_) / static_cast<float>(kTickPeriodUs);
}

void TickBudget::Reset() {
    accumulator_us_ = 0;
    stats_ = TickBudgetStats{};
}

// ------------------------------------------------------------- the frame loop
//
// Kept here so the pump and its only caller stay in the same file. The order
// matters: input is sampled once per frame and held for every substep of that
// frame, because sampling per substep would let a peer with a longer frame
// read the input device more often than a peer with a shorter one.

void FrameLoop::RunFrame(std::int64_t frame_delta_us) {
    input_.SampleOncePerFrame();

    const std::int32_t substeps = budget_.Advance(frame_delta_us);
    for (std::int32_t i = 0; i < substeps; ++i) {
        const std::int64_t tick_index = budget_.Stats().tick_count - (substeps - i - 1);
        world_.StepFixed(input_.Held(), tick_index);
    }

    renderer_.Present(world_, budget_.InterpolationAlpha());
}

// Diagnostic helper used by the frame graph overlay. Reports how far the
// simulation clock has fallen behind the wall clock, in whole ticks.
std::int64_t FrameLoop::TicksBehindWallClock(std::int64_t wall_clock_us) const {
    const std::int64_t simulated_us = budget_.Stats().tick_count * kTickPeriodUs;
    if (wall_clock_us <= simulated_us) {
        return 0;
    }
    return (wall_clock_us - simulated_us) / kTickPeriodUs;
}

// The overlay also wants the recent substep histogram, so QA can tell a frame
// that ran four substeps once from a frame that runs four every time.
void FrameLoop::AccumulateHistogram(std::int32_t (&bins)[kMaxSubstepsPerFrame + 1]) const {
    const std::int32_t last = budget_.Stats().last_substeps;
    if (last >= 0 && last <= kMaxSubstepsPerFrame) {
        ++bins[last];
    }
}

}  // namespace sim
}  // namespace nl
