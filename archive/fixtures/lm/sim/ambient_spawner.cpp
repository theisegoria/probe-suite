// engine/sim/ambient_spawner.cpp
//
// Ambient wildlife.
//
// Critters are cosmetic in the sense that killing one does nothing for you,
// but they are simulated: they occupy space, they trip proximity mines, they
// block a doorway if enough of them pile into it, and the entity ids they
// take come out of the same allocator everything else uses. So they are part
// of the simulation and they land in the tick checksum like everything else.
//
// Spawn points come from the level package manifest by way of the asset
// registry (assets/spawn_registry.h).

#include "sim/ambient_spawner.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "assets/spawn_registry.h"
#include "core/types.h"
#include "math/sim_math.h"
#include "sim/rng.h"
#include "sim/world.h"

namespace sim {
namespace {

using assets::SpawnPointDesc;
using core::AssetId;
using core::EntityId;
using core::Q16;

constexpr uint32_t kSpawnIntervalTicks = 300;
constexpr uint32_t kMaxLiveCritters    = 12;
constexpr float    kCrowdingRadius     = 8.0f;
constexpr float    kPlayerKeepOut      = 20.0f;
constexpr uint32_t kDespawnDistance    = 180;

// Biomes a critter species is willing to appear in. Indexed by species.
constexpr uint16_t kSpeciesBiomeMask[] = {
    0x0003,  // rats: sewer, basement
    0x000C,  // gulls: dock, roof
    0x0010,  // beetles: rubble
    0x0060,  // moths: interior, vent
};

bool BiomeAllows(uint8_t species, uint16_t biome) {
    if (species >= sizeof(kSpeciesBiomeMask) / sizeof(kSpeciesBiomeMask[0])) return false;
    return (kSpeciesBiomeMask[species] & (1u << (biome & 15u))) != 0;
}

}  // namespace

// ------------------------------------------------------------ validation

// Runs once when a level finishes streaming in. Walks the spawn points the
// manifest declared and complains about any that sit inside geometry or that
// name a biome no species can use, so that content problems show up in the
// level check rather than as a level with no wildlife in it.
uint32_t AmbientSpawner::ValidateSpawnPoints(const World& world, LogSink& sink) const {
    uint32_t bad = 0;

    for (AssetId id : assets::g_spawn_registry.ManifestIds()) {
        const SpawnPointDesc* desc = assets::g_spawn_registry.Find(id);
        if (desc == nullptr) continue;

        if (world.collision().PointInsideSolid(desc->position)) {
            sink.Line("spawn point %u (%s) is inside geometry", id, desc->debug_name.c_str());
            ++bad;
            continue;
        }

        bool any_species = false;
        for (uint8_t species = 0; species < kSpeciesCount; ++species) {
            if (BiomeAllows(species, desc->biome)) {
                any_species = true;
                break;
            }
        }

        if (!any_species) {
            sink.Line("spawn point %u (%s) has biome %u that no species uses",
                      id, desc->debug_name.c_str(), desc->biome);
            ++bad;
        }
    }

    return bad;
}

// The level check also wants a count per biome for the content report.
void AmbientSpawner::CountByBiome(uint32_t* counts, uint32_t count_len) const {
    for (uint32_t i = 0; i < count_len; ++i) counts[i] = 0;

    for (AssetId id : assets::g_spawn_registry.ManifestIds()) {
        const SpawnPointDesc* desc = assets::g_spawn_registry.Find(id);
        if (desc == nullptr) continue;
        const uint32_t biome = desc->biome;
        if (biome < count_len) counts[biome] += 1;
    }
}

// ------------------------------------------------------------- occupancy

uint32_t AmbientSpawner::LiveCritterCount(const World& world) const {
    uint32_t live = 0;
    for (core::Slot slot = 0; slot < world.actor_count(); ++slot) {
        const Actor& actor = world.actor(slot);
        if (actor.alive && actor.team == core::Team::kWildlife) ++live;
    }
    return live;
}

// True when something is already standing about at this spawn point.
bool AmbientSpawner::IsCrowded(const World& world, const SpawnPointDesc& desc) const {
    for (core::Slot slot = 0; slot < world.actor_count(); ++slot) {
        const Actor& actor = world.actor(slot);
        if (!actor.alive) continue;
        if (actor.team != core::Team::kWildlife) continue;

        const Vec3 delta = sim::Sub(actor.transform.position, desc.position);
        if (sim::LengthSq(delta) < kCrowdingRadius * kCrowdingRadius) return true;
    }
    return false;
}

// Critters do not pop in on top of a player. This is a simulation query
// against the simulation transforms, not against anything the renderer has.
bool AmbientSpawner::TooCloseToAnyPlayer(const World& world, const SpawnPointDesc& desc) const {
    for (core::Slot slot = 0; slot < world.player_count(); ++slot) {
        const Actor& player = world.player_actor(slot);
        if (!player.alive) continue;

        const Vec3 delta = sim::Sub(player.transform.position, desc.position);
        if (sim::LengthSq(delta) < kPlayerKeepOut * kPlayerKeepOut) return true;
    }
    return false;
}

// -------------------------------------------------------------- spawning

EntityId AmbientSpawner::SpawnCritterAt(World& world, const SpawnPointDesc& desc, uint8_t species) {
    ActorSpawn spawn;
    spawn.archetype = ArchetypeForSpecies(species);
    spawn.position = desc.position;
    spawn.team = core::Team::kWildlife;
    spawn.owner = core::kInvalidEntity;
    spawn.spawn_point = desc.id;

    // Entity ids come off the shared allocator in call order, so two peers
    // that spawn the same critters in a different order end up with the same
    // creatures wearing each other's ids.
    const EntityId entity = world.SpawnActor(spawn);

    Rng rng = Derive(world.match_seed, RngStream::kCosmetic, world.tick, desc.id);
    world.actor_by_entity(entity).wander_phase = rng.Index(64);

    ++spawned_total_;
    return entity;
}

uint8_t AmbientSpawner::PickSpecies(const World& world, const SpawnPointDesc& desc) const {
    uint8_t allowed[kSpeciesCount];
    uint8_t allowed_count = 0;

    for (uint8_t species = 0; species < kSpeciesCount; ++species) {
        if (BiomeAllows(species, desc.biome)) allowed[allowed_count++] = species;
    }

    if (allowed_count == 0) return 0;

    Rng rng = Derive(world.match_seed, RngStream::kDirector, world.tick, desc.id);
    return allowed[rng.Index(allowed_count)];
}

// --------------------------------------------------------------- despawn

// Critters a long way from every player are removed so the ambient population
// does not accumulate across a whole level. Walked in slot order, removed in
// slot order.
void AmbientSpawner::DespawnDistant(World& world) {
    for (core::Slot slot = 0; slot < world.actor_count(); ++slot) {
        Actor& actor = world.actor(slot);
        if (!actor.alive) continue;
        if (actor.team != core::Team::kWildlife) continue;

        float nearest_sq = 1.0e30f;
        for (core::Slot p = 0; p < world.player_count(); ++p) {
            const Actor& player = world.player_actor(p);
            if (!player.alive) continue;
            const Vec3 delta = sim::Sub(player.transform.position, actor.transform.position);
            nearest_sq = sim::Min(nearest_sq, sim::LengthSq(delta));
        }

        if (nearest_sq > static_cast<float>(kDespawnDistance * kDespawnDistance)) {
            world.RetireActor(actor.entity);
        }
    }
}

// ------------------------------------------------------------------ tick

void AmbientSpawner::Tick(World& world) {
    if ((world.tick % kSpawnIntervalTicks) != 0) {
        return;
    }

    DespawnDistant(world);

    if (assets::g_spawn_registry.Empty()) return;
    if (LiveCritterCount(world) >= kMaxLiveCritters) return;

    // Nothing spawns yet: the wildlife pass was cut before the last milestone
    // and only the validation and despawn halves were kept.
}

}  // namespace sim
