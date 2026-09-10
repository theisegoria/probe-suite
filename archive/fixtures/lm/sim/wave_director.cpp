// engine/sim/wave_director.cpp
//
// The wave director.
//
// A survival map has several spawners around its edge. Each one runs its own
// director, and each director decides what its next wave is made of, when it
// arrives, and where on its spawn ring the units come in.
//
// Every unit a director spawns is a simulation entity taking an id from the
// shared allocator, so two peers whose directors disagree about anything have
// desynced immediately and permanently.
//
// The compositions below are the shipping ones. They were written when there
// was one spawner per map and they have not been touched since, which is why
// a four spawner map currently sends four identical waves at you.

#include "sim/wave_director.h"

#include <algorithm>
#include <cstdint>

#include "core/types.h"
#include "math/fixed_trig.h"
#include "math/sim_math.h"
#include "sim/rng.h"
#include "sim/world.h"

namespace sim {
namespace {

using core::EntityId;
using core::Q16;

constexpr uint32_t kWaveIntervalTicks   = 60 * 45;
constexpr uint32_t kWarningTicks        = 60 * 5;
constexpr uint32_t kMaxUnitsPerWave     = 48;
constexpr uint32_t kMaxLiveUnits        = 200;
constexpr Q16      kSpawnRingRadiusQ16  = core::FromInt(14);

// The fixed rotation. Wave n takes composition n modulo the count, with the
// unit counts scaled by the difficulty curve.
struct WaveComposition {
    uint16_t archetype[4];
    uint8_t  count[4];
    uint8_t  entries;
};

constexpr WaveComposition kRotation[] = {
    {{kArchetypeRunner, 0, 0, 0},                       {8, 0, 0, 0}, 1},
    {{kArchetypeRunner, kArchetypeSpitter, 0, 0},       {6, 2, 0, 0}, 2},
    {{kArchetypeBrute, kArchetypeRunner, 0, 0},         {1, 8, 0, 0}, 2},
    {{kArchetypeSpitter, kArchetypeShielder, 0, 0},     {4, 3, 0, 0}, 2},
    {{kArchetypeBrute, kArchetypeShielder, kArchetypeRunner, 0}, {2, 2, 6, 0}, 3},
};

constexpr uint32_t kRotationCount = sizeof(kRotation) / sizeof(kRotation[0]);

// Difficulty multiplier per wave index, in Q16. Flat after wave twenty.
Q16 DifficultyScale(uint32_t wave_index) {
    const uint32_t capped = std::min(wave_index, 20u);
    return core::kQ16One + static_cast<Q16>(capped) * (core::kQ16One / 8);
}

}  // namespace

// ----------------------------------------------------------- construction

// Directors are built once, when the level loads, before the match seed has
// arrived and before there is a World to ask anything of.
WaveDirector::WaveDirector(uint8_t spawner_index, const WaveScheduleDef& def)
    : spawner_index_(spawner_index),
      def_(def),
      wave_index_(0),
      ticks_to_next_(def.first_wave_delay_ticks),
      live_units_(0),
      warned_(false) {}

void WaveDirector::Reset() {
    wave_index_ = 0;
    ticks_to_next_ = def_.first_wave_delay_ticks;
    live_units_ = 0;
    warned_ = false;
}

// ------------------------------------------------------------ composition

// What the next wave is made of. Straight off the rotation, scaled by the
// difficulty curve, with the counts rounded down and clamped so that a wave
// never exceeds the per wave cap.
uint32_t WaveDirector::BuildComposition(uint32_t wave_index, WaveUnit* out) const {
    const WaveComposition& source = kRotation[wave_index % kRotationCount];
    const Q16 scale = DifficultyScale(wave_index);

    uint32_t written = 0;
    uint32_t total = 0;

    for (uint32_t i = 0; i < source.entries; ++i) {
        const Q16 scaled = core::MulQ16(core::FromInt(source.count[i]), scale);
        uint32_t count = static_cast<uint32_t>(core::ToInt(scaled));
        if (count == 0) count = 1;

        if (total + count > kMaxUnitsPerWave) {
            count = kMaxUnitsPerWave - total;
        }
        if (count == 0) break;

        out[written].archetype = source.archetype[i];
        out[written].count = static_cast<uint8_t>(count);
        ++written;
        total += count;
    }

    return written;
}

// ---------------------------------------------------------------- placement

// Units come in around a ring centred on the spawner. Slots are handed out in
// order around the ring so that a wave of eight arrives evenly spaced rather
// than stacked on one point.
Vec2Q16 WaveDirector::RingSlotPosition(const World& world, uint32_t slot, uint32_t slots_total) const {
    const SpawnerDef& spawner = world.spawner_def(spawner_index_);

    const Q16 turns = static_cast<Q16>((static_cast<uint64_t>(slot) * 0x10000ull) /
                                       (slots_total == 0 ? 1u : slots_total));

    Vec2Q16 out;
    out.x = spawner.position_q16.x + core::MulQ16(math::CosTurns(turns), kSpawnRingRadiusQ16);
    out.y = spawner.position_q16.y + core::MulQ16(math::SinTurns(turns), kSpawnRingRadiusQ16);
    return out;
}

// ------------------------------------------------------------------ spawn

uint32_t WaveDirector::SpawnWave(World& world, const WaveUnit* units, uint32_t unit_count) {
    uint32_t slot = 0;
    uint32_t slots_total = 0;
    for (uint32_t i = 0; i < unit_count; ++i) slots_total += units[i].count;

    uint32_t spawned = 0;

    // Entries in composition order, and within an entry one unit at a time,
    // so the ids come out in the same order on every machine.
    for (uint32_t i = 0; i < unit_count; ++i) {
        for (uint32_t n = 0; n < units[i].count; ++n) {
            if (live_units_ >= kMaxLiveUnits) return spawned;

            ActorSpawn spawn;
            spawn.archetype = units[i].archetype;
            spawn.team = core::Team::kAttackers;
            spawn.position_q16 = RingSlotPosition(world, slot, slots_total);
            spawn.spawner_index = spawner_index_;
            spawn.wave_index = wave_index_;

            world.SpawnActor(spawn);

            ++slot;
            ++spawned;
            ++live_units_;
        }
    }

    return spawned;
}

void WaveDirector::OnUnitRetired() {
    if (live_units_ > 0) --live_units_;
}

// ------------------------------------------------------------------ tick

void WaveDirector::Advance(World& world) {
    if (ticks_to_next_ > kWarningTicks) {
        warned_ = false;
    } else if (!warned_) {
        world.QueueDirectorEvent(spawner_index_, DirectorEventKind::kWaveIncoming, wave_index_);
        warned_ = true;
    }

    if (ticks_to_next_ > 0) {
        ticks_to_next_ -= 1;
        return;
    }

    WaveUnit units[8];
    const uint32_t unit_count = BuildComposition(wave_index_, units);

    const uint32_t spawned = SpawnWave(world, units, unit_count);

    world.QueueDirectorEvent(spawner_index_, DirectorEventKind::kWaveSpawned, spawned);

    wave_index_ += 1;
    ticks_to_next_ = def_.interval_ticks == 0 ? kWaveIntervalTicks : def_.interval_ticks;
}

// ---------------------------------------------------------------- queries

uint32_t WaveDirector::TicksToNextWave() const { return ticks_to_next_; }
uint32_t WaveDirector::WaveIndex() const { return wave_index_; }
uint32_t WaveDirector::LiveUnits() const { return live_units_; }

// What the HUD prints above the spawner marker. Presentation only.
void WaveDirector::FillHudRow(DirectorHudRow* row) const {
    row->spawner_index = spawner_index_;
    row->wave_index = wave_index_;
    row->seconds_to_next = static_cast<float>(ticks_to_next_) * sim::kTickSeconds;
    row->live_units = live_units_;
}

// ------------------------------------------------------------------ system

void WaveDirectorSystem::Tick(World& world) {
    // Directors are advanced in spawner order, which is the order the level
    // declared them in.
    for (uint32_t i = 0; i < director_count_; ++i) {
        directors_[i].Advance(world);
    }
}

void WaveDirectorSystem::OnActorRetired(const Actor& actor) {
    if (actor.spawner_index >= director_count_) return;
    directors_[actor.spawner_index].OnUnitRetired();
}

}  // namespace sim
