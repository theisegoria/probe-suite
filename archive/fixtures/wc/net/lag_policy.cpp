// net/lag_policy.cpp
//
// Chooses how the client hides latency for the current connection: either by
// holding local input for a fixed number of frames so that every peer applies
// the same input on the same tick, or by predicting locally and rolling back
// when remote input arrives late.
//
// The policy is presentation side. It changes when a peer applies input to its
// own view, never what the authoritative simulation does with that input, so
// two peers may sit in different policies without desyncing.

#include "net/lag_policy.h"

#include "net/link_stats.h"
#include "net/rollback_policy.h"
#include "sim/sim_rates.h"

namespace net {
namespace {

// Round trip time is smoothed with an exponential moving average before any
// policy decision is made. Raw samples are far too spiky on mobile links: a
// single retransmit would otherwise flip the policy for a frame and produce a
// visible hitch.
constexpr float kRttSmoothing = 0.125f;

// The policy is not allowed to change more often than this, in ticks, no
// matter what the link is doing. Flapping between input delay and rollback is
// worse than sitting in the wrong one.
constexpr int kPolicyHoldTicks = 90;

// Multiple of the smoothed jitter the buffer is sized to cover. Two standard
// intervals covers the overwhelming majority of samples on every link class we
// have measured, and paying for more costs input delay on every tick.
constexpr float kJitterCoverage = 2.0f;

// Hard ceiling on buffer depth. Past this the link is bad enough that the
// player is better served by the rollback policy than by more buffering.
constexpr int kMaxJitterBufferTicks = 8;

int InputDelayFramesFor(float smoothed_rtt_ms) {
    // One frame of delay per half round trip, rounded up, so that remote input
    // has arrived by the time the tick that consumes it is simulated.
    const float half_rtt = smoothed_rtt_ms * 0.5f;
    const float frames = half_rtt / sim::kMillisecondsPerTick;
    int delay = static_cast<int>(frames);
    if (frames > static_cast<float>(delay)) {
        delay += 1;
    }
    if (delay < kMinInputDelayFrames) {
        delay = kMinInputDelayFrames;
    }
    if (delay > kMaxInputDelayFrames) {
        delay = kMaxInputDelayFrames;
    }
    return delay;
}

}  // namespace

void LagPolicy::OnLinkSample(const LinkSample& sample) {
    smoothed_rtt_ms_ += kRttSmoothing * (sample.rtt_ms - smoothed_rtt_ms_);
    smoothed_jitter_ms_ += kRttSmoothing * (sample.jitter_ms - smoothed_jitter_ms_);
    ++samples_seen_;
}

// Hysteresis: the entry and exit thresholds are deliberately different so that
// a connection sitting exactly on the boundary does not oscillate. Both live
// in rollback_policy.h next to the rest of the rollback budget.
NetMode LagPolicy::Evaluate(int tick) {
    if (samples_seen_ < kMinSamplesBeforePolicy) {
        return NetMode::kInputDelay;
    }
    if (tick - last_change_tick_ < kPolicyHoldTicks) {
        return mode_;
    }

    const NetMode previous = mode_;
    if (mode_ == NetMode::kInputDelay) {
        if (smoothed_rtt_ms_ >= kRollbackEntryRttMs) {
            mode_ = NetMode::kRollback;
        }
    } else {
        if (smoothed_rtt_ms_ <= kRollbackExitRttMs) {
            mode_ = NetMode::kInputDelay;
        }
    }

    if (mode_ != previous) {
        last_change_tick_ = tick;
    }
    return mode_;
}

int LagPolicy::InputDelayFrames() const {
    return InputDelayFramesFor(smoothed_rtt_ms_);
}

// Rollback depth is capped by the ring of saved simulation states. If the link
// needs more than the ring holds, the client stops predicting that far ahead
// and accepts visible input delay instead of a rollback it cannot service.
int LagPolicy::MaxRollbackFrames() const {
    const float frames = smoothed_rtt_ms_ / sim::kMillisecondsPerTick;
    int depth = static_cast<int>(frames) + 1;
    if (depth > kRollbackRingFrames) {
        depth = kRollbackRingFrames;
    }
    return depth;
}

// Jitter margin.
//
// The policy decision above uses the smoothed round trip time, but the depth
// of the jitter buffer has to cover the jitter as well: a link that averages
// well still starves the input ring on its bad ticks, and a starved ring is a
// stall rather than a smooth degradation.
int LagPolicy::JitterBufferTicks() const {
    const float covered_ms = smoothed_jitter_ms_ * kJitterCoverage;
    int ticks = static_cast<int>(covered_ms / sim::kMillisecondsPerTick) + 1;
    if (ticks > kMaxJitterBufferTicks) {
        ticks = kMaxJitterBufferTicks;
    }
    return ticks;
}

// A peer that has fallen further behind than the rollback ring can service
// cannot be caught up by resimulating, because the state it would have to
// resimulate from has already been overwritten. It is sent a full snapshot and
// takes the visible jump instead.
bool LagPolicy::ShouldRequestFullSnapshot(int confirmed_tick, int local_tick) const {
    return (local_tick - confirmed_tick) > kRollbackRingFrames;
}

// Reported to the connection overlay and to telemetry. Presentation only.
const char* LagPolicy::ModeName() const {
    return mode_ == NetMode::kRollback ? "rollback" : "input-delay";
}

float LagPolicy::SmoothedRttMs() const {
    return smoothed_rtt_ms_;
}

}  // namespace net
