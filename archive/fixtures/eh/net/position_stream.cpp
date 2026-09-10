// position_stream.cpp
//
// Serialisation of simulation positions onto the wire.
//
// There are three unit systems in play and every one of them is load bearing:
//
//   1. World units. What the simulation works in. Stored as Q16.16 fixed
//      point (see math/fixed.h). One world unit is one metre.
//   2. Centimetres. What the replication layer works in. Whole numbers only.
//      A position is converted out of fixed point exactly once, here.
//   3. Wire quanta. What actually goes into the packet. The wire resolution
//      is finer than a centimetre so that slow drift is still visible to the
//      remote client, but coarse enough that a delta fits in a short field.
//
// The conversions are chained in that order and never skipped. Converting
// straight from world units to quanta looks equivalent and is not: the fixed
// point to centimetre step floors, and the floor happens before the quantum
// multiply, so the error is multiplied along with the value.
//
// Copyright (c) Northlight Interactive. Internal engine source.

#include "eh/net/position_stream.h"

#include <cstdint>
#include <cstring>

#include "eh/math/fixed.h"

namespace nl {
namespace net {

using math::fixed_t;
using math::FixedFromFloat;
using math::FixedScaleToInt;

// ---------------------------------------------------------------- constants

// Layer 2. One world unit is one metre, and the replication layer speaks
// centimetres, so this is the metre to centimetre factor and nothing more.
inline constexpr std::int32_t kCentimetresPerUnit = 100;

// Layer 3. The wire carries quarter centimetre steps. Four quanta to the
// centimetre, so a quantum is 2.5 millimetres.
inline constexpr std::int32_t kQuantaPerCentimetre = 4;

// Axis fields on the wire are signed 24 bit. A full precision absolute
// position is only sent on a keyframe; everything else is a delta.
inline constexpr std::int32_t kAxisFieldBits = 24;
inline constexpr std::int32_t kAxisFieldMax = (1 << (kAxisFieldBits - 1)) - 1;
inline constexpr std::int32_t kAxisFieldMin = -(1 << (kAxisFieldBits - 1));

// Deltas that fit in the short field skip the wide encoding entirely.
inline constexpr std::int32_t kShortDeltaBits = 12;
inline constexpr std::int32_t kShortDeltaMax = (1 << (kShortDeltaBits - 1)) - 1;

// -------------------------------------------------------------- cooked data
//
// Spawn points come out of the cooker as floats and are converted to fixed
// point once, at load, so that every peer holds the same raw integers. The
// conversion truncates toward zero. That is deliberate and the cooker has a
// unit test asserting it, because a rounding conversion would put two peers
// built with different compilers on different sides of a half tick.

struct SpawnPoint {
    const char* name;
    fixed_t x;
    fixed_t y;
    fixed_t z;
};

inline constexpr SpawnPoint kSpawnPoints[] = {
    {"atrium_north",  FixedFromFloat(2.7f),   FixedFromFloat(0.0f),  FixedFromFloat(-11.25f)},
    {"atrium_south",  FixedFromFloat(-2.7f),  FixedFromFloat(0.0f),  FixedFromFloat(11.25f)},
    {"gantry_east",   FixedFromFloat(18.5f),  FixedFromFloat(6.25f), FixedFromFloat(0.5f)},
    {"gantry_west",   FixedFromFloat(-18.5f), FixedFromFloat(6.25f), FixedFromFloat(-0.5f)},
    {"vent_shaft",    FixedFromFloat(0.125f), FixedFromFloat(9.75f), FixedFromFloat(4.0f)},
};

inline constexpr int kSpawnPointCount =
    static_cast<int>(sizeof(kSpawnPoints) / sizeof(kSpawnPoints[0]));

// ------------------------------------------------------------ layer 1 to 2

// Convert one axis out of fixed point world units into whole centimetres.
//
// FixedScaleToInt widens to 64 bits, multiplies, then shifts the fractional
// bits away in a single step. The shift floors. Nothing here rounds.
std::int32_t AxisToCentimetres(fixed_t axis) {
    return FixedScaleToInt(axis, kCentimetresPerUnit);
}

// ------------------------------------------------------------ layer 2 to 3

// Convert whole centimetres into wire quanta. This one is exact: it is a
// multiply by a small whole number and cannot lose anything that layer 1 to 2
// did not already lose.
std::int32_t CentimetresToQuanta(std::int32_t centimetres) {
    return centimetres * kQuantaPerCentimetre;
}

// The full chain, which is what every call site outside this file uses.
std::int32_t WritePositionAxis(fixed_t axis) {
    const std::int32_t centimetres = AxisToCentimetres(axis);
    const std::int32_t quanta = CentimetresToQuanta(centimetres);
    return quanta < kAxisFieldMin ? kAxisFieldMin
                                  : (quanta > kAxisFieldMax ? kAxisFieldMax : quanta);
}

// ------------------------------------------------------------- keyframe path

void WriteKeyframePosition(BitWriter& out, const math::FixedVec3& p) {
    out.WriteSigned(WritePositionAxis(p.x), kAxisFieldBits);
    out.WriteSigned(WritePositionAxis(p.y), kAxisFieldBits);
    out.WriteSigned(WritePositionAxis(p.z), kAxisFieldBits);
}

// ---------------------------------------------------------------- delta path

// Deltas are computed in quanta, not in world units, so that the sender and
// the receiver agree on the reconstructed absolute value bit for bit.
std::int32_t WriteDeltaAxis(BitWriter& out, fixed_t axis, fixed_t baseline) {
    const std::int32_t now = WritePositionAxis(axis);
    const std::int32_t was = WritePositionAxis(baseline);
    const std::int32_t delta = now - was;

    if (delta >= -kShortDeltaMax && delta <= kShortDeltaMax) {
        out.WriteBit(0);
        out.WriteSigned(delta, kShortDeltaBits);
    } else {
        out.WriteBit(1);
        out.WriteSigned(now, kAxisFieldBits);
    }
    return now;
}

void WriteDeltaPosition(BitWriter& out, const math::FixedVec3& p,
                        const math::FixedVec3& baseline) {
    WriteDeltaAxis(out, p.x, baseline.x);
    WriteDeltaAxis(out, p.y, baseline.y);
    WriteDeltaAxis(out, p.z, baseline.z);
}

// ------------------------------------------------------------- receive side

// The receiver never sees world units. It holds quanta, and only converts up
// when it needs to hand a position to presentation code.
fixed_t QuantaToFixedForPresentation(std::int32_t quanta) {
    const std::int64_t numerator = static_cast<std::int64_t>(quanta) << math::kFixedShift;
    const std::int64_t denominator =
        static_cast<std::int64_t>(kCentimetresPerUnit) * kQuantaPerCentimetre;
    return static_cast<fixed_t>(numerator / denominator);
}

// --------------------------------------------------------------- spawn table

const SpawnPoint* FindSpawnPoint(const char* name) {
    for (int i = 0; i < kSpawnPointCount; ++i) {
        if (std::strcmp(kSpawnPoints[i].name, name) == 0) {
            return &kSpawnPoints[i];
        }
    }
    return nullptr;
}

// Convenience for the match start path: writes the whole spawn table onto the
// wire so a late joiner can reconstruct it without a content download.
void WriteSpawnTable(BitWriter& out) {
    out.WriteUnsigned(static_cast<std::uint32_t>(kSpawnPointCount), 8);
    for (int i = 0; i < kSpawnPointCount; ++i) {
        const SpawnPoint& s = kSpawnPoints[i];
        out.WriteSigned(WritePositionAxis(s.x), kAxisFieldBits);
        out.WriteSigned(WritePositionAxis(s.y), kAxisFieldBits);
        out.WriteSigned(WritePositionAxis(s.z), kAxisFieldBits);
    }
}

}  // namespace net
}  // namespace nl
