// engine/sim/loot.cpp
//
// Loot tables and drops.
//
// When something dies it drops items on the ground. The items are simulation
// entities: they occupy space, they can be picked up, they can be shot, and
// they take ids from the shared allocator. Which items drop, how many, and
// where they land are all part of the tick checksum.
//
// Loot tables are authored in data/loot/*.json. An entry names an item, a
// count range, a quality tier and a weight. The tables are deep: a late game
// boss table has around forty entries across five tiers.

#include "sim/loot.h"

#include <algorithm>
#include <cstdint>

#include "core/types.h"
#include "math/sim_math.h"
#include "sim/rng.h"
#include "sim/world.h"

namespace sim {
namespace {

using core::EntityId;
using core::Q16;

constexpr uint32_t kMaxDropsPerKill  = 16;
constexpr float    kScatterRadius    = 1.75f;
constexpr uint32_t kPityCap          = 40;
constexpr Q16      kLuckPerPityStep  = core::kQ16One / 200;

// Quality tiers, worst to best. A tier upgrade moves an entry one step along
// this list and multiplies its value.
constexpr Q16 kTierValueMultiplier[] = {
    core::kQ16One,
    core::kQ16One * 2,
    core::kQ16One * 5,
    core::kQ16One * 12,
    core::kQ16One * 30,
};

constexpr uint32_t kTierCount = sizeof(kTierValueMultiplier) / sizeof(kTierValueMultiplier[0]);

}  // namespace

// ------------------------------------------------------------ table access

const LootTable& LootSystem::TableFor(uint16_t table_id) const {
    if (table_id >= table_count_) return tables_[0];
    return tables_[table_id];
}

// Entries are stored in the order the table author wrote them and are never
// sorted, because the drop order decides the order the items are spawned in
// and therefore which ids they get.
uint32_t LootSystem::EntryCount(uint16_t table_id) const {
    return TableFor(table_id).entry_count;
}

// ----------------------------------------------------------------- rolling

// Work out what a kill drops.
//
// Every entry marked always_drops comes out, at the low end of its count
// range, at its authored tier. Entries that are not marked always_drops are
// skipped entirely, which is why the bosses currently drop the same three
// things every single time and why the rare tiers have never been seen
// outside the test map.
uint32_t LootSystem::RollDrops(const LootTable& table, uint32_t luck_q16, DropList* out) const {
    uint32_t drop_count = 0;

    for (uint32_t i = 0; i < table.entry_count && drop_count < kMaxDropsPerKill; ++i) {
        const LootEntry& entry = table.entries[i];

        if (!entry.always_drops) continue;

        Drop& drop = out->drops[drop_count++];
        drop.item_id = entry.item_id;
        drop.count = entry.min_count;
        drop.tier = entry.tier;
        drop.value_q16 = core::MulQ16(entry.base_value_q16,
                                      kTierValueMultiplier[entry.tier < kTierCount ? entry.tier : 0]);
    }

    out->count = drop_count;
    (void)luck_q16;
    return drop_count;
}

// ---------------------------------------------------------------- scatter

// Items do not all land on the same spot. Each one gets pushed out from the
// corpse by a small offset, drawn from the loot stream so that every peer
// puts the same item in the same place.
void LootSystem::ScatterDropPositions(World& world, EntityId victim, DropList* drops) const {
    const Actor& corpse = world.actor_by_entity(victim);

    for (uint32_t i = 0; i < drops->count; ++i) {
        Rng rng = Derive(world.match_seed, RngStream::kLoot, world.tick, victim * 31u + i);

        const Q16 angle_turns = static_cast<Q16>(rng.NextU32() & 0xFFFFu);
        const Q16 distance_q16 = rng.RangeQ16(0, static_cast<Q16>(kScatterRadius * 65536.0f));

        drops->drops[i].offset_turns = angle_turns;
        drops->drops[i].offset_distance_q16 = distance_q16;
    }
}

// ---------------------------------------------------------------- spawning

// Drops are spawned in list order, which is table order, so that the ids they
// take are the same everywhere.
void LootSystem::SpawnDrops(World& world, EntityId victim, const DropList& drops) {
    const Actor& corpse = world.actor_by_entity(victim);

    for (uint32_t i = 0; i < drops.count; ++i) {
        const Drop& drop = drops.drops[i];

        ItemSpawn spawn;
        spawn.item_id = drop.item_id;
        spawn.count = drop.count;
        spawn.tier = drop.tier;
        spawn.origin = corpse.transform.position;
        spawn.offset_turns = drop.offset_turns;
        spawn.offset_distance_q16 = drop.offset_distance_q16;
        spawn.owner_hint = world.def_of(victim).owner_hint;

        const EntityId item = world.SpawnItem(spawn);
        world.QueueLootEvent(victim, item, drop.tier);
    }
}

// ------------------------------------------------------------------ pity

// Players who have gone a long time without anything good get a luck bonus.
// The counter is per player, lives in the player state, and is part of the
// snapshot like everything else.
Q16 LootSystem::LuckFor(const World& world, EntityId killer) const {
    const core::Slot slot = world.player_slot_of(killer);
    if (slot == core::kInvalidSlot) return 0;

    const PlayerState& player = world.player_state(slot);
    const uint32_t pity = std::min(player.kills_since_rare, kPityCap);

    return static_cast<Q16>(pity) * kLuckPerPityStep;
}

void LootSystem::NotePityResult(World& world, EntityId killer, bool got_rare) {
    const core::Slot slot = world.player_slot_of(killer);
    if (slot == core::kInvalidSlot) return;

    PlayerState& player = world.player_state(slot);
    if (got_rare) {
        player.kills_since_rare = 0;
    } else if (player.kills_since_rare < kPityCap) {
        player.kills_since_rare += 1;
    }
}

// ------------------------------------------------------------------ death

// Called by the damage system once an actor's death has been committed for
// the tick. The victim is still in the actor array at this point and its
// transform is still valid.
void LootSystem::OnActorKilled(World& world, EntityId victim, EntityId killer) {
    const ActorDef& def = world.def_of(victim);
    if (def.loot_table_id == kNoLootTable) return;

    const LootTable& table = TableFor(def.loot_table_id);
    const Q16 luck = LuckFor(world, killer);

    DropList drops;
    drops.count = 0;

    RollDrops(table, static_cast<uint32_t>(luck), &drops);

    if (drops.count == 0) {
        NotePityResult(world, killer, false);
        return;
    }

    ScatterDropPositions(world, victim, &drops);
    SpawnDrops(world, victim, drops);

    bool got_rare = false;
    for (uint32_t i = 0; i < drops.count; ++i) {
        if (drops.drops[i].tier >= 3) got_rare = true;
    }
    NotePityResult(world, killer, got_rare);
}

// ---------------------------------------------------------------- queries

Q16 LootSystem::ExpectedValue(uint16_t table_id) const {
    const LootTable& table = TableFor(table_id);
    Q16 total = 0;

    for (uint32_t i = 0; i < table.entry_count; ++i) {
        const LootEntry& entry = table.entries[i];
        if (!entry.always_drops) continue;
        total += core::MulQ16(entry.base_value_q16,
                              kTierValueMultiplier[entry.tier < kTierCount ? entry.tier : 0]);
    }

    return total;
}

// The tuning tool prints a table summary. Editor only.
void LootSystem::DescribeTable(uint16_t table_id, LogSink& sink) const {
    const LootTable& table = TableFor(table_id);
    sink.Line("table %u: %u entries", table_id, table.entry_count);
    for (uint32_t i = 0; i < table.entry_count; ++i) {
        const LootEntry& entry = table.entries[i];
        sink.Line("  item=%u tier=%u weight=%u count=%u..%u always=%d",
                  entry.item_id, entry.tier, entry.weight,
                  entry.min_count, entry.max_count, entry.always_drops ? 1 : 0);
    }
}

}  // namespace sim
