// handle_store.cpp
//
// The slot table behind the handle system.
//
// Slots are allocated in chunks rather than as one flat array. A match that
// spawns a few hundred entities pays for a few hundred slots, and a match
// that fills the index space pays for all of them, but neither pays up front.
//
// Chunks are never freed once allocated. A slot that has been used once is
// very likely to be used again, and returning a chunk to the allocator only
// to take it back a second later is a poor trade against the fragmentation it
// causes in the arena the chunks come from.
//
// The free list threads through the slot table itself: a free slot stores
// the index of the next free slot, so finding a slot to hand out is a read of
// the list head and never a search.
//
// Copyright (c) Northlight Interactive. Internal entity source.

#include "eh/entity/handle_store.h"

#include <cassert>
#include <cstdint>
#include <cstring>

#include "eh/entity/handle.h"

namespace nl {
namespace entity {

// Slots per chunk. One chunk is the allocation granularity of the slot table.
inline constexpr std::uint32_t kSlotsPerChunk = 512;

// Sentinel stored in the next field of the last free slot in the list.
inline constexpr std::uint32_t kFreeListEnd = 0xFFFFFFFFu;

struct Slot {
    void*         payload = nullptr;
    std::uint32_t generation = 0;
    std::uint32_t next_free = kFreeListEnd;
    bool          live = false;
};

struct SlotChunk {
    Slot slots[kSlotsPerChunk];
};

// Which chunk a slot index falls in, and where inside that chunk.
inline constexpr std::uint32_t ChunkOfSlot(std::uint32_t slot_index) {
    return slot_index / kSlotsPerChunk;
}

inline constexpr std::uint32_t OffsetInChunk(std::uint32_t slot_index) {
    return slot_index % kSlotsPerChunk;
}

Slot* HandleStore::SlotAt(std::uint32_t slot_index) {
    const std::uint32_t chunk = ChunkOfSlot(slot_index);
    if (chunk >= chunks_allocated_) {
        return nullptr;
    }
    return &chunks_[chunk]->slots[OffsetInChunk(slot_index)];
}

// Grows the table by one chunk and threads the new slots onto the free list.
bool HandleStore::AllocateChunk() {
    if (chunks_allocated_ >= kMaxChunks) {
        // The index space is full. Every slot that will ever exist exists.
        return false;
    }

    SlotChunk* chunk = arena_.Allocate<SlotChunk>();
    if (chunk == nullptr) {
        return false;
    }

    const std::uint32_t base = chunks_allocated_ * kSlotsPerChunk;
    for (std::uint32_t i = 0; i < kSlotsPerChunk; ++i) {
        Slot& slot = chunk->slots[i];
        slot.payload = nullptr;
        slot.generation = 1;   // generation zero is reserved for the null handle
        slot.live = false;
        slot.next_free = (i + 1 < kSlotsPerChunk) ? (base + i + 1) : free_list_head_;
    }

    chunks_[chunks_allocated_] = chunk;
    free_list_head_ = base;
    ++chunks_allocated_;
    return true;
}

Handle HandleStore::Create(void* payload) {
    if (free_list_head_ == kFreeListEnd && !AllocateChunk()) {
        return Handle{kNullHandle};
    }

    const std::uint32_t slot_index = free_list_head_;
    Slot* slot = SlotAt(slot_index);
    assert(slot != nullptr && !slot->live);

    free_list_head_ = slot->next_free;
    slot->payload = payload;
    slot->live = true;
    slot->next_free = kFreeListEnd;
    ++live_count_;

    return MakeHandle(slot_index, slot->generation);
}

void* HandleStore::Resolve(Handle handle) {
    if (handle.IsNull()) {
        return nullptr;
    }
    Slot* slot = SlotAt(handle.Index());
    if (slot == nullptr || !slot->live) {
        return nullptr;
    }
    if (slot->generation != handle.Generation()) {
        // The slot was recycled since this handle was made.
        return nullptr;
    }
    return slot->payload;
}

bool HandleStore::Destroy(Handle handle) {
    Slot* slot = SlotAt(handle.Index());
    if (slot == nullptr || !slot->live || slot->generation != handle.Generation()) {
        return false;
    }

    slot->live = false;
    slot->payload = nullptr;

    // Bump the generation so every outstanding handle to this slot stops
    // resolving. Wrapping past the field width lands back on zero, which the
    // null handle owns, so the counter skips it.
    slot->generation = (slot->generation + 1) & kGenerationMask;
    if (slot->generation == 0) {
        slot->generation = 1;
    }

    slot->next_free = free_list_head_;
    free_list_head_ = handle.Index();
    --live_count_;
    return true;
}

// ---------------------------------------------------------------- reporting

void HandleStore::ReportOccupancy(LogSink& log) const {
    log.Info("handle store: %u live, %u chunks allocated, %u slots per chunk",
             live_count_, chunks_allocated_, kSlotsPerChunk);
}

}  // namespace entity
}  // namespace nl
