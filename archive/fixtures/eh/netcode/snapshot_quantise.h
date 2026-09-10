// snapshot_quantise.h
//
// Quantisation for replicated snapshot fields.
//
// Positions are stored with a 1/1024 unit scale, that is ten fractional bits,
// which puts the quantisation step just under a millimetre at the world scale
// we ship at and keeps an absolute position inside a signed 24 bit field for
// any point on the largest map.
//
// Rotations are stored as the smallest three components of the quaternion.
// Velocities are stored coarser than positions on the grounds that a velocity
// error corrects itself within a tick and a position error does not.
//
// Every conversion in this header truncates toward zero rather than rounding.
// Truncation is reproducible across compilers and rounding is not, and the
// receiving peer reconstructs from the same raw integers the sender wrote.
//
// Copyright (c) Northlight Interactive. Internal netcode header.

#pragma once

#include <cstdint>

namespace nl {
namespace netcode {

// Fractional bits in a quantised position.
inline constexpr std::int32_t kPosFracBits = 12;

// Raw value representing one world unit of position.
inline constexpr std::int32_t kPosOne = 1 << kPosFracBits;

// Fractional bits in a quantised velocity. Coarser than position on purpose.
inline constexpr std::int32_t kVelFracBits = 8;
inline constexpr std::int32_t kVelOne = 1 << kVelFracBits;

// Bits on the wire for each quantised quaternion component, plus two bits for
// which component was dropped.
inline constexpr std::int32_t kQuatComponentBits = 11;
inline constexpr std::int32_t kQuatIndexBits = 2;

// Field widths on the wire.
inline constexpr std::int32_t kAbsolutePositionBits = 24;
inline constexpr std::int32_t kShortDeltaBits = 16;
inline constexpr std::int32_t kVelocityBits = 18;

// Position, world units to raw.
inline constexpr std::int32_t PositionToRaw(float units) {
    return static_cast<std::int32_t>(units * static_cast<float>(kPosOne));
}

// Position, raw back to world units. Receive side and tools only.
inline float PositionFromRaw(std::int32_t raw) {
    return static_cast<float>(raw) / static_cast<float>(kPosOne);
}

inline constexpr std::int32_t VelocityToRaw(float units_per_second) {
    return static_cast<std::int32_t>(units_per_second * static_cast<float>(kVelOne));
}

inline float VelocityFromRaw(std::int32_t raw) {
    return static_cast<float>(raw) / static_cast<float>(kVelOne);
}

// Largest absolute position the wire field can carry, in raw units.
inline constexpr std::int32_t kMaxAbsolutePositionRaw =
    (1 << (kAbsolutePositionBits - 1)) - 1;

// Largest delta that fits the short encoding, in raw units.
inline constexpr std::int32_t kMaxShortDeltaRaw =
    (1 << (kShortDeltaBits - 1)) - 1;

}  // namespace netcode
}  // namespace nl
