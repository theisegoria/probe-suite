// handle_store.h
//
// The slot table behind the handle system.

#pragma once

#include <cstdint>

#include "eh/entity/handle.h"

namespace nl {
namespace entity {

struct Slot;
struct SlotChunk;
class LogSink;

class Arena {
public:
    template <typename T>
    T* Allocate();
};

// Chunks the store may allocate. Sized so that the chunk pointer table covers
// every slot index a handle can name, which is what makes an out of range
// index impossible rather than merely unlikely.
extern const std::uint32_t kMaxChunks;

class HandleStore {
public:
    Handle Create(void* payload);
    void*  Resolve(Handle handle);
    bool   Destroy(Handle handle);

    std::uint32_t LiveCount() const { return live_count_; }
    std::uint32_t ChunksAllocated() const { return chunks_allocated_; }

    void ReportOccupancy(LogSink& log) const;

private:
    bool  AllocateChunk();
    Slot* SlotAt(std::uint32_t slot_index);

    Arena arena_;
    SlotChunk** chunks_ = nullptr;
    std::uint32_t chunks_allocated_ = 0;
    std::uint32_t live_count_ = 0;
    std::uint32_t free_list_head_ = 0xFFFFFFFFu;
};

}  // namespace entity
}  // namespace nl
