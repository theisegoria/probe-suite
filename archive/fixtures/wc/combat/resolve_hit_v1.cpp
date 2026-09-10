// combat/resolve_hit_v1.cpp
//
// The 2.6 damage resolution path, kept verbatim.
//
// This is not dead code. The balance regression harness in tools/balance
// replays every recorded hit from the 2.6 match archive through this function
// and diffs it against the current path, which is how the design team tracks
// what a tuning change actually did to time to kill. It has to keep producing
// the numbers 2.6 produced, so nothing in here may be refactored to share
// constants with the current path: the constants below are a frozen copy on
// purpose.
//
// Do not include combat/resolve_hit.h from this file.

#include "combat/resolve_hit_v1.h"

#include "combat/damage_types.h"

namespace combat {
namespace legacy {
namespace {

// Frozen 2.6 tunables.
constexpr float kCritMultiplier = 2.0f;
constexpr float kFriendlyFireScale = 0.25f;
constexpr float kMinDamage = 1.0f;
constexpr float kArmourFractionCap = 0.90f;

float ClampArmourFraction(float fraction) {
    if (fraction < 0.0f) {
        return 0.0f;
    }
    if (fraction > kArmourFractionCap) {
        return kArmourFractionCap;
    }
    return fraction;
}

}  // namespace

// Damage resolution.
//
// in.base_damage already carries the weapon's range falloff; this function
// applies the critical multiplier, then armour, then the team scale, and
// finally floors the result so that a hit always registers as at least one
// point of damage against a live target.
float ResolveHit(const HitInput& in) {
    float damage = in.base_damage;

    if (in.is_critical) {
        damage *= kCritMultiplier;
    }

    damage *= (1.0f - ClampArmourFraction(in.armour_fraction));

    if (in.instigator_team == in.victim_team) {
        damage *= kFriendlyFireScale;
    }

    if (damage > 0.0f && damage < kMinDamage) {
        damage = kMinDamage;
    }
    return damage;
}

// Shield damage went through the same multiplier in 2.6, with the armour term
// skipped, because shields were modelled as armour that depletes.
float ResolveShieldHit(const HitInput& in) {
    float damage = in.base_damage;
    if (in.is_critical) {
        damage *= kCritMultiplier;
    }
    return damage;
}

}  // namespace legacy
}  // namespace combat
