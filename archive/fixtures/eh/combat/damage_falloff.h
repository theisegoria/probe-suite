// damage_falloff.h
//
// Damage application for hitscan and projectile weapons.
//
// Copyright (c) Northlight Interactive. Internal gameplay header.

#pragma once

#include <cstdint>

namespace nl {
namespace combat {

struct WeaponProfile {
    const char* name;
    float base_damage;
    float inner_radius;      // metres. Full damage at or inside this.
    float outer_radius;      // metres. Minimum damage at or outside this.
    float inner_scale;       // multiplier at the inner radius
    float outer_scale;       // multiplier at the outer radius
    float armour_pierce;     // raw pierce rating, compared against armour
};

struct TargetState {
    float armour_rating = 0.0f;
    float health = 100.0f;
    bool  is_shielded = false;
};

// Distance falloff multiplier for a hit at the given distance.
float RadialFalloff(const WeaponProfile& w, float distance_m);

// Armour multiplier for a hit against the given target.
float ArmourFactor(const WeaponProfile& w, const TargetState& t);

// The full chain. Returns whole points of damage.
std::int32_t ResolveHit(const WeaponProfile& w, const TargetState& t,
                        float distance_m);

extern const WeaponProfile kWeaponProfiles[];
extern const int kWeaponProfileCount;

const WeaponProfile* FindWeapon(const char* name);

}  // namespace combat
}  // namespace nl
