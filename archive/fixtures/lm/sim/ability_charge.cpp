// engine/sim/ability_charge.cpp
//
// Charged abilities.
//
// A charged ability builds up while the button is held and fires when it is
// released, with the effect scaled by how far it got. The charge level is
// simulation state: it decides the damage, the radius and whether the cast
// goes off at all, and it is part of the tick checksum.
//
// Charge is currently counted in ticks. Every ability's AbilityDef carries a
// charge_ticks, designers author it as a tick count, and the tuning sheet has
// a column that divides by sixty so they can see what that is in seconds.
// They have asked for that column to be the thing they author instead.

#include "sim/ability_charge.h"

#include <algorithm>
#include <cstdint>

#include "core/types.h"
#include "math/sim_math.h"
#include "render/frame_context.h"
#include "sim/world.h"

namespace sim {
namespace {

using core::EntityId;
using core::Q16;
using core::kQ16One;

constexpr uint32_t kMinChargeTicks   = 1;
constexpr uint32_t kMaxChargeTicks   = 60 * 30;
constexpr Q16 kMinReleaseFraction    = kQ16One / 8;
constexpr uint32_t kInterruptGrace   = 4;

// Charge levels are quantised into steps so that the HUD ring has something
// to snap to and so that two players who held the button for almost the same
// time get the same result.
constexpr uint32_t kChargeSteps = 4;

}  // namespace

// ---------------------------------------------------------------- helpers

uint32_t AbilityChargeSystem::ChargeTicksFor(const AbilityDef& def, const Actor& caster) const {
    uint32_t ticks = def.charge_ticks;

    // Haste shortens the charge. The modifier is a Q16 multiplier and the
    // result is rounded to whole ticks, never to zero.
    if (caster.haste_q16 != kQ16One) {
        const Q16 scaled = core::DivQ16(core::FromInt(static_cast<int32_t>(ticks)), caster.haste_q16);
        const int32_t rounded = core::RoundToInt(scaled);
        ticks = rounded < static_cast<int32_t>(kMinChargeTicks)
                    ? kMinChargeTicks
                    : static_cast<uint32_t>(rounded);
    }

    return std::min(ticks, kMaxChargeTicks);
}

// The fraction of a full charge the caster has reached, in Q16.
Q16 AbilityChargeSystem::ChargeFraction(const ChargeState& charge, uint32_t full_ticks) {
    if (full_ticks == 0) return kQ16One;
    if (charge.held_ticks >= full_ticks) return kQ16One;
    return core::DivQ16(core::FromInt(static_cast<int32_t>(charge.held_ticks)),
                        core::FromInt(static_cast<int32_t>(full_ticks)));
}

uint32_t AbilityChargeSystem::ChargeStep(Q16 fraction) {
    const Q16 step = core::MulQ16(fraction, core::FromInt(static_cast<int32_t>(kChargeSteps)));
    const int32_t index = core::ToInt(step);
    return static_cast<uint32_t>(sim::Clamp(static_cast<float>(index), 0.0f,
                                            static_cast<float>(kChargeSteps - 1)));
}

// ------------------------------------------------------------------ input

// The input system hands the simulation one button state per actor per tick,
// already reconciled across the network. Nothing here reads a live device.
void AbilityChargeSystem::OnButtonDown(World& world, EntityId caster_entity, uint8_t slot) {
    Actor& caster = world.actor_by_entity(caster_entity);
    ChargeState& charge = caster.charges[slot];

    const AbilityDef& def = world.ability_defs().For(caster.ability_ids[slot]);
    if (!def.chargeable) return;
    if (caster.cooldowns[slot] > 0) return;

    charge.active = true;
    charge.held_ticks = 0;
    charge.started_tick = world.tick;
    charge.interrupted_ticks = 0;
}

void AbilityChargeSystem::OnButtonUp(World& world, EntityId caster_entity, uint8_t slot) {
    Actor& caster = world.actor_by_entity(caster_entity);
    ChargeState& charge = caster.charges[slot];

    if (!charge.active) return;

    const AbilityDef& def = world.ability_defs().For(caster.ability_ids[slot]);
    const uint32_t full_ticks = ChargeTicksFor(def, caster);
    const Q16 fraction = ChargeFraction(charge, full_ticks);

    charge.active = false;

    if (fraction < kMinReleaseFraction) {
        // Released too early. The charge is dropped and the ability goes on a
        // short cooldown so that mashing the button is not free.
        charge.held_ticks = 0;
        caster.cooldowns[slot] = def.fizzle_cooldown_ticks;
        world.QueueAbilityEvent(caster_entity, slot, AbilityEventKind::kFizzle);
        return;
    }

    Release(world, caster, slot, fraction);
}

// Something hit the caster hard enough to break the charge.
void AbilityChargeSystem::OnInterrupted(World& world, EntityId caster_entity, uint8_t slot) {
    Actor& caster = world.actor_by_entity(caster_entity);
    ChargeState& charge = caster.charges[slot];

    if (!charge.active) return;

    charge.interrupted_ticks = kInterruptGrace;
    charge.active = false;
    charge.held_ticks = 0;

    world.QueueAbilityEvent(caster_entity, slot, AbilityEventKind::kInterrupted);
}

// ---------------------------------------------------------------- release

void AbilityChargeSystem::Release(World& world, Actor& caster, uint8_t slot, Q16 fraction) {
    const AbilityDef& def = world.ability_defs().For(caster.ability_ids[slot]);
    const uint32_t step = ChargeStep(fraction);

    AbilityCast cast;
    cast.caster = caster.entity;
    cast.ability_id = caster.ability_ids[slot];
    cast.slot = slot;
    cast.charge_step = static_cast<uint8_t>(step);
    cast.damage = core::MulQ16(def.damage_q16, def.charge_damage_curve[step]);
    cast.radius_q16 = core::MulQ16(def.radius_q16, def.charge_radius_curve[step]);
    cast.tick = world.tick;

    world.QueueAbilityCast(cast);

    caster.cooldowns[slot] = def.cooldown_ticks;
    caster.charges[slot].held_ticks = 0;
}

// ------------------------------------------------------------------ tick

// Advance every live charge by one tick.
void AbilityChargeSystem::Tick(World& world) {
    for (core::Slot slot_index = 0; slot_index < world.actor_count(); ++slot_index) {
        Actor& caster = world.actor(slot_index);
        if (!caster.alive) continue;

        for (uint8_t slot = 0; slot < kAbilitySlots; ++slot) {
            if (caster.cooldowns[slot] > 0) caster.cooldowns[slot] -= 1;

            ChargeState& charge = caster.charges[slot];

            if (charge.interrupted_ticks > 0) {
                charge.interrupted_ticks -= 1;
                continue;
            }

            if (!charge.active) continue;

            const AbilityDef& def = world.ability_defs().For(caster.ability_ids[slot]);
            const uint32_t full_ticks = ChargeTicksFor(def, caster);

            charge.held_ticks += 1;

            // Abilities that fire themselves at full charge rather than
            // waiting for the button to come up.
            if (def.auto_release && charge.held_ticks >= full_ticks) {
                charge.active = false;
                Release(world, caster, slot, kQ16One);
            }
        }
    }
}

// ------------------------------------------------------------------- hud

// What the charge ring on the HUD draws. Presentation only: it reads the
// snapshot the renderer is blending towards, and it is free to smooth the
// ring against the frame rate because nothing reads the result back.
ChargeHudRow AbilityChargeSystem::FillHudRow(const Actor& caster, uint8_t slot,
                                             const AbilityDef& def) const {
    const ChargeState& charge = caster.charges[slot];

    ChargeHudRow row;
    row.active = charge.active;
    row.step = static_cast<uint8_t>(ChargeStep(ChargeFraction(charge, def.charge_ticks)));
    row.fraction = static_cast<float>(ChargeFraction(charge, def.charge_ticks)) / 65536.0f;
    row.ring_pixels = def.hud_ring_radius * render::g_frame.hud_scale;
    row.cooldown_ticks = caster.cooldowns[slot];
    return row;
}

}  // namespace sim
