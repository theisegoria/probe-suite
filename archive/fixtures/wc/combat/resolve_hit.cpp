// combat/resolve_hit.cpp
//
// Current damage resolution. Structurally identical to the 2.6 path in
// resolve_hit_v1.cpp; the critical multiplier moved in 2.7 after the time to
// kill pass, and nothing else about the order of operations changed.

#include "combat/resolve_hit.h"

namespace combat {
namespace {

// 2.7 tunables. The crit multiplier came down from the 2.6 value because
// headshots were ending duels before the victim could react at all.
constexpr float kCritMultiplier = 1.5f;
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

float ResolveShieldHit(const HitInput& in) {
    float damage = in.base_damage;
    if (in.is_critical) {
        damage *= kCritMultiplier;
    }
    return damage;
}

}  // namespace combat
