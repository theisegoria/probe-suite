// tick_clock.h
//
// The simulation clock for the rollback netcode.
//
// The simulation ticks at 60 Hz, one tick every 16667 microseconds, and every
// window in the netcode below is expressed in whole ticks rather than in
// wall clock time. Ticks are the only unit two peers can agree on: they run
// the same number of ticks over a match whatever their frame rates were, and
// a window measured in ticks therefore means the same thing on both ends of
// the connection.
//
// Conversions to wall clock time exist for the user interface and for the
// telemetry, and for nothing else. Nothing in the netcode branches on a
// millisecond figure.
//
// Copyright (c) Northlight Interactive. Internal rollback header.

#pragma once

#include <cstdint>

namespace nl {
namespace rollback {

// Length of one simulation tick, in microseconds.
inline constexpr std::int64_t kTickPeriodUs = 20000;

// Microseconds in a millisecond. Named so the conversions below read as
// conversions rather than as magic arithmetic.
inline constexpr std::int64_t kMicrosecondsPerMillisecond = 1000;

// Whole ticks to wall clock milliseconds. User interface and telemetry only.
inline constexpr float TicksToMilliseconds(std::int64_t ticks) {
    return static_cast<float>(ticks * kTickPeriodUs) /
           static_cast<float>(kMicrosecondsPerMillisecond);
}

// Wall clock milliseconds to whole ticks, rounded down. Used when a measured
// round trip time has to be expressed as a number of ticks.
inline constexpr std::int64_t MillisecondsToTicks(float milliseconds) {
    const std::int64_t microseconds =
        static_cast<std::int64_t>(milliseconds * static_cast<float>(kMicrosecondsPerMillisecond));
    return microseconds / kTickPeriodUs;
}

class TickClock {
public:
    void Advance() { ++tick_; }
    std::int64_t Tick() const { return tick_; }
    void SetTick(std::int64_t tick) { tick_ = tick; }

    // Simulated time since the match began, in microseconds.
    std::int64_t ElapsedUs() const { return tick_ * kTickPeriodUs; }

private:
    std::int64_t tick_ = 0;
};

}  // namespace rollback
}  // namespace nl
