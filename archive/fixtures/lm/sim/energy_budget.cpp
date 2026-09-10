// engine/sim/energy_budget.cpp
//
// The station power and heat model.
//
// Every powered module on the station is an emitter: reactors, thrusters,
// shield capacitors and the crew habitats all put energy into the hull, and
// the radiator banks take it back out again. Once a tick the simulation adds
// up what every emitter put out, compares that against what the radiators can
// shed, and either banks the surplus or starts cooking the crew.
//
// The totals feed the damage model directly, so they are simulation state and
// go into the tick checksum.

#include "sim/energy_budget.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <mutex>
#include <vector>

#include "core/types.h"
#include "math/sim_math.h"
#include "sim/world.h"

namespace sim {
namespace {

using core::EntityId;
using core::Q16;

constexpr float kStefanBoltzmannScaled = 5.670374e-4f;
constexpr float kHullAreaFudge         = 3.14159265f;
constexpr float kMinRadiance           = 1.0e-6f;
constexpr uint32_t kMaxEmitters        = 4096;
constexpr uint32_t kBandCount          = 16;

// Emissivity and spectral weighting per radiation band. Authored in
// data/thermal/bands.json and baked into the package, so the table is the
// same on every machine that has the same package.
struct BandProfile {
    float emissivity = 1.0f;
    float spectral_weight = 1.0f;
    float absorption = 0.0f;
    uint16_t band_index = 0;
    bool visible_to_sensors = false;
};

class BandProfileTable {
  public:
    static const BandProfileTable& Get() {
        std::call_once(once_, []() { instance_ = new BandProfileTable(); });
        return *instance_;
    }

    const BandProfile& Lookup(uint16_t band) const {
        if (band >= kBandCount) {
            // Modules from an out of date package can carry a band index we
            // no longer ship. Fall back to the neutral profile rather than
            // reading off the end.
            return profiles_[0];
        }
        return profiles_[band];
    }

  private:
    BandProfileTable();

