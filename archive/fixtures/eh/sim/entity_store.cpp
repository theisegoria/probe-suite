// entity_store.cpp
//
// Chunked entity store, implementation.
//
// Copyright (c) Northlight Interactive. Internal simulation source.

#include "eh/sim/entity_store.h"

#include <cassert>
#include <cstring>

namespace nl {
namespace sim {

void* EntityStore::ChunkBase(std::uint32_t chunk_index) {
    assert(chunk_index < chunk_count_);
    return slab_ + static_cast<std::size_t>(chunk_index) * ChunkBytes();
}

const void* EntityStore::ChunkBase(std::uint32_t chunk_index) const {
    assert(chunk_index < chunk_count_);
    return slab_ + static_cast<std::size_t>(chunk_index) * ChunkBytes();
}

// Pointer to one component of one entity. Two lookups: the chunk, then the
// offset inside it.
void* EntityStore::ComponentPointer(ComponentId id, std::uint32_t entity_index) {
    std::uint8_t* chunk = static_cast<std::uint8_t*>(ChunkBase(ChunkIndexOf(entity_index)));
    return chunk + ComponentByteOffset(id, entity_index);
}

// ------------------------------------------------------------------ systems
//
// Systems take a chunk at a time rather than an entity at a time. The array
// bases are resolved once per chunk and the inner loop is a straight walk.

void IntegrateVelocities(EntityStore& store, float dt) {
    for (std::uint32_t c = 0; c < store.ChunkCount(); ++c) {
        std::uint8_t* chunk = static_cast<std::uint8_t*>(store.ChunkBase(c));
        float* positions = reinterpret_cast<float*>(
            chunk + ComponentArrayOffset(ComponentId::Position));
        const float* velocities = reinterpret_cast<const float*>(
            chunk + ComponentArrayOffset(ComponentId::Velocity));

        for (std::uint32_t i = 0; i < kEntitiesPerChunk; ++i) {
            positions[i * 3 + 0] += velocities[i * 3 + 0] * dt;
            positions[i * 3 + 1] += velocities[i * 3 + 1] * dt;
            positions[i * 3 + 2] += velocities[i * 3 + 2] * dt;
        }
    }
}

void ApplyDamageQueue(EntityStore& store, const std::uint32_t* indices,
                      const float* amounts, std::uint32_t count) {
    for (std::uint32_t n = 0; n < count; ++n) {
        float* health = static_cast<float*>(
            store.ComponentPointer(ComponentId::Health, indices[n]));
        *health -= amounts[n];
    }
}

// Bulk clear used when a chunk is recycled. Clears every array in one pass
// rather than component by component, because the padding between arrays is
// part of the chunk and leaving stale bytes there upsets the memory debugger.
void ClearChunk(EntityStore& store, std::uint32_t chunk_index) {
    std::uint8_t* chunk = static_cast<std::uint8_t*>(store.ChunkBase(chunk_index));
    std::memset(chunk + kChunkHeaderBytes, 0, ChunkBytes() - kChunkHeaderBytes);
}

// ---------------------------------------------------------------- debugging
//
// The entity inspector in the editor asks for the address of one field of one
// component so it can show the raw bytes next to the decoded value. Vector
// components are laid out x then y then z, four bytes each, inside the twelve
// byte stride.

std::size_t VectorFieldByteOffset(ComponentId id, std::uint32_t entity_index,
                                  std::uint32_t component_axis) {
    assert(component_axis < 3);
    return ComponentByteOffset(id, entity_index) +
           static_cast<std::size_t>(component_axis) * sizeof(float);
}

// Reports which chunk and slot an entity index maps to, for the inspector
// title bar.
void DescribeEntitySlot(std::uint32_t entity_index, std::uint32_t& out_chunk,
                        std::uint32_t& out_slot) {
    out_chunk = ChunkIndexOf(entity_index);
    out_slot = SlotIndexOf(entity_index);
}

}  // namespace sim
}  // namespace nl
