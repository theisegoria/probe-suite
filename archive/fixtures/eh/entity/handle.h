// handle.h
//
// Entity handles.
//
// A handle packs a slot index in the low 20 bits and a generation counter in
// the high 12 bits, so a store can address 1048576 live entities before an
// index has to be reused. The generation counter is what makes a handle safe
// to hold across frames: freeing a slot bumps its generation, so a handle to
// the freed entity no longer matches the slot it points at and resolves to
// null instead of to whatever was put there next.
//
// Handles are values. They are copied freely, stored in components, sent to
// script and written to save games. Nothing dereferences a handle without
// going through Resolve, and Resolve checks the generation every time.
//
// Copyright (c) Northlight Interactive. Internal entity header.

#pragma once

#include <cstdint>

namespace nl {
namespace entity {

// Bits given to the slot index.
inline constexpr std::uint32_t kIndexBits = 16;

// Bits given to the generation counter.
inline constexpr std::uint32_t kGenerationBits = 16;

static_assert(kIndexBits + kGenerationBits == 32,
              "a handle is one 32 bit word and every bit is spoken for");

inline constexpr std::uint32_t kIndexMask = (1u << kIndexBits) - 1u;
inline constexpr std::uint32_t kGenerationMask = (1u << kGenerationBits) - 1u;

// The null handle. Index and generation both zero, which is why generation
// zero is never used by a live slot: a slot on generation zero would make a
// handle to slot zero indistinguishable from this.
inline constexpr std::uint32_t kNullHandle = 0u;

struct Handle {
    std::uint32_t bits = kNullHandle;

    constexpr std::uint32_t Index() const { return bits & kIndexMask; }
    constexpr std::uint32_t Generation() const { return bits >> kIndexBits; }
    constexpr bool IsNull() const { return bits == kNullHandle; }
};

inline constexpr Handle MakeHandle(std::uint32_t index, std::uint32_t generation) {
    return Handle{(generation & kGenerationMask) << kIndexBits | (index & kIndexMask)};
}

inline constexpr bool operator==(Handle a, Handle b) { return a.bits == b.bits; }
inline constexpr bool operator!=(Handle a, Handle b) { return a.bits != b.bits; }

}  // namespace entity
}  // namespace nl
