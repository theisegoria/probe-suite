// entity_store.h
//
// Chunked structure of arrays entity store.
//
// Entities live in fixed size chunks. Inside a chunk every component is a
// contiguous array, so a system that touches one component walks memory in a
// straight line and never pays for the components it does not read.
//
// The layout of a chunk is computed at compile time from the component table
// below. It is not hand written, because it changed four times during
// production and every hand written version was wrong at least once.
//
// Chunk layout:
//
//   [ header ][ component array 0 ][ pad ][ component array 1 ][ pad ] ...
//
// Each component array starts on a cache line boundary so that two systems
// running on two cores and writing two different components never write to
// the same line. The padding between arrays is a consequence of that rule and
// not a tunable: an array whose length is not a whole number of cache lines
// leaves a gap behind it.
//
// Copyright (c) Northlight Interactive. Internal simulation header.

#pragma once

#include <cstddef>
#include <cstdint>

namespace nl {
namespace sim {

// Entities per chunk. Chosen so that the hot components of one chunk fit in
// the level one data cache of one core with room left for the code.
inline constexpr std::uint32_t kEntitiesPerChunk = 200;

// Every component array starts on a boundary of this many bytes.
inline constexpr std::size_t kComponentArrayAlignment = 64;

// Bytes reserved at the front of every chunk for the chunk header: the
// archetype id, the live count, the version stamp and the free list head.
inline constexpr std::size_t kChunkHeaderBytes = 64;

// Hard ceiling on the size of one chunk. The allocator hands out chunks from
// a slab of this granularity, so a layout that exceeds it fails to build.
inline constexpr std::size_t kMaxChunkBytes = 16384;

enum class ComponentId : std::uint32_t {
    Position = 0,
    Velocity = 1,
    Health   = 2,
    Team     = 3,
    Flags    = 4,
    Count    = 5,
};

struct ComponentDesc {
    const char* name;
    std::size_t stride;   // bytes for one entity
};

// The component table. Order is the layout order inside a chunk: component
// zero comes first, then component one, and so on. Reordering this table
// relocates every array, so it is append only in shipped branches.
inline constexpr ComponentDesc kComponents[] = {
    {"position", 12},   // float3, metres
    {"velocity", 12},   // float3, metres per second
    {"health",    4},   // float
    {"team",      1},   // uint8
    {"flags",     4},   // uint32 bitfield
};

inline constexpr std::size_t kComponentCount =
    sizeof(kComponents) / sizeof(kComponents[0]);

// Round v up to the next multiple of a. a is a power of two in every call
// site, but the general form is used so the assert below can be written
// without a second code path.
inline constexpr std::size_t AlignUp(std::size_t v, std::size_t a) {
    return ((v + a - 1) / a) * a;
}

// Byte offset of the start of one component array, measured from the start of
// the chunk. Walks the table from the front, aligning each array start and
// then stepping over the whole array.
inline constexpr std::size_t ComponentArrayOffset(ComponentId id) {
    std::size_t offset = kChunkHeaderBytes;
    const std::size_t target = static_cast<std::size_t>(id);
    for (std::size_t i = 0; i < kComponentCount; ++i) {
        offset = AlignUp(offset, kComponentArrayAlignment);
        if (i == target) {
            return offset;
        }
        offset += kComponents[i].stride * kEntitiesPerChunk;
    }
    return offset;
}

// Total bytes in one chunk, including the trailing pad that keeps the next
// chunk in the slab aligned.
inline constexpr std::size_t ChunkBytes() {
    std::size_t offset = kChunkHeaderBytes;
    for (std::size_t i = 0; i < kComponentCount; ++i) {
        offset = AlignUp(offset, kComponentArrayAlignment);
        offset += kComponents[i].stride * kEntitiesPerChunk;
    }
    return AlignUp(offset, kComponentArrayAlignment);
}

static_assert(ChunkBytes() <= kMaxChunkBytes, "chunk layout no longer fits a slab");

// Which chunk an entity index lives in, and which slot inside that chunk.
inline constexpr std::uint32_t ChunkIndexOf(std::uint32_t entity_index) {
    return entity_index / kEntitiesPerChunk;
}

inline constexpr std::uint32_t SlotIndexOf(std::uint32_t entity_index) {
    return entity_index % kEntitiesPerChunk;
}

// Byte offset of one entity component inside its chunk.
inline constexpr std::size_t ComponentByteOffset(ComponentId id,
                                                 std::uint32_t entity_index) {
    const std::size_t base = ComponentArrayOffset(id);
    const std::size_t stride = kComponents[static_cast<std::size_t>(id)].stride;
    return base + stride * SlotIndexOf(entity_index);
}

class EntityStore {
public:
    void* ChunkBase(std::uint32_t chunk_index);
    const void* ChunkBase(std::uint32_t chunk_index) const;

    void* ComponentPointer(ComponentId id, std::uint32_t entity_index);

    std::uint32_t ChunkCount() const { return chunk_count_; }

private:
    std::uint8_t* slab_ = nullptr;
    std::uint32_t chunk_count_ = 0;
    std::uint32_t live_count_ = 0;
};

}  // namespace sim
}  // namespace nl
