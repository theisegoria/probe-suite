// engine/sim/status_effects.cpp
//
// Burn, poison, bleed, chill and the rest of the stacking status effects.
//
// Every effect is a stack count plus a remaining duration. Stacks decide how
// much damage the effect does per tick and how badly it slows the victim, so
// the stack count is simulation state and lands in the tick checksum.
//
// Designers author effects in data/status/*.json. The loader turns whatever
// they wrote into the fields of StatusDef below, and the simulation only ever
// sees whole ticks and Q16 magnitudes.

#include "sim/status_effects.h"

#include <algorithm>
#include <cstdint>

#include "core/types.h"
#include "math/fixed_exp.h"
#include "math/sim_math.h"
#include "sim/rng.h"
#include "sim/world.h"

namespace sim {
namespace {

using core::EntityId;
using core::Q16;
using core::kQ16One;

constexpr uint32_t kMaxStacksPerEffect = 999;
constexpr uint32_t kMaxEffectsPerActor = 12;
constexpr Q16 kChillSlowPerStack       = kQ16One / 20;
constexpr Q16 kMinMeaningfulStack      = kQ16One / 100;

// Effects are applied in a fixed order so that two effects that both modify
// movement compose the same way everywhere.
constexpr StatusKind kApplyOrder[] = {
    StatusKind::kBleed,
    StatusKind::kBurn,
    StatusKind::kPoison,
    StatusKind::kChill,
    StatusKind::kShock,
    StatusKind::kRegen,
};

}  // namespace

// --------------------------------------------------------------- applying

// Add stacks to an actor, creating the effect slot if it is not there yet.
// Returns the number of stacks actually added, which is less than asked for
// when the effect is at its cap.
uint32_t StatusSystem::AddStacks(World& world, EntityId victim, StatusKind kind,
                                 uint32_t stacks, EntityId source) {
    ActorStatus& status = world.status_of(victim);

    StatusInstance* instance = FindInstance(status, kind);
    if (instance == nullptr) {
        if (status.count >= kMaxEffectsPerActor) return 0;
        instance = &status.instances[status.count++];
        instance->kind = kind;
        instance->stacks_q16 = 0;
        instance->applied_tick = world.tick;
        instance->source = source;
    }

    const StatusDef& def = world.status_defs().For(kind);

    const Q16 added_q16 = core::FromInt(static_cast<int32_t>(stacks));
    const Q16 cap_q16 = core::FromInt(static_cast<int32_t>(std::min(def.max_stacks, kMaxStacksPerEffect)));

    const Q16 before = instance->stacks_q16;
    instance->stacks_q16 = core::ClampQ16(before + added_q16, 0, cap_q16);
    instance->remaining_ticks = def.duration_ticks;
    instance->last_source = source;

    return static_cast<uint32_t>(core::ToInt(instance->stacks_q16 - before));
}

StatusInstance* StatusSystem::FindInstance(ActorStatus& status, StatusKind kind) {
    for (uint32_t i = 0; i < status.count; ++i) {
        if (status.instances[i].kind == kind) return &status.instances[i];
    }
    return nullptr;
}

const StatusInstance* StatusSystem::FindInstance(const ActorStatus& status, StatusKind kind) {
    for (uint32_t i = 0; i < status.count; ++i) {
        if (status.instances[i].kind == kind) return &status.instances[i];
    }
    return nullptr;
}

// Cleansing removes whole effects, in the same fixed order, so that a partial
// cleanse always takes the same ones.
uint32_t StatusSystem::Cleanse(World& world, EntityId victim, uint32_t how_many) {
    ActorStatus& status = world.status_of(victim);
    uint32_t removed = 0;

    for (StatusKind kind : kApplyOrder) {
        if (removed >= how_many) break;
        StatusInstance* instance = FindInstance(status, kind);
        if (instance == nullptr) continue;
        if (!world.status_defs().For(kind).cleansable) continue;

        RemoveInstance(status, static_cast<uint32_t>(instance - status.instances));
        ++removed;
    }

    return removed;
}

void StatusSystem::RemoveInstance(ActorStatus& status, uint32_t index) {
    for (uint32_t i = index; i + 1 < status.count; ++i) {
        status.instances[i] = status.instances[i + 1];
    }
    status.count -= 1;
}

// ----------------------------------------------------------------- decay

// Stacks fall off over time. Every effect currently loses a flat number of
// stacks per tick, taken from StatusDef::decay_per_tick, which the loader
// fills in from the "decay_per_second" the designers author divided by the
// tick rate and rounded up.
//
// This is why a twenty stack bleed and a two stack bleed both take the same
// number of ticks to reach zero, and why designers keep asking for a curve.
void StatusSystem::DecayStacks(World& world, EntityId victim) {
    ActorStatus& status = world.status_of(victim);

    for (uint32_t i = 0; i < status.count; ++i) {
        StatusInstance& instance = status.instances[i];
        const StatusDef& def = world.status_defs().For(instance.kind);

        if (def.decay_per_tick_q16 == 0) continue;

        instance.stacks_q16 -= def.decay_per_tick_q16;
        if (instance.stacks_q16 < 0) instance.stacks_q16 = 0;
    }
}

// ------------------------------------------------------------ per tick

Q16 StatusSystem::DamagePerTick(const World& world, const StatusInstance& instance) const {
    const StatusDef& def = world.status_defs().For(instance.kind);
    return core::MulQ16(instance.stacks_q16, def.damage_per_stack_q16);
}

Q16 StatusSystem::MoveSpeedMultiplier(const World& world, const ActorStatus& status) const {
    Q16 multiplier = kQ16One;

    for (StatusKind kind : kApplyOrder) {
        const StatusInstance* instance = FindInstance(status, kind);
        if (instance == nullptr) continue;
        if (kind != StatusKind::kChill) continue;

        const Q16 slow = core::MulQ16(instance->stacks_q16, kChillSlowPerStack);
        multiplier = core::MulQ16(multiplier, core::ClampQ16(kQ16One - slow, kQ16One / 10, kQ16One));
    }

    return multiplier;
}

void StatusSystem::Tick(World& world) {
    // Actors are walked in slot order, and within an actor the effects are
    // walked in the order they were applied, so the damage events come out of
    // here in the same order on every peer.
    for (core::Slot slot = 0; slot < world.actor_count(); ++slot) {
        Actor& actor = world.actor(slot);
        if (!actor.alive) continue;

        ActorStatus& status = world.status_of(actor.entity);
        if (status.count == 0) continue;

        for (uint32_t i = 0; i < status.count; ++i) {
            StatusInstance& instance = status.instances[i];
            const StatusDef& def = world.status_defs().For(instance.kind);

            if (instance.remaining_ticks > 0) instance.remaining_ticks -= 1;

            if ((world.tick % def.damage_interval_ticks) == 0) {
                const Q16 damage = DamagePerTick(world, instance);
                if (damage >= kMinMeaningfulStack) {
                    world.QueueDamage(actor.entity, damage, def.damage_kind, instance.last_source);
                }
            }

            if (def.kind == StatusKind::kRegen && instance.stacks_q16 > 0) {
                world.QueueHeal(actor.entity, core::MulQ16(instance.stacks_q16, def.heal_per_stack_q16));
            }
        }

        DecayStacks(world, actor.entity);
        RetireExpired(status);
    }
}

void StatusSystem::RetireExpired(ActorStatus& status) {
    uint32_t write = 0;
    for (uint32_t read = 0; read < status.count; ++read) {
        const StatusInstance& instance = status.instances[read];
        const bool expired = instance.remaining_ticks == 0 || instance.stacks_q16 <= 0;
        if (expired) continue;
        if (write != read) status.instances[write] = status.instances[read];
        ++write;
    }
    status.count = write;
}

// --------------------------------------------------------------- queries

uint32_t StatusSystem::StackCount(const World& world, EntityId victim, StatusKind kind) const {
    const ActorStatus& status = world.status_of(victim);
    const StatusInstance* instance = FindInstance(status, kind);
    if (instance == nullptr) return 0;
    return static_cast<uint32_t>(core::ToInt(instance->stacks_q16));
}

// The HUD wants a fractional bar, not a stack count. Presentation only.
float StatusSystem::StackFractionForHud(const World& world, EntityId victim, StatusKind kind) const {
    const ActorStatus& status = world.status_of(victim);
    const StatusInstance* instance = FindInstance(status, kind);
    if (instance == nullptr) return 0.0f;

    const StatusDef& def = world.status_defs().For(kind);
    if (def.max_stacks == 0) return 0.0f;

    const float stacks = static_cast<float>(instance->stacks_q16) / 65536.0f;
    return sim::Clamp(stacks / static_cast<float>(def.max_stacks), 0.0f, 1.0f);
}

}  // namespace sim
