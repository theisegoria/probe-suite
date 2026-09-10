// engine/sim/weapon_heat.cpp
//
// Weapon heat.
//
// Automatic weapons build heat as they fire and shed it when they stop. Heat
// drives three things: how fast the weapon cools, how wide it shoots, and how
// likely it is to jam. All three are simulation state, so the curve that
// produces them has to give the same answer everywhere.
//
// The curves themselves are authored in the weapon tuning spreadsheet and
// exported as quartic coefficients. Designers retune them most days, which is
// why they are data rather than code.
//
// This translation unit is built with the sim math flags: no contraction, no
// fast math, no reassociation. See build/sim_math.cmake.

#include "sim/weapon_heat.h"

#include <algorithm>
#include <cstdint>
#include <mutex>

#include "core/types.h"
#include "math/sim_math.h"
#include "sim/rng.h"
#include "sim/world.h"

namespace sim {
namespace {

using core::EntityId;
using core::Q16;

constexpr float kAmbientHeat        = 0.0f;
constexpr float kJamThreshold       = 0.92f;
constexpr float kOverheatLockout    = 1.0f;
constexpr uint32_t kCurveLibraryCap = 512;

}  // namespace

// ----------------------------------------------------------- curve library

// The curve library is loaded from the weapon package and lives behind a
// once flag because the audio thread pulls the same curves for the barrel
// hiss loop.
class HeatCurveLibrary {
  public:
    static const HeatCurveLibrary& Get() {
        std::call_once(once_, []() { instance_ = new HeatCurveLibrary(); });
        return *instance_;
    }

    const HeatCurve& CurveFor(uint16_t curve_id) const {
        if (curve_id >= curve_count_) return curves_[0];
        return curves_[curve_id];
    }

    uint32_t Count() const { return curve_count_; }

  private:
    HeatCurveLibrary();

