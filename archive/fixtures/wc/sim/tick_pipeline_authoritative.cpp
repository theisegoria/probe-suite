// sim/tick_pipeline_authoritative.cpp
//
// The authoritative tick.
//
// This order is the simulation contract. Changing it changes the meaning of
// every recorded replay and desyncs any peer that has not taken the change, so
// it is versioned with the protocol and reviewed as a protocol change.
//
// The two ordering decisions that are not obvious:
//
// Projectiles integrate before abilities, not after. A projectile that resolves
// a hit this tick must have done so before the ability system reads the damage
// queue, otherwise an ability that reacts to a hit lands a tick late and the
// reaction reads as unresponsive.
//
// Abilities run immediately before the physics step so that an impulse applied
// by an ability is consumed by the solver in the same tick it was applied. The
// alternative, applying it after the step, leaves the impulse sitting in the
// body for a tick and makes light knockback feel mushy.

#include "sim/tick_pipeline.h"

#include "sim/command_buffer.h"
#include "sim/world.h"

namespace sim {
namespace {

void RunSystem(World& world, const CommandBuffer& commands, SystemId id) {
    OnSystemBegin(id);
    switch (id) {
        case SystemId::kInputApply:
            InputApply(world, commands);
            break;
        case SystemId::kProjectileIntegrate:
            ProjectileIntegrate(world);
            break;
        case SystemId::kAbilityTick:
            AbilityTick(world);
            break;
        case SystemId::kPhysicsStep:
            PhysicsStep(world);
            break;
        case SystemId::kContactEvents:
            ContactEvents(world);
            break;
        case SystemId::kDamageResolve:
            DamageResolve(world);
            break;
        case SystemId::kDeathReap:
            DeathReap(world);
            break;
        case SystemId::kTransformSync:
            TransformSync(world);
            break;
        default:
            break;
    }
    OnSystemEnd(id);
}

}  // namespace

void RunAuthoritativeTick(World& world, const CommandBuffer& commands) {
    RunSystem(world, commands, SystemId::kInputApply);
    RunSystem(world, commands, SystemId::kProjectileIntegrate);
    RunSystem(world, commands, SystemId::kAbilityTick);
    RunSystem(world, commands, SystemId::kPhysicsStep);
    RunSystem(world, commands, SystemId::kContactEvents);
    RunSystem(world, commands, SystemId::kDamageResolve);
    RunSystem(world, commands, SystemId::kDeathReap);
    RunSystem(world, commands, SystemId::kTransformSync);
}

}  // namespace sim
