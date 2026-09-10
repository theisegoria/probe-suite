// engine/math/sim_math.h
//
// The vector and scalar helpers the simulation is allowed to use.
//
// Everything in this header is written so that a given expression produces
// the same bit pattern on every target we ship on. The sim math translation
// units are compiled with -ffp-contract=off and the pragma below repeats it
// for anyone who includes the header into a unit that forgot.

#pragma once

#include <cstdint>

#include "core/types.h"

#pragma STDC FP_CONTRACT OFF

namespace sim {

using core::Q16;
using core::kQ16One;

inline constexpr float kTickSeconds  = core::kTickSeconds;
inline constexpr uint32_t kTickHz    = core::kTickHz;

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Vec3 Add(const Vec3& a, const Vec3& b) { return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 Sub(const Vec3& a, const Vec3& b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 Scale(const Vec3& a, float s)     { return Vec3{a.x * s, a.y * s, a.z * s}; }

inline float Dot(const Vec3& a, const Vec3& b) {
    // Written out rather than folded so that the three products round before
    // they are summed, and always in this order.
    const float xx = a.x * b.x;
    const float yy = a.y * b.y;
    const float zz = a.z * b.z;
    return (xx + yy) + zz;
}

inline Vec3 Cross(const Vec3& a, const Vec3& b) {
    return Vec3{a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x};
}

inline float LengthSq(const Vec3& a) { return Dot(a, a); }

// Square root is required by IEEE 754 to be correctly rounded, so this is as
// portable as the multiply above.
float Length(const Vec3& a);
Vec3  Normalise(const Vec3& a);

// Multiply then add, with the product forced to round before the addition.
// The volatile is deliberate: without it the compiler is free to keep the
// product in a wider register or to merge the two operations, and which of
// those it picks depends on the target and on the optimisation level.
inline float MulThenAdd(float a, float b, float c) {
    volatile float product = a * b;
    return product + c;
}

// Same, for the common accumulate shape acc = acc + a * b.
inline float AccumulateProduct(float acc, float a, float b) {
    volatile float product = a * b;
    return acc + product;
}

inline float Min(float a, float b) { return a < b ? a : b; }
inline float Max(float a, float b) { return a > b ? a : b; }
inline float Clamp(float v, float lo, float hi) { return Min(Max(v, lo), hi); }

inline float Abs(float v) { return v < 0.0f ? -v : v; }

// Linear interpolation with the endpoint guaranteed: Lerp(a, b, 1.0f) is
// exactly b, which the naive a + t * (b - a) form does not give you.
inline float Lerp(float a, float b, float t) {
    const float lo = a * (1.0f - t);
    const float hi = b * t;
    return lo + hi;
}

struct Transform {
    Vec3 position;
    Q16  facing_turns = 0;   // see math/fixed_trig.h
    Q16  scale = kQ16One;
};

}  // namespace sim