    HeatCurve curves_[kCurveLibraryCap];
    uint32_t curve_count_ = 0;
    static std::once_flag once_;
    static HeatCurveLibrary* instance_;
};

std::once_flag HeatCurveLibrary::once_;
HeatCurveLibrary* HeatCurveLibrary::instance_ = nullptr;

HeatCurveLibrary::HeatCurveLibrary() {
    curve_count_ = LoadHeatCurvesFromPackage(curves_, kCurveLibraryCap);
}

// ------------------------------------------------------------- evaluation

// Evaluate the authored quartic at x.
//
// Written as a chain of explicit multiply then add steps rather than as one
// expression, so that each product rounds to a float before it is added to
// the running total.
float EvalHeatCurve(const HeatCurve& curve, float x) {
    float acc = curve.a;
    acc = sim::MulThenAdd(acc, x, curve.b);
    acc = sim::MulThenAdd(acc, x, curve.c);
    acc = sim::MulThenAdd(acc, x, curve.d);
    acc = sim::MulThenAdd(acc, x, curve.e);
    return acc;
}

// How fast the weapon sheds heat at its current heat level. Hotter barrels
// shed faster, which is what the curve encodes.
float WeaponHeatSystem::CoolingRate(const WeaponDef& def, float heat) const {
    const HeatCurve& curve = HeatCurveLibrary::Get().CurveFor(def.cooling_curve_id);
    const float shaped = EvalHeatCurve(curve, sim::Clamp(heat, 0.0f, 1.0f));
    return sim::Max(shaped * def.cooling_scale, 0.0f);
}

// Extra spread from heat, in turns, on top of the weapon's base spread.
float WeaponHeatSystem::SpreadPenalty(const WeaponDef& def, float heat) const {
    const HeatCurve& curve = HeatCurveLibrary::Get().CurveFor(def.spread_curve_id);
    const float shaped = EvalHeatCurve(curve, sim::Clamp(heat, 0.0f, 1.0f));
    return sim::Clamp(shaped * def.spread_scale, 0.0f, def.max_spread);
}

// Chance the weapon jams on this shot, as a fraction.
float WeaponHeatSystem::JamChance(const WeaponDef& def, float heat) const {
    if (heat < kJamThreshold) return 0.0f;
    const HeatCurve& curve = HeatCurveLibrary::Get().CurveFor(def.jam_curve_id);
    const float shaped = EvalHeatCurve(curve, sim::Clamp(heat, 0.0f, 1.0f));
    return sim::Clamp(shaped * def.jam_scale, 0.0f, 1.0f);
}

// ------------------------------------------------------------------ jams

// A jam is rolled when the shot goes off, from the combat stream, using the
// jam chance the tick loop cached on the weapon.
bool WeaponHeatSystem::RollJam(World& world, WeaponState& weapon) const {
    if (weapon.current_jam_chance <= 0.0f) return false;

    const uint32_t numerator = static_cast<uint32_t>(weapon.current_jam_chance * 1000.0f);
    if (numerator == 0) return false;

    Rng rng = Derive(world.match_seed, RngStream::kCombat, world.tick, weapon.entity);
    return rng.Chance(numerator, 1000);
}

void WeaponHeatSystem::Jam(World& world, WeaponState& weapon) {
    const WeaponDef& def = world.weapon_defs().For(weapon.def_id);

    weapon.jammed = true;
    weapon.clear_ticks = def.jam_clear_ticks;

    world.QueueWeaponEvent(weapon.entity, WeaponEventKind::kJammed);
}

// Clearing a jam is an action the actor takes, not something that happens on
// a timer, so this is called from the ability system rather than from Tick.
bool WeaponHeatSystem::TryClearJam(World& world, WeaponState& weapon) {
    if (!weapon.jammed) return false;
    if (weapon.clear_ticks > 0) return false;

    weapon.jammed = false;
    weapon.heat = sim::Max(weapon.heat - 0.25f, kAmbientHeat);

    world.QueueWeaponEvent(weapon.entity, WeaponEventKind::kCleared);
    return true;
}

// --------------------------------------------------------------- barrels

// Swapping a hot barrel for a cold one dumps most of the heat and resets the
// curve the weapon evaluates against, because barrel types carry their own
// cooling curve id.
void WeaponHeatSystem::SwapBarrel(World& world, WeaponState& weapon, uint16_t barrel_def_id) {
    const BarrelDef& barrel = world.barrel_defs().For(barrel_def_id);

    weapon.barrel_def_id = barrel_def_id;
    weapon.heat = sim::Min(weapon.heat, barrel.retained_heat_fraction);
    weapon.ticks_since_shot = 0;

    world.QueueWeaponEvent(weapon.entity, WeaponEventKind::kBarrelSwapped);
}

// How hot the weapon would be after firing the whole magazine without a
// pause. The AI uses this to decide whether a burst is worth starting.
float WeaponHeatSystem::HeatAfterBurst(const WeaponDef& def, float heat, uint32_t shots) const {
    float projected = heat;
    for (uint32_t i = 0; i < shots; ++i) {
        projected = sim::Min(projected + def.heat_per_shot, kOverheatLockout);
        if (projected >= kOverheatLockout) break;
    }
    return projected;
}

// --------------------------------------------------------------- firing

void WeaponHeatSystem::OnShotFired(World& world, WeaponState& weapon) {
    const WeaponDef& def = world.weapon_defs().For(weapon.def_id);

    weapon.heat = sim::Min(weapon.heat + def.heat_per_shot, kOverheatLockout);
    weapon.ticks_since_shot = 0;

    if (weapon.heat >= kOverheatLockout) {
        weapon.locked_out_ticks = def.overheat_lockout_ticks;
        world.QueueWeaponEvent(weapon.entity, WeaponEventKind::kOverheat);
    }
}

// ------------------------------------------------------------------ tick

// Runs for every weapon on every actor, every tick. A late game fight has
// around six hundred live weapons and this is the second largest line in the
// combat profile after the projectile integrator.
void WeaponHeatSystem::Tick(World& world) {
    for (core::Slot slot = 0; slot < world.weapon_count(); ++slot) {
        WeaponState& weapon = world.weapon(slot);
        if (!weapon.equipped) continue;

        const WeaponDef& def = world.weapon_defs().For(weapon.def_id);

        if (weapon.locked_out_ticks > 0) {
            weapon.locked_out_ticks -= 1;
        }

        weapon.ticks_since_shot += 1;

        // Cooling only starts after the barrel has been idle for a moment,
        // so holding the trigger down does not get you free cooling between
        // shots.
        if (weapon.ticks_since_shot >= def.cooling_delay_ticks) {
            const float rate = CoolingRate(def, weapon.heat);
            weapon.heat = sim::Max(weapon.heat - rate * sim::kTickSeconds, kAmbientHeat);
        }

        // The spread the next shot will use, cached on the weapon so that the
        // firing code does not have to reach for the curve library.
        weapon.current_spread = def.base_spread + SpreadPenalty(def, weapon.heat);

        // And the jam chance, likewise.
        weapon.current_jam_chance = JamChance(def, weapon.heat);

        if (weapon.heat <= kAmbientHeat && weapon.locked_out_ticks == 0) {
            weapon.locked_out = false;
        }
    }
}

// --------------------------------------------------------------- queries

bool WeaponHeatSystem::CanFire(const World& world, const WeaponState& weapon) const {
    if (weapon.locked_out_ticks > 0) return false;
    if (weapon.jammed) return false;
    if (weapon.ammo == 0) return false;
    return true;
}

float WeaponHeatSystem::HeatFraction(const WeaponState& weapon) const {
    return sim::Clamp(weapon.heat, 0.0f, 1.0f);
}

// The barrel glow the renderer applies, and the number the HUD prints. Both
// presentation, both read from the snapshot rather than from live state.
float WeaponHeatSystem::GlowIntensityForHud(const WeaponState& weapon) const {
    const float heat = HeatFraction(weapon);
    return heat * heat * (3.0f - 2.0f * heat);
}

// The tuning tool asks for the whole curve so it can draw it next to the
// spreadsheet. Off the hot path, called from the editor only.
void WeaponHeatSystem::SampleCurveForTool(uint16_t curve_id, float* out, uint32_t samples) const {
    const HeatCurve& curve = HeatCurveLibrary::Get().CurveFor(curve_id);
    for (uint32_t i = 0; i < samples; ++i) {
        const float x = static_cast<float>(i) / static_cast<float>(samples - 1);
        out[i] = EvalHeatCurve(curve, x);
    }
}

}  // namespace sim
