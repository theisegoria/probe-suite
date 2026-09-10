// math/round_mode.h
//
// Rounding modes for the fixed point helpers.
//
// Rounding is not a detail here. Every fixed point divide and every fixed
// point multiply that shifts down has to discard bits, and which way those
// bits are discarded is simulation visible: a projectile whose flight time
// rounds one raw unit differently on two peers will land in a different place
// a few seconds later. So the mode is spelled out at every call site that has
// a choice, rather than being left to the language.
//
// The modes are not interchangeable and there is no engine wide default. The
// ballistics path, the integration path and the tooling path each pick the
// mode that suits them and say so.

#ifndef MATH_ROUND_MODE_H
#define MATH_ROUND_MODE_H

#include <cstdint>

namespace mathlib {

enum class RoundMode : uint8_t {
    // Truncation. The quotient is rounded toward zero, which is what C and
    // C++ integer division already does.
    TowardZero = 0,

    // Floor. What an arithmetic right shift does for a power of two divisor.
    TowardNegInf = 1,

    // Ceiling.
    TowardPosInf = 2,

    // Round to nearest, halfway cases away from zero.
    NearestTiesAway = 3,

    // Round to nearest, halfway cases to the even quotient.
    NearestTiesEven = 4,
};

// Applies the mode to a truncated quotient and its remainder.
//
// quotient is the truncated result, remainder is a - quotient * b, and divisor
// is b. The remainder carries the sign of the numerator, as it does in C.
inline int64_t ApplyRound(int64_t quotient, int64_t remainder, int64_t divisor, RoundMode mode) {
    if (remainder == 0) {
        return quotient;
    }

    const bool negative = (remainder < 0) != (divisor < 0);
    const int64_t abs_remainder = remainder < 0 ? -remainder : remainder;
    const int64_t abs_divisor = divisor < 0 ? -divisor : divisor;
    const int64_t twice = abs_remainder * 2;

    switch (mode) {
        case RoundMode::TowardZero:
            return quotient;
        case RoundMode::TowardNegInf:
            return negative ? quotient - 1 : quotient;
        case RoundMode::TowardPosInf:
            return negative ? quotient : quotient + 1;
        case RoundMode::NearestTiesAway:
            if (twice >= abs_divisor) {
                return negative ? quotient - 1 : quotient + 1;
            }
            return quotient;
        case RoundMode::NearestTiesEven:
            if (twice > abs_divisor || (twice == abs_divisor && (quotient & 1) != 0)) {
                return negative ? quotient - 1 : quotient + 1;
            }
            return quotient;
    }
    return quotient;
}

}  // namespace mathlib

#endif  // MATH_ROUND_MODE_H
