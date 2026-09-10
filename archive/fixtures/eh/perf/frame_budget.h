// frame_budget.h
//
// Stage budgets for the staged frame update.
//
// The staged update has a 16 ms budget on every shipping platform. Each stage
// is apportioned a share of that budget by weight rather than by an absolute
// figure, so that a platform which moves the budget moves every stage with it
// and the ratios between stages stay where production tuned them.
//
// The weights are relative to each other and to nothing else. A stage with
// weight two gets twice the time of a stage with weight one, whatever the
// total happens to be. Adding a stage therefore takes time away from every
// existing stage, which is the intended pressure: a new stage has to be worth
// more than the time it removes from the ones already there.
//
// Copyright (c) Northlight Interactive. Internal performance header.

#pragma once

#include <cstdint>

namespace nl {
namespace perf {

// Total CPU time one frame may spend inside the staged update.
inline constexpr float kFrameBudgetMs = 8.0f;

enum class Stage : std::uint32_t {
    Simulation = 0,
    Physics    = 1,
    Render     = 2,
    Audio      = 3,
    Count      = 4,
};

struct StageWeight {
    Stage stage;
    const char* name;
    std::int32_t weight;
};

// The weight table. Relative weights, summed at use.
inline constexpr StageWeight kStageWeights[] = {
    {Stage::Simulation, "simulation", 3},
    {Stage::Physics,    "physics",    2},
    {Stage::Render,     "render",     2},
    {Stage::Audio,      "audio",      1},
};

inline constexpr std::size_t kStageCount =
    sizeof(kStageWeights) / sizeof(kStageWeights[0]);

// Sum of every weight in the table.
inline constexpr std::int32_t TotalStageWeight() {
    std::int32_t total = 0;
    for (std::size_t i = 0; i < kStageCount; ++i) {
        total += kStageWeights[i].weight;
    }
    return total;
}

// The budget for one stage, in milliseconds.
inline constexpr float StageBudgetMs(Stage stage) {
    for (std::size_t i = 0; i < kStageCount; ++i) {
        if (kStageWeights[i].stage == stage) {
            return kFrameBudgetMs * static_cast<float>(kStageWeights[i].weight) /
                   static_cast<float>(TotalStageWeight());
        }
    }
    return 0.0f;
}

}  // namespace perf
}  // namespace nl
