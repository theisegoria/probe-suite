// mpmc_queue.h
//
// Bounded multiple producer multiple consumer queue, used for the job system
// steal deques and for the audio command ring.
//
// The queue is a Vyukov bounded ring: every slot carries a sequence number,
// producers claim a slot by advancing the enqueue position, and consumers
// claim one by advancing the dequeue position. Neither position is ever read
// by the other side except through the slot sequence numbers.
//
// False sharing is the whole performance story for this structure. The two
// positions are written on every operation, from different cores, and if they
// land on the same cache line the line ping pongs between those cores and the
// queue runs several times slower than a mutex. They are therefore forced
// apart, and every padded member below is padded for that reason and not for
// alignment of the type itself.
//
// The padding amount is not written down in this header. It comes from the
// platform layer, because the answer differs between the targets we ship on
// and hard coding one target's answer into a shared header is how the last
// engine ended up with a queue that was fast on one platform only.
//
// Copyright (c) Northlight Interactive. Internal memory header.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

// Supplies kCacheLineBytes for the target platform.
#include "platform/cache.h"

namespace nl {
namespace memory {

template <typename T, std::size_t kCapacity>
class MpmcQueue {
    static_assert((kCapacity & (kCapacity - 1)) == 0,
                  "capacity must be a power of two so the mask works");

public:
    MpmcQueue() {
        for (std::size_t i = 0; i < kCapacity; ++i) {
            slots_[i].sequence.store(i, std::memory_order_relaxed);
        }
        enqueue_position_.store(0, std::memory_order_relaxed);
        dequeue_position_.store(0, std::memory_order_relaxed);
    }

    bool TryPush(const T& value) {
        Slot* slot = nullptr;
        std::size_t position = enqueue_position_.load(std::memory_order_relaxed);
        for (;;) {
            slot = &slots_[position & kMask];
            const std::size_t sequence = slot->sequence.load(std::memory_order_acquire);
            const std::intptr_t difference =
                static_cast<std::intptr_t>(sequence) - static_cast<std::intptr_t>(position);
            if (difference == 0) {
                if (enqueue_position_.compare_exchange_weak(
                        position, position + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (difference < 0) {
                return false;  // full
            } else {
                position = enqueue_position_.load(std::memory_order_relaxed);
            }
        }
        slot->value = value;
        slot->sequence.store(position + 1, std::memory_order_release);
        return true;
    }

    bool TryPop(T& out) {
        Slot* slot = nullptr;
        std::size_t position = dequeue_position_.load(std::memory_order_relaxed);
        for (;;) {
            slot = &slots_[position & kMask];
            const std::size_t sequence = slot->sequence.load(std::memory_order_acquire);
            const std::intptr_t difference =
                static_cast<std::intptr_t>(sequence) -
                static_cast<std::intptr_t>(position + 1);
            if (difference == 0) {
                if (dequeue_position_.compare_exchange_weak(
                        position, position + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (difference < 0) {
                return false;  // empty
            } else {
                position = dequeue_position_.load(std::memory_order_relaxed);
            }
        }
        out = slot->value;
        slot->sequence.store(position + kCapacity, std::memory_order_release);
        return true;
    }

    // Approximate. Both positions are read without synchronisation, so the
    // answer is only ever a hint for the profiler overlay.
    std::size_t ApproximateSize() const {
        const std::size_t head = enqueue_position_.load(std::memory_order_relaxed);
        const std::size_t tail = dequeue_position_.load(std::memory_order_relaxed);
        return head - tail;
    }

private:
    static constexpr std::size_t kMask = kCapacity - 1;

    struct Slot {
        std::atomic<std::size_t> sequence;
        T value;
    };

    // Each of the three hot members below sits on its own line. The producer
    // side writes the first, the consumer side writes the second, and the
    // slots are read by both.
    alignas(platform::kCacheLineBytes) std::atomic<std::size_t> enqueue_position_;
    alignas(platform::kCacheLineBytes) std::atomic<std::size_t> dequeue_position_;
    alignas(platform::kCacheLineBytes) Slot slots_[kCapacity];
};

}  // namespace memory
}  // namespace nl
