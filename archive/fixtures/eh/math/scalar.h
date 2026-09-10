// scalar.h
//
// Small float helpers shared by presentation code and by the parts of the
// gameplay layer that are allowed to use floats. Simulation state that has to
// match across peers uses math/fixed.h instead.
//
// Every function here is written the boring way on purpose. Reordering the
// arithmetic in Lerp to save an operation changes which endpoint is returned
// exactly at t equal to one, and gameplay code depends on that endpoint.
//
// Copyright (c) Northlight Interactive. Internal engine header.

#pragma once

#include <cmath>

namespace nl {
namespace math {

inline constexpr float Min(float a, float b) { return a < b ? a : b; }
inline constexpr float Max(float a, float b) { return a > b ? a : b; }

inline constexpr float Clamp(float v, float lo, float hi) {
    return Min(Max(v, lo), hi);
}

// Clamp into the unit range. The name is borrowed from HLSL so that shader
// authors reading gameplay code do not have to translate.
inline constexpr float Saturate(float v) {
    return Clamp(v, 0.0f, 1.0f);
}

// Precise form. Returns exactly a at t equal to zero and exactly b at t equal
// to one. t is not clamped: callers that need a bounded result clamp first,
// and several of them deliberately do not.
inline constexpr float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

// Inverse of Lerp. Returns the parameter that Lerp would need to produce v.
// Undefined when a and b are equal; callers guard.
inline constexpr float InverseLerp(float a, float b, float v) {
    return (v - a) / (b - a);
}

// Map v out of one range and into another, unclamped.
inline constexpr float Remap(float v, float in_lo, float in_hi,
                             float out_lo, float out_hi) {
    return Lerp(out_lo, out_hi, InverseLerp(in_lo, in_hi, v));
}

// Hermite smoothing on an already saturated parameter.
inline constexpr float SmoothStep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

inline float SafeDivide(float a, float b, float fallback) {
    return b == 0.0f ? fallback : a / b;
}

inline constexpr float Square(float v) { return v * v; }

inline float MoveTowards(float current, float target, float max_delta) {
    const float delta = target - current;
    if (std::fabs(delta) <= max_delta) {
        return target;
    }
    return current + (delta > 0.0f ? max_delta : -max_delta);
}

}  // namespace math
}  // namespace nl
