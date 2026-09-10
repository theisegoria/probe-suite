// math/fixed_point.h
//
// Fixed point length type.
//
// Floating point is not allowed anywhere in the simulation. Two peers can and
// do get different results from the same float expression: different compiler
// versions contract a multiply and an add into a fused multiply add in
// different places, x87 and SSE round differently, and libm is not the same
// function on every platform. Fixed point removes the question.
//
// The type is a template over the scale so that each domain can pick the
// resolution it needs while sharing one implementation. Every instantiation
// lives in a using declaration next to the code that owns the domain: the
// cooked asset scale is at the bottom of this header, the runtime simulation
// scale is in sim_length.h.

#ifndef MATH_FIXED_POINT_H
#define MATH_FIXED_POINT_H

#include <cstdint>

namespace mathlib {

// Raw is the storage integer. UnitsPerMetre is how many raw units make up one
// metre, which is what fixes the resolution of the type.
template <typename Raw, int UnitsPerMetre>
class FixedLength {
public:
    using RawType = Raw;
    static constexpr int kUnitsPerMetre = UnitsPerMetre;

    constexpr FixedLength() : raw_(0) {}

    static constexpr FixedLength FromRaw(Raw raw) {
        FixedLength v;
        v.raw_ = raw;
        return v;
    }

    // Authoring helper. Only ever called at load time, from data that has
    // already been rounded by the cooker, never from simulation code.
    static constexpr FixedLength FromMetres(int whole_metres) {
        return FromRaw(static_cast<Raw>(whole_metres) * static_cast<Raw>(kUnitsPerMetre));
    }

    constexpr Raw raw() const { return raw_; }

    constexpr FixedLength operator+(FixedLength rhs) const { return FromRaw(raw_ + rhs.raw_); }
    constexpr FixedLength operator-(FixedLength rhs) const { return FromRaw(raw_ - rhs.raw_); }
    constexpr bool operator<(FixedLength rhs) const { return raw_ < rhs.raw_; }
    constexpr bool operator==(FixedLength rhs) const { return raw_ == rhs.raw_; }

    // Scaling by a unitless fixed point fraction, in 16.16. The intermediate
    // is widened so that the product cannot overflow before the shift.
    FixedLength ScaledBy(int32_t fraction_16_16) const {
        const int64_t wide = static_cast<int64_t>(raw_) * static_cast<int64_t>(fraction_16_16);
        return FromRaw(static_cast<Raw>(wide >> 16));
    }

private:
    Raw raw_;
};

// Cooked asset scale.
//
// Meshes, collision hulls and placement transforms are cooked into this scale.
// It is deliberately coarse: the cooker snaps to it, so a coarser grid means
// more shared vertices and smaller cooked data, and nothing in the asset
// pipeline needs to resolve anything finer than the physical build tolerance
// the art team works to.
constexpr int kAssetUnitsPerMetre = 2000;

using AssetLength = FixedLength<int32_t, kAssetUnitsPerMetre>;

struct AssetVec3 {
    AssetLength x;
    AssetLength y;
    AssetLength z;
};

}  // namespace mathlib

#endif  // MATH_FIXED_POINT_H
