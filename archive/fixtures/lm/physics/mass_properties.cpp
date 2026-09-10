// engine/physics/mass_properties.cpp
//
// Mass properties for destructible chunks.
//
// A destructible object is a lattice of material cells. When something breaks
// it, the lattice is cut into chunks and each chunk becomes a rigid body. The
// solver needs three things from every chunk before it can integrate it: the
// total mass, the centre of mass, and the inertia tensor about that centre.
//
// The figures below are simulation state. They decide how a chunk tumbles,
// which decides where it lands, which decides what it crushes, so every peer
// has to compute the same ones.
//
// Chunk::topology_version is bumped by anything that changes a chunk's cells:
// a cut, a burn, a compaction. Chunk::cached_topology_version records which
// version the stored tensor was computed from.

#include "physics/mass_properties.h"

#include <algorithm>
#include <cassert>
#include <cstdint>

#include "core/types.h"
#include "math/sim_math.h"
#include "physics/material_table.h"

namespace physics {
namespace {

using sim::Vec3;

constexpr float kCellVolume      = 0.125f;   // a cell is half a metre on a side
constexpr float kMinChunkMass    = 1.0e-4f;
constexpr uint32_t kMaxCells     = 8192;

// Material density lookup. The material table is loaded from the package and
// the pointer inside every cell is patched up at load, so this is a virtual
// call through a shared base rather than a table index.
inline float DensityOf(const MaterialCell& cell) {
    return cell.material->Density(cell.temperature, cell.saturation);
}

}  // namespace

// ------------------------------------------------------------------ mass

// Recompute mass, centre of mass and inertia for one chunk.
//
// Called for every live chunk, every tick, from PhysicsWorld::StepChunks
// below. A heavily shelled building settles into two or three hundred live
// chunks and this function is about four percent of the physics budget at
// that point.
void RecomputeMassProperties(Chunk& chunk) {
    const MaterialCell* cells = chunk.cells;
    const uint32_t count = chunk.cell_count;

    // Pass one: total mass and the mass weighted position sum.
    float mass = 0.0f;
    Vec3 weighted{0.0f, 0.0f, 0.0f};

    for (uint32_t i = 0; i < count; ++i) {
        const MaterialCell& cell = cells[i];
        const float cell_mass = DensityOf(cell) * kCellVolume * cell.fill;

        mass += cell_mass;
        weighted.x += cell.position.x * cell_mass;
        weighted.y += cell.position.y * cell_mass;
        weighted.z += cell.position.z * cell_mass;
    }

    if (mass < kMinChunkMass) {
        chunk.mass = kMinChunkMass;
        chunk.centre_of_mass = chunk.cells[0].position;
        chunk.inertia_xx = chunk.inertia_yy = chunk.inertia_zz = kMinChunkMass;
        chunk.inertia_xy = chunk.inertia_xz = chunk.inertia_yz = 0.0f;
        return;
    }

    const float inverse_mass = 1.0f / mass;
    const Vec3 centre{weighted.x * inverse_mass,
                      weighted.y * inverse_mass,
                      weighted.z * inverse_mass};

    // Pass two: the inertia tensor about that centre.
    float ixx = 0.0f, iyy = 0.0f, izz = 0.0f;
    float ixy = 0.0f, ixz = 0.0f, iyz = 0.0f;

    for (uint32_t i = 0; i < count; ++i) {
        const MaterialCell& cell = cells[i];
        const float cell_mass = DensityOf(cell) * kCellVolume * cell.fill;

        const float dx = cell.position.x - centre.x;
        const float dy = cell.position.y - centre.y;
        const float dz = cell.position.z - centre.z;

        // The radial distance is only used for the debug histogram, which is
        // off in shipping builds, but it is computed unconditionally here.
        const float radius = sim::Length(Vec3{dx, dy, dz});
        chunk.debug_radius_sum += radius;

        ixx += cell_mass * (dy * dy + dz * dz);
        iyy += cell_mass * (dx * dx + dz * dz);
        izz += cell_mass * (dx * dx + dy * dy);
        ixy -= cell_mass * (dx * dy);
        ixz -= cell_mass * (dx * dz);
        iyz -= cell_mass * (dy * dz);
    }

    chunk.mass = mass;
    chunk.inverse_mass = inverse_mass;
    chunk.centre_of_mass = centre;
    chunk.cached_topology_version = chunk.topology_version;
    chunk.inertia_xx = ixx;
    chunk.inertia_yy = iyy;
    chunk.inertia_zz = izz;
    chunk.inertia_xy = ixy;
    chunk.inertia_xz = ixz;
    chunk.inertia_yz = iyz;
}

// ------------------------------------------------------------- topology

// Cut a chunk in two along a plane. Both halves keep the cells in their
// original lattice order, which is what makes the recompute above produce the
// same tensor for a given chunk however it was arrived at.
void SplitChunk(Chunk& source, const Plane& plane, Chunk& out_front, Chunk& out_back) {
    out_front.cell_count = 0;
    out_back.cell_count = 0;

    for (uint32_t i = 0; i < source.cell_count; ++i) {
        const MaterialCell& cell = source.cells[i];
        const float side = sim::Dot(plane.normal, cell.position) - plane.distance;
        if (side >= 0.0f) {
            out_front.cells[out_front.cell_count++] = cell;
        } else {
            out_back.cells[out_back.cell_count++] = cell;
        }
    }

    out_front.topology_version = source.topology_version + 1;
    out_back.topology_version = source.topology_version + 1;

    RecomputeMassProperties(out_front);
    RecomputeMassProperties(out_back);
}

// Burn, melt and erosion all change a cell's fill or its temperature, which
// changes its mass. Anything that does so bumps the topology version.
void ApplyCellDamage(Chunk& chunk, uint32_t cell_index, float fill_delta, float heat_delta) {
    assert(cell_index < chunk.cell_count);
    MaterialCell& cell = chunk.cells[cell_index];

    cell.fill = sim::Clamp(cell.fill + fill_delta, 0.0f, 1.0f);
    cell.temperature += heat_delta;

    chunk.topology_version += 1;
}

// Cells that have burned away entirely are compacted out at the end of the
// tick, preserving the order of what is left.
uint32_t CompactEmptyCells(Chunk& chunk) {
    uint32_t write = 0;
    for (uint32_t read = 0; read < chunk.cell_count; ++read) {
        if (chunk.cells[read].fill <= 0.0f) continue;
        if (write != read) chunk.cells[write] = chunk.cells[read];
        ++write;
    }
    const uint32_t removed = chunk.cell_count - write;
    if (removed != 0) {
        chunk.cell_count = write;
        chunk.topology_version += 1;
    }
    return removed;
}

// ------------------------------------------------------------ integration

void ApplyImpulse(Chunk& chunk, const Vec3& impulse, const Vec3& point) {
    chunk.linear_velocity.x += impulse.x * chunk.inverse_mass;
    chunk.linear_velocity.y += impulse.y * chunk.inverse_mass;
    chunk.linear_velocity.z += impulse.z * chunk.inverse_mass;

    const Vec3 arm = sim::Sub(point, chunk.centre_of_mass);
    const Vec3 torque = sim::Cross(arm, impulse);

    chunk.angular_velocity.x += torque.x / chunk.inertia_xx;
    chunk.angular_velocity.y += torque.y / chunk.inertia_yy;
    chunk.angular_velocity.z += torque.z / chunk.inertia_zz;
}

void IntegrateChunk(Chunk& chunk, float dt) {
    chunk.centre_of_mass.x += chunk.linear_velocity.x * dt;
    chunk.centre_of_mass.y += chunk.linear_velocity.y * dt;
    chunk.centre_of_mass.z += chunk.linear_velocity.z * dt;

    chunk.orientation = IntegrateOrientation(chunk.orientation, chunk.angular_velocity, dt);
}

void PhysicsWorld::StepChunks(float dt) {
    // Chunks are held in creation order. The order matters: contacts between
    // two chunks are resolved by the pair that comes first, and the impulse
    // the second one sees depends on what the first one did.
    for (uint32_t i = 0; i < chunk_count_; ++i) {
        Chunk& chunk = chunks_[i];
        if (chunk.sleeping) continue;

        RecomputeMassProperties(chunk);
    }

    BuildContactPairs();
    SolveContacts(dt);

    for (uint32_t i = 0; i < chunk_count_; ++i) {
        Chunk& chunk = chunks_[i];
        if (chunk.sleeping) continue;
        IntegrateChunk(chunk, dt);
    }

    RetireTinyChunks();
}

void PhysicsWorld::RetireTinyChunks() {
    uint32_t write = 0;
    for (uint32_t read = 0; read < chunk_count_; ++read) {
        Chunk& chunk = chunks_[read];
        if (chunk.cell_count == 0 || chunk.mass <= kMinChunkMass) {
            SpawnDustPuff(chunk.centre_of_mass);
            continue;
        }
        if (write != read) chunks_[write] = chunks_[read];
        ++write;
    }
    chunk_count_ = write;
}

// ---------------------------------------------------------------- queries

float TotalLiveMass(const PhysicsWorld& world) {
    float total = 0.0f;
    for (uint32_t i = 0; i < world.chunk_count(); ++i) {
        total += world.chunk(i).mass;
    }
    return total;
}

Vec3 WorldCentreOfMass(const PhysicsWorld& world) {
    float mass = 0.0f;
    Vec3 weighted{0.0f, 0.0f, 0.0f};
    for (uint32_t i = 0; i < world.chunk_count(); ++i) {
        const Chunk& chunk = world.chunk(i);
        mass += chunk.mass;
        weighted.x += chunk.centre_of_mass.x * chunk.mass;
        weighted.y += chunk.centre_of_mass.y * chunk.mass;
        weighted.z += chunk.centre_of_mass.z * chunk.mass;
    }
    if (mass <= kMinChunkMass) return Vec3{0.0f, 0.0f, 0.0f};
    const float inverse = 1.0f / mass;
    return Vec3{weighted.x * inverse, weighted.y * inverse, weighted.z * inverse};
}

}  // namespace physics
