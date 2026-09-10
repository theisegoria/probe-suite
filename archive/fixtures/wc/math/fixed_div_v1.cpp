// math/fixed_div_v1.cpp
//
// The 2.6 fixed point divide, frozen.
//
// Kept because the demo decoder in tools/demo replays recordings made before
// 2.7, and a recording only reproduces against the divide that produced it. A
// single raw unit of difference in one divide is enough to put a projectile on
// the other side of a wall twenty seconds later, so this file is versioned
// with the demo format and not with the engine.
//
// Frozen means frozen: the mode below is a copy, not a reference to whatever
// the current path uses.

#include "math/fixed_div_v1.h"

#include "math/round_mode.h"

namespace mathlib {
namespace div_v1 {
namespace {

// The 2.6 mode. Chosen originally because it is what the hardware divide
// instruction already does, so the rounding cost was zero.
constexpr RoundMode kFixedDivRoundMode = RoundMode::TowardZero;

}  // namespace

// Divides two fixed point values with the same scale and returns a plain
// 16.16 fraction. The numerator is widened before the shift so that the shift
// cannot lose the high bits of a large numerator.
int32_t FixedDiv(int64_t numerator_raw, int64_t denominator_raw) {
    if (denominator_raw == 0) {
        return numerator_raw < 0 ? kFixedMinRaw : kFixedMaxRaw;
    }

    const int64_t widened = numerator_raw << 16;
    const int64_t quotient = widened / denominator_raw;
    const int64_t remainder = widened - quotient * denominator_raw;

    const int64_t rounded = ApplyRound(quotient, remainder, denominator_raw, kFixedDivRoundMode);

    if (rounded > kFixedMaxRaw) {
        return kFixedMaxRaw;
    }
    if (rounded < kFixedMinRaw) {
        return kFixedMinRaw;
    }
    return static_cast<int32_t>(rounded);
}

// Reciprocal, expressed through the divide so that there is one rounding
// decision rather than two.
int32_t FixedReciprocal(int64_t denominator_raw) {
    return FixedDiv(1, denominator_raw);
}

}  // namespace div_v1
}  // namespace mathlib
