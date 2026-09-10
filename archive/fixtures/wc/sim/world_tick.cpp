// sim/world_tick.cpp
//
// The outer tick loop.
//
// Fixed timestep, no variable step anywhere in the simulation. The renderer
// interpolates between the last two published transforms, which is why
// TransformSync publishes rather than writes in place.

#include "sim/world_tick.h"

#include "sim/command_buffer.h"
#include "sim/sim_rates.h"
#include "sim/tick_pipeline.h"
#include "sim/world.h"

namespace sim {
namespace {

// Accumulated real time that has not yet been turned into ticks. Capped so
// that a long stall does not produce a burst of catch up ticks that stalls the
// process further.
constexpr float kMaxAccumulatedSeconds = 0.25f;

}  // namespace

void WorldTick::Advance(World& world, float real_seconds) {
    accumulator_ += real_seconds;
    if (accumulator_ > kMaxAccumulatedSeconds) {
        accumulator_ = kMaxAccumulatedSeconds;
    }

    while (accumulator_ >= kSecondsPerTick) {
        accumulator_ -= kSecondsPerTick;

        const CommandBuffer& commands = command_source_->CommandsForTick(world.tick);

        // One entry point. Every peer in a match runs this same pipeline; the
        // editor preview pipeline is never reachable from a match.
        RunAuthoritativeTick(world, commands);

        world.tick += 1;
        state_hash_ = HashSimulationState(world);
        hash_log_.Record(world.tick, state_hash_);
    }

    // Presentation only. The interpolation alpha is not simulation state and
    // is allowed to differ per peer.
    world.presentation_alpha = accumulator_ / kSecondsPerTick;
}

uint64_t WorldTick::StateHash() const {
    return state_hash_;
}

}  // namespace sim
