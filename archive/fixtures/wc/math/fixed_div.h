// math/fixed_div.h
//
// The current fixed point divide.

#ifndef MATH_FIXED_DIV_H
#define MATH_FIXED_DIV_H

#include <cstdint>

#include "math/round_mode.h"

namespace mathlib {

// The rounding mode the current divide applies. Part of the simulation
// contract: changing it changes recorded replays, so it is versioned with the
// protocol.
//
// Moved off the truncating mode in 2.7. Truncation is asymmetric about zero,
// which showed up as a drift on any quantity that spends time on both sides of
// an axis, and the drift accumulated over a long run of divides. Ties to even
// was chosen over ties away because halfway cases are common here: the
// divisors are usually powers of two or round decimals.
constexpr RoundMode kFixedDivRoundMode = RoundMode::NearestTiesEven;

int32_t FixedDiv(int64_t numerator_raw, int64_t denominator_raw);
int32_t FixedReciprocal(int64_t denominator_raw);

}  // namespace mathlib

#endif  // MATH_FIXED_DIV_H
