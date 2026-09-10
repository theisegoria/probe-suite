// engine/render/view_state.h
//
// The camera and the visibility results for the frame being drawn.
//
// Culling runs on the render thread against the interpolated transforms, one
// frame behind, and its results are cached here for the rest of the frame to
// query. Everything in this structure is per machine: two players watching
// the same match from different angles see different contents here.

#pragma once

#include <cstdint>
#include <vector>

#include "core/types.h"
#include "math/sim_math.h"

namespace render {

using sim::Vec3;

struct DebugLineBuffer {
    void Add(const Vec3& a, const Vec3& b, uint32_t rgba);
    void AddSphere(const Vec3& centre, float radius, uint32_t rgba);
    void Clear();

    std::vector<float> verts;
};

class ViewState {
  public:
    // Camera for the frame being drawn, already interpolated and already
    // shaken by whatever the presentation layer is doing to it.
    Vec3 camera_pos;
    Vec3 camera_forward;
    float fov_y_degrees = 60.0f;
    float aspect = 1.777f;
    float near_plane = 0.1f;
    float far_plane = 4000.0f;

    // Result of last frame's cull for this entity: inside the frustum and not
    // occluded by the depth pyramid.
    bool IsVisible(core::EntityId id) const;

    // Fraction of the screen the entity covers, 0 when off screen.
    float ScreenCoverage(core::EntityId id) const;

    // Level of detail the renderer picked for this entity this frame.
    uint8_t LodOf(core::EntityId id) const;

    // Interpolated position the entity is being drawn at, which sits between
    // the two snapshots and is therefore not any position the simulation ever
    // held.
    Vec3 InterpolatedPos(core::EntityId id) const;

    // How many entities passed the cull this frame.
    uint32_t VisibleCount() const;

  private:
    std::vector<uint64_t> visible_bits_;
    std::vector<uint8_t> lod_;
};

extern ViewState g_view;

// The debug line buffer for this frame. Cleared at the top of every frame,
// drawn at the end of it, and thrown away. Editor and development builds
// only: the shipping build compiles the body away to nothing.
DebugLineBuffer& DebugLines();

}  // namespace render
