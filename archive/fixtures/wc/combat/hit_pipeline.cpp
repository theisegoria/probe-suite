// combat/hit_pipeline.cpp
//
// The per hit pipeline. A confirmed hit arrives here after the lag
// compensated trace has already picked a victim and a hit region. This file
// does the bookkeeping around the damage number; the damage number itself is
// produced by ResolveHit.
//
// Ordering note: range falloff is folded into HitInput::base_damage here,
// before ResolveHit is called, so that ResolveHit stays a pure function of the
// values in HitInput and can be replayed against recorded inputs.

#include "combat/hit_pipeline.h"

#include "combat/damage_types.h"
#include "combat/resolve_hit.h"
#include "combat/weapon_stats.h"
#include "sim/entity_store.h"
#include "sim/tick_context.h"

#if COMBAT_BALANCE_REGRESSION
// The balance harness re runs every recorded hit through the pre 2.7 damage
// path and reports the delta per weapon. It is compiled only in the tools
// build, but resolve_hit_v1.cpp is in the normal library target so that the
// harness does not need its own copy of the weapon tables.
#include "combat/resolve_hit_v1.h"
#endif

namespace combat {
namespace {

// Range falloff. Weapons carry two range stops; damage is full inside the near
// stop, scaled linearly to the far stop, and flat beyond it.
float RangeScale(const WeaponStats& w, float distance_m) {
    if (distance_m <= w.near_range_m) {
        return 1.0f;
    }
    if (distance_m >= w.far_range_m) {
        return w.far_range_scale;
    }
    const float t = (distance_m - w.near_range_m) / (w.far_range_m - w.near_range_m);
    return 1.0f + t * (w.far_range_scale - 1.0f);
}

// Hit regions are resolved by the trace, not here. The region only selects
// whether the hit is flagged critical; the multiplier itself belongs to
// ResolveHit so that every damage source shares one definition of a crit.
bool IsCriticalRegion(HitRegion region) {
    return region == HitRegion::kHead || region == HitRegion::kWeakPoint;
}

}  // namespace

HitOutcome ResolveConfirmedHit(sim::EntityStore& store,
                               const sim::TickContext& tick,
                               const ConfirmedHit& hit) {
    const WeaponStats& weapon = WeaponStatsFor(hit.weapon_id);

    HitInput in;
    in.base_damage = weapon.base_damage * RangeScale(weapon, hit.distance_m);
    in.armour_fraction = store.ArmourFractionOf(hit.victim, hit.region);
    in.is_critical = IsCriticalRegion(hit.region);
    in.damage_type = weapon.damage_type;
    in.instigator_team = store.TeamOf(hit.instigator);
    in.victim_team = store.TeamOf(hit.victim);

    const float final_damage = ResolveHit(in);

#if COMBAT_BALANCE_REGRESSION
    if (tick.recording_balance_deltas) {
        RecordBalanceDelta(hit.weapon_id, final_damage, legacy::ResolveHit(in));
    }
#endif

    HitOutcome out;
    out.damage = final_damage;
    out.was_critical = in.is_critical;
    out.killed = store.ApplyDamage(hit.victim, final_damage, in.damage_type, hit.instigator);

    // Hit markers are presentation only and are allowed to differ per peer.
    if (out.damage > 0.0f) {
        store.QueueHitMarker(hit.instigator, out.was_critical, out.killed);
    }
    return out;
}

}  // namespace combat
