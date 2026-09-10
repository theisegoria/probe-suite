// engine/sim/rng.h
//
// The simulation random number generator.
//
// Every draw the simulation makes comes from a stream derived from the match
// seed, which arrives in the match start packet and is identical on every
// peer. A stream is derived, used and thrown away: nothing holds one across
// ticks, so nothing has to be saved into the rollback snapshot.
//
//   sim::Rng rng = sim::Derive(world.match_seed, sim::RngStream::kCombat,
//                              world.tick, shooter.id);
//   const Q16 spread = rng.RangeQ16(-def.spread, def.spread);
//
// The salt argument is what keeps two callers on the same tick from drawing
// the same numbers. Pass something that identifies the caller, an entity id
// or a slot index.

#pragma once

#include <cstdint>

#include "core/types.h"

namespace sim {

using core::Q16;

enum class RngStream : uint8_t {
    kCombat = 0,     // hit rolls, spread, critical hits
    kLoot = 1,       // drop tables
    kDirector = 2,   // wave composition and spawn placement
    kAi = 3,         // behaviour tie breaks
    kCosmetic = 4,   // presentation only, never read by the simulation
    kCount
};

// A small counter based generator. Cheap to construct, so callers construct
// one per use rather than keeping one around.
class Rng {
  public:
    explicit Rng(uint64_t state) : state_(state) {}

    uint32_t NextU32();

    // Uniform in [0, n), rejection sampled so the distribution does not skew
    // for values of n that do not divide 2^32.
    uint32_t Index(uint32_t n);

    // True with probability numerator/denominator.
    bool Chance(uint32_t numerator, uint32_t denominator);

    // Uniform in [lo, hi] inclusive, in Q16.
    Q16 RangeQ16(Q16 lo, Q16 hi);

    // Pick an index from a weight table. weights_sum must equal the sum of
    // the first count entries.
    uint32_t WeightedIndex(const uint16_t* weights, uint32_t count, uint32_t weights_sum);

    uint64_t state() const { return state_; }

  private:
    uint64_t state_;
};

// Derive a stream. Same arguments give the same stream on every peer and on
// every replay of the same match.
Rng Derive(uint64_t match_seed, RngStream stream, uint32_t tick, uint32_t salt);

}  // namespace sim
