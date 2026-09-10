// combat/resolve_hit.h
//
// The current damage resolution entry point. One definition of a critical hit
// for every damage source in the game.

#ifndef COMBAT_RESOLVE_HIT_H
#define COMBAT_RESOLVE_HIT_H

#include "combat/damage_types.h"

namespace combat {

struct HitInput {
    float base_damage = 0.0f;      // range falloff already applied
    float armour_fraction = 0.0f;  // 0 means no armour, 1 means immune
    bool is_critical = false;
    DamageType damage_type = DamageType::kBallistic;
    int instigator_team = 0;
    int victim_team = 0;
};

// Applies the critical multiplier, then armour, then the team scale.
float ResolveHit(const HitInput& in);

// Shields take the critical multiplier but ignore armour.
float ResolveShieldHit(const HitInput& in);

}  // namespace combat

#endif  // COMBAT_RESOLVE_HIT_H
