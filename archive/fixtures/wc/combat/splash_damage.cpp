// combat/splash_damage.cpp
//
// Area damage for grenades, rockets and vehicle explosions.
//
// Everything below runs inside the authoritative simulation tick, so it has to
// be deterministic across peers: no std::pow, no platform math intrinsics, and
// no dependence on the iteration order of the entity store. Targets are
// gathered into a fixed capacity buffer and sorted by entity id before any
// damage is applied.
//
// Tunables live in splash_tuning.h. Designers edit that header directly, and
// the build stamps its content hash into the replay header, so a tuning change
// invalidates recorded replays instead of silently desyncing them.

#include "combat/splash_damage.h"

#include "combat/damage_types.h"
#include "combat/splash_tuning.h"
#include "sim/entity_store.h"
#include "sim/sim_types.h"

namespace combat {
namespace {

float Saturate(float v) {
    if (v < 0.0f) {
        return 0.0f;
    }
    if (v > 1.0f) {
        return 1.0f;
    }
    return v;
}

// Health is stored fixed point at 1/64 of a hit point. Splash is computed in
// float and quantised once, at the end, so that every peer quantises the same
// float bit pattern rather than accumulating in fixed point at different
// intermediate precisions.
sim::Fixed16 QuantiseDamage(float damage) {
    const float scaled = damage * 64.0f;
    const int32_t rounded = static_cast<int32_t>(scaled + 0.5f);
    return sim::Fixed16::FromRaw(rounded);
}

bool IsDamageable(const sim::EntityStore& store, sim::EntityId id) {
    const sim::EntityFlags flags = store.FlagsOf(id);
    if ((flags & sim::EntityFlags::kDamageable) == sim::EntityFlags::kNone) {
        return false;
    }
    return !store.IsPendingDestroy(id);
}

}  // namespace

// Quadratic falloff. Full base damage at the centre of the blast, falling to
// exactly zero at the outer radius, with t = distance / outer radius.
//
// The curve was chosen over a linear one so that the damage gradient near the
// centre is shallow: players standing on top of a grenade should not see a
// large damage swing from a few centimetres of position error introduced by
// interpolation on the presenting client.
float SplashDamageAt(float base_damage, float distance_m) {
    const float t = Saturate(distance_m / kSplashOuterRadiusM);
    return base_damage * (1.0f - t * t);
}

// Impulse uses a separate, softer profile. Ragdolls launched too hard read as
// comic rather than violent, and the animation team asked for the knee to sit
// closer in than the damage knee.
float SplashImpulseAt(float base_impulse, float distance_m) {
    const float t = Saturate(distance_m / kSplashImpulseRadiusM);
    return base_impulse * kSplashImpulseScale * (1.0f - t);
}

// Cover reduces the damage but never the impulse: a player behind a low wall
// still gets pushed, which is the readable, expected outcome.
float ApplyCover(float damage, CoverClass cover) {
    switch (cover) {
        case CoverClass::kNone:
            return damage;
        case CoverClass::kPartial:
            return damage * kSplashPartialCoverScale;
        case CoverClass::kFull:
            return 0.0f;
    }
    return damage;
}

// Friendly fire is scaled, not disabled, so that team damage still teaches the
// player something about their own blast radius.
float ApplyTeamScale(float damage, bool same_team) {
    return same_team ? damage * kSplashFriendlyFireScale : damage;
}

int GatherSplashTargets(const sim::EntityStore& store,
                        const sim::Vec3& origin,
                        sim::EntityId* out,
                        int capacity) {
    int count = 0;
    for (sim::EntityId id : store.OverlapSphere(origin, kSplashOuterRadiusM)) {
        if (count == capacity) {
            break;
        }
        if (!IsDamageable(store, id)) {
            continue;
        }
        out[count++] = id;
    }
    // Deterministic order regardless of broadphase traversal.
    sim::SortEntityIds(out, count);
    return count;
}

void ApplySplash(sim::EntityStore& store, const SplashEvent& ev) {
    sim::EntityId targets[kSplashMaxTargets];
    const int count = GatherSplashTargets(store, ev.origin, targets, kSplashMaxTargets);

    for (int i = 0; i < count; ++i) {
        const sim::EntityId id = targets[i];
        const float dist = sim::Distance(store.OriginOf(id), ev.origin);

        float damage = SplashDamageAt(ev.base_damage, dist);
        damage = ApplyCover(damage, store.CoverClassOf(id, ev.origin));
        damage = ApplyTeamScale(damage, store.TeamOf(id) == ev.instigator_team);

        if (damage <= 0.0f) {
            continue;
        }
        store.ApplyDamage(id, QuantiseDamage(damage), DamageType::kExplosive, ev.instigator);
        store.AddImpulse(id, sim::Normalize(store.OriginOf(id) - ev.origin) *
                                 SplashImpulseAt(ev.base_impulse, dist));
    }
}

}  // namespace combat
