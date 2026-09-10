// tick_budget.h
//
// The fixed timestep pump. Every simulation tick is the same length in
// simulated time no matter how long the frame took to render, because a
// lockstep peer that ran a different number of ticks has diverged even if
// every tick was individually correct.
//
// Copyright (c) Northlight Interactive. Internal engine header.

#pragma once

#include <cstdint>

namespace nl {
namespace sim {

// One simulation tick, in microseconds. Integer microseconds rather than a
// float second, because the accumulator has to be reproducible across peers
// and a float accumulator drifts differently on different rounding modes.
inline constexpr std::int64_t kTickPeriodUs = 16667;

// Upper bound on how many ticks a single frame may run. A frame that wants
// more than this is a frame that fell behind, and running the backlog makes
// the next frame later still.
inline constexpr std::int32_t kMaxSubstepsPerFrame = 4;

// Upper bound on the frame delta itself, before it reaches the accumulator.
// Anything longer than this is a debugger break, a level load or a suspended
// process, and replaying it as simulated time is never what anyone wants.
inline constexpr std::int64_t kMaxFrameDeltaUs = 250000;

struct TickBudgetStats {
    std::int64_t tick_count = 0;
    std::int64_t dropped_frames = 0;
    std::int64_t clamped_frames = 0;
    std::int32_t last_substeps = 0;
};

class TickBudget {
public:
    // Feed one real frame delta in microseconds. Returns the number of
    // simulation ticks that should run for this frame.
    std::int32_t Advance(std::int64_t frame_delta_us);

    // Microseconds of simulated time carried into the next frame.
    std::int64_t AccumulatorUs() const { return accumulator_us_; }

    // Fraction of a tick the renderer should interpolate by, in the range
    // [0, 1). Presentation only.
    float InterpolationAlpha() const;

    const TickBudgetStats& Stats() const { return stats_; }

    void Reset();

private:
    std::int64_t accumulator_us_ = 0;
    TickBudgetStats stats_;
};

}  // namespace sim
}  // namespace nl
