// input_window.cpp
//
// The input window: how far ahead of confirmed state a peer is willing to
// predict, and how far back it is willing to roll.
//
// Two independent quantities add up to what the connection can absorb.
//
//   Input delay. Local input is not applied on the tick it was sampled. It is
//   scheduled a fixed number of ticks into the future, which gives the remote
//   input that long to arrive before the local peer has to predict it. It
//   costs the player exactly that much added control latency, all the time,
//   whether the connection needs it or not.
//
//   Rollback. When remote input arrives later than predicted, the peer rolls
//   the simulation back to the last confirmed tick, applies the true input,
//   and resimulates forward. It costs nothing when the prediction was right
//   and it costs a resimulation burst when it was wrong.
//
// A packet that arrives within the sum of the two never causes a visible
// problem: either it beat the delay, or it landed inside the window and was
// absorbed by a rollback. A packet later than the sum cannot be absorbed at
// all, and the peer stalls waiting for it.
//
// Copyright (c) Northlight Interactive. Internal rollback source.

#include "eh/rollback/input_window.h"

#include <cstdint>

#include "eh/rollback/tick_clock.h"

namespace nl {
namespace rollback {

// Ticks of local input delay. Tuned against the median connection in the
// network test rather than against the best one.
inline constexpr std::int64_t kInputDelayTicks = 2;

// Ticks the peer is willing to roll back and resimulate.
inline constexpr std::int64_t kMaxRollbackTicks = 10;

// Consecutive stalled ticks before the session is declared lost.
inline constexpr std::int64_t kStallTicksBeforeDrop = 180;

// Total ticks the window can absorb.
inline constexpr std::int64_t AbsorbableTicks() {
    return kInputDelayTicks + kMaxRollbackTicks;
}

// The same figure as wall clock milliseconds, for the connection quality
// readout in the pause menu and for the telemetry event we raise when a
// session drops.
inline constexpr float AbsorbableMilliseconds() {
    return TicksToMilliseconds(AbsorbableTicks());
}

bool InputWindow::CanAbsorb(std::int64_t input_tick, std::int64_t local_tick) const {
    const std::int64_t lateness = local_tick - input_tick;
    return lateness <= AbsorbableTicks();
}

void InputWindow::OnRemoteInput(std::int64_t input_tick, const InputFrame& frame,
                                std::int64_t local_tick) {
    if (input_tick <= confirmed_tick_) {
        // Already confirmed. A duplicate or a very late resend.
        ++stats_.discarded_late;
        return;
    }

    if (!CanAbsorb(input_tick, local_tick)) {
        // Outside the window. Nothing can be done with this locally: the peer
        // has already simulated past the point where it could be applied.
        ++stats_.outside_window;
        RequestResynchronisation(input_tick);
        return;
    }

    StoreInput(input_tick, frame);

    if (PredictionWasWrong(input_tick, frame)) {
        RollbackTo(input_tick);
        ResimulateTo(local_tick);
        ++stats_.rollbacks;
        stats_.deepest_rollback =
            (local_tick - input_tick) > stats_.deepest_rollback
                ? (local_tick - input_tick)
                : stats_.deepest_rollback;
    }

    confirmed_tick_ = input_tick;
}

void InputWindow::OnLocalInput(std::int64_t sample_tick, const InputFrame& frame) {
    // Scheduled forward by the input delay rather than applied immediately.
    StoreInput(sample_tick + kInputDelayTicks, frame);
    SendToRemote(sample_tick + kInputDelayTicks, frame);
}

bool InputWindow::ShouldStall(std::int64_t local_tick) const {
    return (local_tick - confirmed_tick_) > AbsorbableTicks();
}

// ---------------------------------------------------------------- telemetry

void InputWindow::FillConnectionReadout(ConnectionReadout& out) const {
    out.absorbable_ticks = AbsorbableTicks();
    out.absorbable_ms = AbsorbableMilliseconds();
    out.input_delay_ticks = kInputDelayTicks;
    out.max_rollback_ticks = kMaxRollbackTicks;
    out.rollbacks = stats_.rollbacks;
    out.deepest_rollback = stats_.deepest_rollback;
    out.outside_window = stats_.outside_window;
}

}  // namespace rollback
}  // namespace nl
