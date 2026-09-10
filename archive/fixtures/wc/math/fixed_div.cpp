// math/fixed_div.cpp
//
// Current fixed point divide. Same shape as the 2.6 path in fixed_div_v1.cpp;
// the rounding mode is the only difference.

#include "math/fixed_div.h"

namespace mathlib {

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

int32_t FixedReciprocal(int64_t denominator_raw) {
    return FixedDiv(1, denominator_raw);
}

}  // namespace mathlib
