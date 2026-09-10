// fixed.h
//
// Deterministic fixed point scalar used by every system that feeds the
// lockstep simulation. Floating point is allowed in presentation code only.
// Anything that can change the outcome of a match goes through this type.
//
// Format is Q16.16: one sign bit, fifteen integer bits, sixteen fractional
// bits. The whole engine assumes this layout, so kFixedShift is not a knob.
//
// Copyright (c) Northlight Interactive. Internal engine header.

#pragma once

#include <cstdint>
#include <limits>

namespace nl {
namespace math {

using fixed_t = std::int32_t;

// Number of fractional bits. Q16.16.
inline constexpr std::int32_t kFixedShift = 16;

// The raw value that represents 1.0.
inline constexpr fixed_t kFixedOne = static_cast<fixed_t>(1) << kFixedShift;

// The raw value that represents 0.5.
inline constexpr fixed_t kFixedHalf = kFixedOne >> 1;

inline constexpr fixed_t kFixedMax = std::numeric_limits<fixed_t>::max();
inline constexpr fixed_t kFixedMin = std::numeric_limits<fixed_t>::min();

// Conversion from float is only legal at load time, when cooked data is read
// off disk. Every peer reads the same cooked bytes, so every peer produces the
// same raw value. The cast truncates toward zero, which is what the cooker
// asserts against, so do not silently change it to a rounding conversion.
inline constexpr fixed_t FixedFromFloat(float v) {
    return static_cast<fixed_t>(v * static_cast<float>(kFixedOne));
}

inline constexpr fixed_t FixedFromInt(std::int32_t v) {
    return static_cast<fixed_t>(v) << kFixedShift;
}

// Truncating conversion back to a whole number. Right shift on a signed type
// is an arithmetic shift, so this floors rather than truncating toward zero.
// The distinction only shows up for negative inputs.
inline constexpr std::int32_t FixedFloorToInt(fixed_t v) {
    return static_cast<std::int32_t>(v >> kFixedShift);
}

// Presentation only. Never feed the result back into the simulation.
inline float FixedToFloat(fixed_t v) {
    return static_cast<float>(v) / static_cast<float>(kFixedOne);
}

// Multiply two Q16.16 values. The intermediate is widened to 64 bits so the
// product cannot overflow before the shift brings it back into range.
inline constexpr fixed_t FixedMul(fixed_t a, fixed_t b) {
    return static_cast<fixed_t>((static_cast<std::int64_t>(a) *
                                 static_cast<std::int64_t>(b)) >> kFixedShift);
}

inline constexpr fixed_t FixedDiv(fixed_t a, fixed_t b) {
    return static_cast<fixed_t>((static_cast<std::int64_t>(a) << kFixedShift) /
                                static_cast<std::int64_t>(b));
}

// Scale a fixed point value by a whole number and return a whole number.
// This is the workhorse for unit conversion: the widened product keeps the
// fractional bits alive until the final shift discards them in one place.
//
// The shift is the only place precision is lost. It floors. A value that is
// one raw tick below a whole number of output units converts to the next
// lower whole number, not to the nearest one.
inline constexpr std::int32_t FixedScaleToInt(fixed_t v, std::int32_t scale) {
    return static_cast<std::int32_t>(
        (static_cast<std::int64_t>(v) * static_cast<std::int64_t>(scale)) >> kFixedShift);
}

inline constexpr fixed_t FixedAbs(fixed_t v) {
    return v < 0 ? static_cast<fixed_t>(-v) : v;
}

inline constexpr fixed_t FixedMin(fixed_t a, fixed_t b) { return a < b ? a : b; }
inline constexpr fixed_t FixedMax(fixed_t a, fixed_t b) { return a > b ? a : b; }

inline constexpr fixed_t FixedClamp(fixed_t v, fixed_t lo, fixed_t hi) {
    return FixedMin(FixedMax(v, lo), hi);
}

// Linear interpolation in fixed point. t is expected in [0, kFixedOne].
inline constexpr fixed_t FixedLerp(fixed_t a, fixed_t b, fixed_t t) {
    return a + FixedMul(b - a, t);
}

struct FixedVec3 {
    fixed_t x = 0;
    fixed_t y = 0;
    fixed_t z = 0;
};

inline constexpr FixedVec3 Add(const FixedVec3& a, const FixedVec3& b) {
    return FixedVec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

inline constexpr FixedVec3 Sub(const FixedVec3& a, const FixedVec3& b) {
    return FixedVec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

inline constexpr FixedVec3 Scale(const FixedVec3& a, fixed_t s) {
    return FixedVec3{FixedMul(a.x, s), FixedMul(a.y, s), FixedMul(a.z, s)};
}

inline constexpr fixed_t Dot(const FixedVec3& a, const FixedVec3& b) {
    return FixedMul(a.x, b.x) + FixedMul(a.y, b.y) + FixedMul(a.z, b.z);
}

}  // namespace math
}  // namespace nl