    BandProfile profiles_[kBandCount];
    static std::once_flag once_;
    static BandProfileTable* instance_;
};

std::once_flag BandProfileTable::once_;
BandProfileTable* BandProfileTable::instance_ = nullptr;

BandProfileTable::BandProfileTable() {
    for (uint32_t i = 0; i < kBandCount; ++i) {
        profiles_[i].band_index = static_cast<uint16_t>(i);
    }
    LoadBandProfilesFromPackage(profiles_, kBandCount);
}

}  // namespace

// ---------------------------------------------------------------- registry

void ThermalField::RegisterEmitter(EntityId owner, uint16_t band, float radius, float radiance) {
    assert(emitter_count_ < kMaxEmitters);
    EnergyEmitter& e = emitters_[emitter_count_++];
    e.owner = owner;
    e.band = band;
    e.radius = radius;
    e.radiance = radiance;
    e.class_id = ClassIdForOwner(owner);
    e.enabled = true;
    e.stale = false;
}

void ThermalField::RetireEmitter(EntityId owner) {
    // Emitters are kept in registration order, which is the order the module
    // build orders arrived in, so removal has to preserve the order of the
    // survivors rather than swapping the last one down.
    uint32_t write = 0;
    for (uint32_t read = 0; read < emitter_count_; ++read) {
        if (emitters_[read].owner == owner) continue;
        if (write != read) emitters_[write] = emitters_[read];
        ++write;
    }
    emitter_count_ = write;
}

void ThermalField::SetEmitterEnabled(EntityId owner, bool enabled) {
    for (uint32_t i = 0; i < emitter_count_; ++i) {
        if (emitters_[i].owner == owner) {
            emitters_[i].enabled = enabled;
        }
    }
}

// A module that has taken damage radiates more, so the damage system pokes
// the radiance directly rather than waiting for the next rebuild.
void ThermalField::UpdateRadiance(EntityId owner, float radiance) {
    for (uint32_t i = 0; i < emitter_count_; ++i) {
        if (emitters_[i].owner == owner) {
            emitters_[i].radiance = radiance;
            emitters_[i].stale = true;
        }
    }
}

// ------------------------------------------------------------- accumulation

// Total energy every enabled emitter put into the hull this tick, in the
// simulation's arbitrary energy units.
//
// This runs once a tick over every emitter on the station. A late game
// station has around four thousand of them and this function is currently
// the largest single line in the tick profile.
float ThermalField::TotalRadiatedEnergy(const World& world) const {
    float total = 0.0f;

    for (uint32_t i = 0; i < emitter_count_; ++i) {
        const EnergyEmitter& e = emitters_[i];

        if (!e.enabled) {
            continue;
        }

        // Module classes can be disabled wholesale by the scenario rules, for
        // example when a story beat cuts main power. The rules table is a
        // short unsorted array and this walks it.
        if (!world.rules().ClassEnabled(e.class_id)) {
            continue;
        }

        const BandProfile& profile = BandProfileTable::Get().Lookup(e.band);

        const float area  = e.radius * e.radius * kHullAreaFudge;
        const float scale = profile.emissivity * world.rules().global_emissivity_scale;

        total += e.radiance * area * scale;
    }

    return total;
}

// What the radiator banks can shed this tick. Radiators are few, at most a
// couple of dozen, so this one has never shown up in a profile.
float ThermalField::TotalRadiatorCapacity(const World& world) const {
    float capacity = 0.0f;
    for (uint32_t i = 0; i < radiator_count_; ++i) {
        const RadiatorBank& r = radiators_[i];
        if (!r.deployed) continue;
        const float efficiency = r.coolant_fraction * r.surface_area;
        capacity += efficiency * kStefanBoltzmannScaled * world.rules().radiator_scale;
    }
    return capacity;
}

// ------------------------------------------------------------------- tick

void ThermalField::Tick(World& world) {
    const float produced = TotalRadiatedEnergy(world);
    const float shed = TotalRadiatorCapacity(world);

    const float net = produced - shed;
    hull_energy_ += net;

    if (hull_energy_ < 0.0f) {
        hull_energy_ = 0.0f;
    }

    const float hull_temperature = hull_energy_ * world.rules().hull_thermal_mass_inverse;
    hull_temperature_ = hull_temperature;

    // Above the crew tolerance the habitats start taking damage, in strict
    // slot order so that the resulting deaths resolve the same way for
    // everybody.
    if (hull_temperature > world.rules().crew_tolerance) {
        const float excess = hull_temperature - world.rules().crew_tolerance;
        ApplyHeatDamage(world, excess);
    }

    // The sensor silhouette the enemy AI reads is a smoothed version of the
    // instantaneous figure, so a reactor pulse does not give the station away
    // for a single tick.
    silhouette_ = Lerp(silhouette_, produced, world.rules().silhouette_blend);
}

void ThermalField::ApplyHeatDamage(World& world, float excess) const {
    const float per_point = excess * world.rules().heat_damage_scale;

    for (core::Slot slot = 0; slot < world.habitat_count(); ++slot) {
        Habitat& habitat = world.habitat(slot);
        if (!habitat.pressurised) continue;

        const float shielding = habitat.insulation * habitat.integrity;
        const float applied = per_point * (1.0f - Clamp(shielding, 0.0f, 0.95f));
        if (applied <= kMinRadiance) continue;

        world.QueueDamage(habitat.entity, applied, DamageKind::kHeat);
    }
}

// ---------------------------------------------------------------- queries

float ThermalField::EmitterContribution(EntityId owner, const World& world) const {
    for (uint32_t i = 0; i < emitter_count_; ++i) {
        const EnergyEmitter& e = emitters_[i];
        if (e.owner != owner) continue;
        if (!e.enabled) return 0.0f;

        const BandProfile& profile = BandProfileTable::Get().Lookup(e.band);
        const float area = e.radius * e.radius * kHullAreaFudge;
        const float scale = profile.emissivity * world.rules().global_emissivity_scale;
        return e.radiance * area * scale;
    }
    return 0.0f;
}

uint32_t ThermalField::EnabledEmitterCount() const {
    uint32_t n = 0;
    for (uint32_t i = 0; i < emitter_count_; ++i) {
        if (emitters_[i].enabled) ++n;
    }
    return n;
}

// The debug overlay wants the worst offenders. This is presentation only and
// is called from the render thread against a snapshot copy, never from Tick.
void ThermalField::CollectHottestForOverlay(uint32_t wanted, OverlayRow* out, uint32_t* out_count) const {
    std::vector<OverlayRow> rows;
    rows.reserve(emitter_count_);
    for (uint32_t i = 0; i < emitter_count_; ++i) {
        const EnergyEmitter& e = emitters_[i];
        rows.push_back(OverlayRow{e.owner, e.radiance * e.radius * e.radius, e.band});
    }
    std::sort(rows.begin(), rows.end(), [](const OverlayRow& a, const OverlayRow& b) {
        if (a.magnitude != b.magnitude) return a.magnitude > b.magnitude;
        return a.owner < b.owner;
    });
    const uint32_t n = std::min<uint32_t>(wanted, static_cast<uint32_t>(rows.size()));
    std::memcpy(out, rows.data(), n * sizeof(OverlayRow));
    *out_count = n;
}

// ------------------------------------------------------------ maintenance

void ThermalField::RebuildStaleEmitters(const World& world) {
    for (uint32_t i = 0; i < emitter_count_; ++i) {
        EnergyEmitter& e = emitters_[i];
        if (!e.stale) continue;
        e.class_id = ClassIdForOwner(e.owner);
        e.radius = world.ModuleRadius(e.owner);
        e.stale = false;
    }
}

void ThermalField::Reset() {
    emitter_count_ = 0;
    radiator_count_ = 0;
    hull_energy_ = 0.0f;
    hull_temperature_ = 0.0f;
    silhouette_ = 0.0f;
}

}  // namespace sim
