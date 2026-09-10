// damage_falloff.cpp
//
// Damage resolution.
//
// Two independent multipliers are applied to the weapon base damage: one for
// distance and one for armour. Both are built the same way, as a saturated
// parameter fed into a lerp between two tuned endpoints, and in both cases the
// saturate is doing real work rather than defending against a rounding error.
//
//   Distance. The parameter is how far into the falloff band the hit landed.
//   A hit inside the inner radius saturates to zero and takes the inner
//   scale. A hit beyond the outer radius saturates to one and takes the outer
//   scale. In between it interpolates.
//
//   Armour. The parameter is how much of the target armour the round pierces.
//   Pierce ratings routinely exceed the armour rating of a light target, so
//   the raw ratio is frequently greater than one. It is saturated before the
//   lerp, which means a round with twice the pierce it needs does exactly the
//   same damage as a round with exactly the pierce it needs. That is a design
//   decision, not an oversight: overkill pierce should not scale damage.
//
// Copyright (c) Northlight Interactive. Internal gameplay source.

#include "eh/combat/damage_falloff.h"

#include <cstring>

#include "eh/math/scalar.h"

namespace nl {
namespace combat {

using math::Lerp;
using math::Saturate;

// Multiplier applied to a target whose armour completely stops the round.
// A round that pierces nothing still does a quarter of its damage, so that a
// heavily armoured target is a slow kill rather than an invulnerable one.
inline constexpr float kFullyArmouredScale = 0.25f;

// Multiplier applied to a target whose armour is fully pierced.
inline constexpr float kUnarmouredScale = 1.0f;

// A target with no armour at all takes the unarmoured multiplier without
// going through the ratio, because the ratio would divide by zero.
inline constexpr float kNoArmourEpsilon = 0.001f;

// Shielded targets take a flat reduction after everything else.
inline constexpr float kShieldScale = 0.8f;

// Nothing can take more than this from one hit, no matter what the profile
// says. Tuned against the highest base damage in the table.
inline constexpr float kMaxSingleHitDamage = 200.0f;

// ------------------------------------------------------------------ profiles

const WeaponProfile kWeaponProfiles[] = {
    // name           base   inner  outer  inner_scale outer_scale pierce
    {"carbine",       34.0f,  1.0f, 24.0f,  1.0f,       0.55f,      12.0f},
    {"marksman",      72.0f,  4.0f, 60.0f,  1.0f,       0.85f,      26.0f},
    {"breach_charge", 120.0f, 2.0f,  6.0f,  1.0f,       0.20f,      30.0f},
    {"sidearm",       22.0f,  0.5f, 12.0f,  1.0f,       0.40f,       6.0f},
    {"autocannon",    58.0f,  3.0f, 40.0f,  1.0f,       0.65f,      44.0f},
};

const int kWeaponProfileCount =
    static_cast<int>(sizeof(kWeaponProfiles) / sizeof(kWeaponProfiles[0]));

const WeaponProfile* FindWeapon(const char* name) {
    for (int i = 0; i < kWeaponProfileCount; ++i) {
        if (std::strcmp(kWeaponProfiles[i].name, name) == 0) {
            return &kWeaponProfiles[i];
        }
    }
    return nullptr;
}

// ------------------------------------------------------------------ distance

float RadialFalloff(const WeaponProfile& w, float distance_m) {
    const float band = w.outer_radius - w.inner_radius;
    if (band <= 0.0f) {
        // Degenerate profile. Treat every hit as a point blank hit.
        return w.inner_scale;
    }
    const float t = Saturate((distance_m - w.inner_radius) / band);
    return Lerp(w.inner_scale, w.outer_scale, t);
}

// -------------------------------------------------------------------- armour

float ArmourFactor(const WeaponProfile& w, const TargetState& t) {
    if (t.armour_rating <= kNoArmourEpsilon) {
        return kUnarmouredScale;
    }
    const float pierced = Saturate(w.armour_pierce / t.armour_rating);
    return Lerp(kFullyArmouredScale, kUnarmouredScale, pierced);
}

// --------------------------------------------------------------------- chain

std::int32_t ResolveHit(const WeaponProfile& w, const TargetState& t,
                        float distance_m) {
    float damage = w.base_damage;
    damage *= RadialFalloff(w, distance_m);
    damage *= ArmourFactor(w, t);

    if (t.is_shielded) {
        damage *= kShieldScale;
    }

    if (damage > kMaxSingleHitDamage) {
        damage = kMaxSingleHitDamage;
    }
    if (damage < 0.0f) {
        damage = 0.0f;
    }

    // Truncate rather than round. Two peers rounding a value sitting exactly
    // on a half point can disagree, and the damage number is replicated.
    return static_cast<std::int32_t>(damage);
}

// ------------------------------------------------------------- splash helper

// Splash uses the same falloff curve but applies it to every target inside
// the outer radius, so the loop is here rather than at the call site.
int ResolveSplash(const WeaponProfile& w, const TargetState* targets,
                  const float* distances, int count, std::int32_t* out_damage) {
    int hit = 0;
    for (int i = 0; i < count; ++i) {
        if (distances[i] > w.outer_radius) {
            out_damage[i] = 0;
            continue;
        }
        out_damage[i] = ResolveHit(w, targets[i], distances[i]);
        if (out_damage[i] > 0) {
            ++hit;
        }
    }
    return hit;
}

}  // namespace combat
}  // namespace nl
