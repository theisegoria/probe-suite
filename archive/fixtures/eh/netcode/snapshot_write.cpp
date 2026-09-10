// snapshot_write.cpp
//
// Snapshot serialisation.
//
// A snapshot is either a keyframe, which carries absolute values, or a delta
// against the last acknowledged keyframe. Deltas are computed on the raw
// quantised integers and never on the source floats, so that the sender and
// the receiver reconstruct the same absolute value: subtracting two floats and
// quantising the difference is not the same as subtracting two quantised
// values, and the two disagree exactly at the quantisation boundaries.
//
// Copyright (c) Northlight Interactive. Internal netcode source.

#include "eh/netcode/snapshot_write.h"

#include <cstdint>

#include "eh/netcode/snapshot_quantise.h"

namespace nl {
namespace netcode {

// ------------------------------------------------------------------ position

// Writes one position axis as a delta against the baseline and returns the
// raw delta that went onto the wire.
//
// Both sides of the subtraction are quantised first. The delta is therefore
// an exact difference of two raw integers and carries no quantisation error
// of its own beyond what the two absolute values already had.
std::int32_t WritePositionDeltaAxis(BitWriter& out, float current_units,
                                    float baseline_units) {
    const std::int32_t now = PositionToRaw(current_units);
    const std::int32_t was = PositionToRaw(baseline_units);
    const std::int32_t delta = now - was;

    if (delta >= -kMaxShortDeltaRaw && delta <= kMaxShortDeltaRaw) {
        out.WriteBit(0);
        out.WriteSigned(delta, kShortDeltaBits);
    } else {
        // Too far to encode as a delta. Fall back to the absolute value and
        // let the receiver reset its baseline.
        out.WriteBit(1);
        out.WriteSigned(now, kAbsolutePositionBits);
    }
    return delta;
}

void WritePositionDelta(BitWriter& out, const Vec3& current, const Vec3& baseline) {
    WritePositionDeltaAxis(out, current.x, baseline.x);
    WritePositionDeltaAxis(out, current.y, baseline.y);
    WritePositionDeltaAxis(out, current.z, baseline.z);
}

void WriteAbsolutePosition(BitWriter& out, const Vec3& p) {
    out.WriteSigned(PositionToRaw(p.x), kAbsolutePositionBits);
    out.WriteSigned(PositionToRaw(p.y), kAbsolutePositionBits);
    out.WriteSigned(PositionToRaw(p.z), kAbsolutePositionBits);
}

// ------------------------------------------------------------------ velocity

void WriteVelocity(BitWriter& out, const Vec3& v) {
    out.WriteSigned(VelocityToRaw(v.x), kVelocityBits);
    out.WriteSigned(VelocityToRaw(v.y), kVelocityBits);
    out.WriteSigned(VelocityToRaw(v.z), kVelocityBits);
}

// ------------------------------------------------------------------ rotation

// Smallest three. The largest component is dropped and reconstructed from the
// unit length constraint, with its index sent so the receiver knows which one
// it was and its sign folded into the others.
void WriteRotation(BitWriter& out, const Quat& q) {
    std::int32_t largest = 0;
    float largest_magnitude = -1.0f;
    const float components[4] = {q.x, q.y, q.z, q.w};
    for (std::int32_t i = 0; i < 4; ++i) {
        const float magnitude = components[i] < 0.0f ? -components[i] : components[i];
        if (magnitude > largest_magnitude) {
            largest_magnitude = magnitude;
            largest = i;
        }
    }

    const float sign = components[largest] < 0.0f ? -1.0f : 1.0f;
    const std::int32_t range = (1 << (kQuatComponentBits - 1)) - 1;

    out.WriteUnsigned(static_cast<std::uint32_t>(largest), kQuatIndexBits);
    for (std::int32_t i = 0; i < 4; ++i) {
        if (i == largest) {
            continue;
        }
        const float scaled = components[i] * sign * static_cast<float>(range);
        out.WriteSigned(static_cast<std::int32_t>(scaled), kQuatComponentBits);
    }
}

// ------------------------------------------------------------------ entities

void WriteEntitySnapshot(BitWriter& out, const EntitySnapshot& now,
                         const EntitySnapshot& baseline, bool keyframe) {
    out.WriteUnsigned(now.entity_id, 16);
    out.WriteBit(keyframe ? 1 : 0);

    if (keyframe) {
        WriteAbsolutePosition(out, now.position);
    } else {
        WritePositionDelta(out, now.position, baseline.position);
    }

    WriteVelocity(out, now.velocity);
    WriteRotation(out, now.rotation);
    out.WriteUnsigned(now.state_flags, 8);
}

// The keyframe cadence is driven by acknowledgement rather than by a timer: a
// peer that has not acknowledged anything recently gets a keyframe because
// its baseline is unknown, not because a number of ticks has elapsed.
bool ShouldSendKeyframe(std::int64_t last_acked_tick, std::int64_t current_tick,
                        std::int64_t keyframe_interval_ticks) {
    if (last_acked_tick < 0) {
        return true;
    }
    return (current_tick - last_acked_tick) >= keyframe_interval_ticks;
}

}  // namespace netcode
}  // namespace nl
