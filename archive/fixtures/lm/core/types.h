// engine/core/types.h
//
// Primitive types shared by the simulation and by the presentation layer.
//
// Simulation structures are trivially copyable. The rollback buffer memcpys
// whole component arrays when it rewinds a match, so nothing declared here
// may own heap memory or carry a vtable. If you need a container inside a
// simulation component, use one of the fixed capacity ones in core/small.h.
//
// Fixed point convention:
//   Q16 is a signed 16.16 value, kQ16One is 1.0.
//   Q32 is a signed 32.32 value, used by accumulators that have to survive a
//   forty minute match without losing their low bits.
//   Angles are stored as turns in Q16: a whole circle is 0x10000, so
//   wraparound costs nothing and there is no representable angle outside the
//   circle.

#pragma once

#include <cstddef>
#include <cstdint>

namespace core {

using EntityId   = uint32_t;
using TickIndex  = uint32_t;
using AssetId    = uint32_t;
using Q16        = int32_t;
using Q32        = int64_t;

inline constexpr EntityId kInvalidEntity = 0xFFFFFFFFu;
inline constexpr AssetId  kInvalidAsset  = 0u;
inline constexpr uint32_t kMaxEntities   = 16384;
inline constexpr uint32_t kTickHz        = 60;
inline constexpr Q16      kQ16One        = 1 << 16;
inline constexpr Q16      kQ16Half       = 1 << 15;

// The simulation timestep is fixed for the life of a build. Changing it
// changes the replay format version, see net/replay_header.h.
inline constexpr float kTickSeconds = 1.0f / static_cast<float>(kTickHz);

inline constexpr Q16 MulQ16(Q16 a, Q16 b) {
    return static_cast<Q16>((static_cast<int64_t>(a) * static_cast<int64_t>(b)) >> 16);
}

inline constexpr Q16 DivQ16(Q16 a, Q16 b) {
    return static_cast<Q16>((static_cast<int64_t>(a) << 16) / static_cast<int64_t>(b));
}

inline constexpr Q16 FromInt(int32_t v) { return static_cast<Q16>(v) << 16; }
inline constexpr int32_t ToInt(Q16 v)   { return v >> 16; }

// Round to nearest, ties away from zero. Used wherever a Q16 has to become a
// whole number of hit points, stacks or gold pieces.
inline constexpr int32_t RoundToInt(Q16 v) {
    return v >= 0 ? ((v + kQ16Half) >> 16) : -((-v + kQ16Half) >> 16);
}

inline constexpr Q16 ClampQ16(Q16 v, Q16 lo, Q16 hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline constexpr uint32_t SecondsToTicks(uint32_t seconds) {
    return seconds * kTickHz;
}

// Seconds authored by designers arrive as Q16 and are converted once, at load
// time, so that the simulation only ever counts whole ticks.
inline constexpr uint32_t Q16SecondsToTicks(Q16 seconds) {
    const int64_t ticks = (static_cast<int64_t>(seconds) * kTickHz + kQ16Half) >> 16;
    return ticks < 1 ? 1u : static_cast<uint32_t>(ticks);
}

struct EntityHandle {
    EntityId id;
    uint32_t generation;

    bool operator==(const EntityHandle& o) const {
        return id == o.id && generation == o.generation;
    }
};

// Dense slot index into a component array. Slots are handed out by the entity
// allocator in a fixed order derived from the input stream, so slot order is
// the same on every peer and is the canonical order for anything that has to
// be walked.
using Slot = uint32_t;
inline constexpr Slot kInvalidSlot = 0xFFFFFFFFu;

enum class Team : uint8_t {
    kNeutral = 0,
    kAttackers = 1,
    kDefenders = 2,
    kWildlife = 3,
    kCount
};

}  // namespace core
