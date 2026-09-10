// engine/assets/spawn_registry.h
//
// Registry of spawn point assets.
//
// Level packages declare spawn points in their manifest. The loader inserts
// each one into the table below as it streams the package in, and removes it
// again when the package is unloaded. Lookup by id has to be fast because the
// editor hot reload path hits it several thousand times a second.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/types.h"
#include "math/sim_math.h"

namespace assets {

using core::AssetId;
using sim::Vec3;

struct SpawnPointDesc {
    AssetId id = core::kInvalidAsset;
    Vec3 position;
    uint16_t biome = 0;
    uint16_t max_occupants = 1;
    uint16_t weight = 100;
    uint8_t package_index = 0;
    bool indoor = false;
    std::string debug_name;
};

using SpawnPointTable = std::unordered_map<AssetId, SpawnPointDesc>;

class SpawnRegistry {
  public:
    void Insert(const SpawnPointDesc& desc);
    void RemovePackage(uint8_t package_index);

    // Every registered spawn point, keyed by asset id.
    const SpawnPointTable& Entries() const { return table_; }

    // The same ids in manifest order: package by package, and within a
    // package in the order the manifest lists them. The loader appends here
    // as it inserts, and compacts on unload, so this is the same list on
    // every machine that has the same packages resident.
    const std::vector<AssetId>& ManifestIds() const { return manifest_ids_; }

    const SpawnPointDesc* Find(AssetId id) const;

    size_t Count() const { return manifest_ids_.size(); }
    bool Empty() const { return manifest_ids_.empty(); }

  private:
    SpawnPointTable table_;
    std::vector<AssetId> manifest_ids_;
};

extern SpawnRegistry g_spawn_registry;

}  // namespace assets
