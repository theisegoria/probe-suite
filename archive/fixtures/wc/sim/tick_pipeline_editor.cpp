// sim/tick_pipeline_editor.cpp
//
// The editor preview tick.
//
// The editor drives a live world in the viewport so that designers can watch a
// change take effect without cooking and launching. It is not determinism
// critical: it runs on one machine, it is never recorded, and it never talks
// to a peer. That freedom is used deliberately here. Presentation systems that
// the shipping pipeline runs elsewhere are folded into the tick so that the
// viewport shows animation and effects while paused and stepping.
//
// Do not copy this order into the shipping pipeline.

#include "sim/tick_pipeline.h"

#include "sim/command_buffer.h"
#include "sim/world.h"

namespace sim {
namespace {

// The editor steps one tick per viewport frame while playing, and one tick per
// press while stepping. Either way it is a single tick through this order.
void RunSystem(World& world, const CommandBuffer& commands, SystemId id) {
    OnSystemBegin(id);
    switch (id) {
        case SystemId::kSampleEditorInput:
            SampleEditorInput(world, commands);
            break;
        case SystemId::kAbilityTick:
            AbilityTick(world);
            break;
        case SystemId::kProjectileIntegrate:
            ProjectileIntegrate(world);
            break;
        case SystemId::kPhysicsStep:
            PhysicsStep(world);
            break;
        case SystemId::kAnimationDrive:
            AnimationDrive(world);
            break;
        case SystemId::kVfxSpawn:
            VfxSpawn(world);
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

void RunEditorPreviewTick(World& world, const CommandBuffer& commands) {
    // Editor input is sampled inside the tick rather than ahead of it, because
    // the viewport gizmos can move an entity between two steps and the preview
    // should pick that up immediately.
    RunSystem(world, commands, SystemId::kSampleEditorInput);

    // Abilities before projectiles, so that a projectile spawned by an ability
    // this tick is integrated in the same tick and the designer sees it leave
    // the muzzle rather than appearing a frame later.
    RunSystem(world, commands, SystemId::kAbilityTick);
    RunSystem(world, commands, SystemId::kProjectileIntegrate);

    RunSystem(world, commands, SystemId::kPhysicsStep);

    // Presentation. Folded in so the viewport is correct while stepping.
    RunSystem(world, commands, SystemId::kAnimationDrive);
    RunSystem(world, commands, SystemId::kVfxSpawn);

    RunSystem(world, commands, SystemId::kTransformSync);

    world.editor_preview_ticks += 1;
}

}  // namespace sim
