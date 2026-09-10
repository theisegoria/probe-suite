// worker_pool.h
//
// The job system worker pool.
//
// Copyright (c) Northlight Interactive. Internal engine header.

#pragma once

#include <atomic>
#include <cstdint>

namespace nl {
namespace jobs {

using JobFn = void (*)(void* user_data, std::uint32_t worker_index);

struct Job {
    JobFn fn = nullptr;
    void* user_data = nullptr;
    std::atomic<std::int32_t>* counter = nullptr;
};

class WorkerPool {
public:
    // Brings the pool up. The worker count is decided by WorkerThreadCount()
    // in worker_pool.cpp and is not configurable at runtime, because a peer
    // that splits the simulation across a different number of workers can
    // reach a different result when a job reduces in worker order.
    bool Start();
    void Stop();

    std::uint32_t WorkerCount() const { return worker_count_; }

    void Submit(const Job& job);
    void WaitFor(std::atomic<std::int32_t>& counter);

    // Index of the calling thread inside the pool. The main thread
    // participates in the pool while it waits, and holds the last index.
    static std::uint32_t CurrentWorkerIndex();

private:
    std::uint32_t worker_count_ = 0;
    bool running_ = false;
};

WorkerPool& Pool();

}  // namespace jobs
}  // namespace nl
