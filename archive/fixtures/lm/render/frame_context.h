// engine/render/frame_context.h
//
// Per frame presentation state.
//
// The renderer runs free, at whatever rate the machine manages, and rebuilds
// this structure once per rendered frame from the two most recent simulation
// snapshots. Everything here describes the frame being drawn on this machine
// right now: how long the last frame took, where we are between the two
// snapshots being blended, what the local player has hovered.
//
// The simulation snapshots themselves live in sim/snapshot.h.

#pragma once

#include <cstdint>

#include "core/types.h"

namespace render {

struct FrameContext {
    // Wall clock seconds since the previous rendered frame, as measured by
    // the platform timer at the top of the frame.
    float delta_seconds = 0.0f;

    // The same figure with a four frame box filter over it, which is what the
    // camera and the UI animate against so a single long frame does not jolt
    // the picture.
    float smoothed_delta = 0.0f;

    // Where this frame sits between the last two simulation snapshots, in
    // [0, 1). Everything drawn is blended by this.
    float interp_alpha = 0.0f;

    // Seconds since the client process started. Wraps never, resets on
    // reconnect.
    double ui_time_seconds = 0.0;

    // Frames drawn since the client process started.
    uint64_t frame_index = 0;

    // Backbuffer scale and DPI scale, both of which the HUD lays out against.
    float render_scale = 1.0f;
    float hud_scale = 1.0f;

    // Index of the newer of the two snapshots being blended. Presentation
    // only: this is not the tick the simulation is currently stepping.
    core::TickIndex newer_snapshot_tick = 0;
    core::TickIndex older_snapshot_tick = 0;
};

// Filled once per frame by the render thread, before anything draws.
extern FrameContext g_frame;

}  // namespace render
