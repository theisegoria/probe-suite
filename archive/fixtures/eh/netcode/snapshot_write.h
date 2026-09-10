// snapshot_write.h
//
// Snapshot serialisation declarations.

#pragma once

#include <cstdint>

namespace nl {
namespace netcode {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct EntitySnapshot {
    std::uint32_t entity_id = 0;
    Vec3 position;
    Vec3 velocity;
    Quat rotation;
    std::uint32_t state_flags = 0;
};

class BitWriter {
public:
    void WriteBit(std::uint32_t bit);
    void WriteUnsigned(std::uint32_t value, std::int32_t bits);
    void WriteSigned(std::int32_t value, std::int32_t bits);
    std::int32_t BitsWritten() const;
};

std::int32_t WritePositionDeltaAxis(BitWriter& out, float current_units,
                                    float baseline_units);
void WritePositionDelta(BitWriter& out, const Vec3& current, const Vec3& baseline);
void WriteAbsolutePosition(BitWriter& out, const Vec3& p);
void WriteVelocity(BitWriter& out, const Vec3& v);
void WriteRotation(BitWriter& out, const Quat& q);
void WriteEntitySnapshot(BitWriter& out, const EntitySnapshot& now,
                         const EntitySnapshot& baseline, bool keyframe);

}  // namespace netcode
}  // namespace nl
