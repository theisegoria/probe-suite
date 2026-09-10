// engine/core/cpu_features.h
//
// What the processor we are running on can do. Filled once at boot by
// DetectCpuFeatures, before any subsystem starts, and read only afterwards.
//
// The shipping build is a single binary per platform: we do not build a
// separate executable per instruction set, so anything that wants a wider
// code path has to branch on these flags at runtime.

#pragma once

#include <cstdint>

namespace core {

struct CpuFeatures {
    bool has_sse42 = false;
    bool has_avx = false;
    bool has_avx2 = false;
    bool has_fma = false;
    bool has_f16c = false;
    bool has_neon = false;      // set on the arm64 targets
    bool has_sve = false;

    uint32_t physical_cores = 1;
    uint32_t logical_cores = 1;
    uint32_t cache_line_bytes = 64;
    uint32_t l2_bytes = 0;

    char vendor[16] = {0};
    char brand[64] = {0};
};

extern CpuFeatures g_cpu;

void DetectCpuFeatures();

// Human readable summary for the crash reporter footer.
const char* CpuSummary();

}  // namespace core
