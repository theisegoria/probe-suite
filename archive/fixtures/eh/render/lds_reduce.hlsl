// lds_reduce.hlsl
//
// Tiled luminance reduction for the automatic exposure pass.
//
// Each thread group reads one tile of the half resolution scene colour,
// reduces it to a log luminance sum and a maximum, and writes one entry into
// the tile buffer. A second pass reduces the tile buffer to a single value.
//
// The reduction is a tree in groupshared memory. Each thread seeds one entry,
// then half the threads fold the upper half into the lower half, and so on
// until thread zero holds the result. The barrier between rounds is a group
// barrier rather than a wave barrier, because the group is larger than a wave
// on every target we ship and wave intrinsics alone would not synchronise it.
//
// Copyright (c) Northlight Interactive.

// Threads per group along each axis. The group is square.
#define TILE_DIM 16
#define THREADS_PER_GROUP (TILE_DIM * TILE_DIM)

Texture2D<float3>   g_SceneColour   : register(t0);
RWStructuredBuffer<float2> g_TileResults : register(u0);

cbuffer ExposureConstants : register(b0)
{
    uint2 g_SceneSize;
    float g_MinLogLuminance;
    float g_LogLuminanceRange;
};

struct TileAccumulator
{
    float log_sum;
    float peak;
    float weight;
    float padding;
};

// One accumulator per thread. The tree reduction folds this array in place.
groupshared TileAccumulator g_Tile[THREADS_PER_GROUP];

float LogLuminance(float3 colour)
{
    const float luminance = dot(colour, float3(0.2126, 0.7152, 0.0722));
    return log2(max(luminance, 1e-5));
}

[numthreads(TILE_DIM, TILE_DIM, 1)]
void CS_ReduceTile(uint3 group_id : SV_GroupID,
                   uint3 thread_id : SV_GroupThreadID,
                   uint  flat_index : SV_GroupIndex)
{
    const uint2 pixel = group_id.xy * TILE_DIM + thread_id.xy;

    TileAccumulator seed;
    seed.log_sum = 0.0;
    seed.peak = 0.0;
    seed.weight = 0.0;
    seed.padding = 0.0;

    if (pixel.x < g_SceneSize.x && pixel.y < g_SceneSize.y)
    {
        const float log_luminance = LogLuminance(g_SceneColour[pixel]);
        seed.log_sum = log_luminance;
        seed.peak = log_luminance;
        seed.weight = 1.0;
    }

    g_Tile[flat_index] = seed;
    GroupMemoryBarrierWithGroupSync();

    // Fold. Each round halves the number of live entries.
    for (uint stride = THREADS_PER_GROUP / 2; stride > 0; stride >>= 1)
    {
        if (flat_index < stride)
        {
            TileAccumulator lhs = g_Tile[flat_index];
            TileAccumulator rhs = g_Tile[flat_index + stride];
            lhs.log_sum += rhs.log_sum;
            lhs.peak = max(lhs.peak, rhs.peak);
            lhs.weight += rhs.weight;
            g_Tile[flat_index] = lhs;
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (flat_index == 0)
    {
        const TileAccumulator total = g_Tile[0];
        const float mean = total.weight > 0.0 ? (total.log_sum / total.weight)
                                              : g_MinLogLuminance;
        const uint tile_index = group_id.y * ((g_SceneSize.x + TILE_DIM - 1) / TILE_DIM)
                              + group_id.x;
        g_TileResults[tile_index] = float2(mean, total.peak);
    }
}
