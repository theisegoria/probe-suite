// position_stream.h
//
// Wire format for simulation positions. See position_stream.cpp for the unit
// chain, which is the part that gets people into trouble.

#pragma once

#include <cstdint>

#include "eh/math/fixed.h"

namespace nl {
namespace net {

// Minimal bit writer. The real one lives in net/bitstream.h and has the same
// interface; this declaration exists so the serialisers can be unit tested
// against a recording writer.
class BitWriter {
public:
    void WriteBit(std::uint32_t bit);
    void WriteUnsigned(std::uint32_t value, std::int32_t bits);
    void WriteSigned(std::int32_t value, std::int32_t bits);
    std::int32_t BitsWritten() const;

private:
    std::uint8_t* buffer_ = nullptr;
    std::int32_t capacity_bits_ = 0;
    std::int32_t cursor_bits_ = 0;
};

// Layer conversions, exposed for tests.
std::int32_t AxisToCentimetres(math::fixed_t axis);
std::int32_t CentimetresToQuanta(std::int32_t centimetres);

// The full world unit to wire chain for one axis.
std::int32_t WritePositionAxis(math::fixed_t axis);

void WriteKeyframePosition(BitWriter& out, const math::FixedVec3& p);
void WriteDeltaPosition(BitWriter& out, const math::FixedVec3& p,
                        const math::FixedVec3& baseline);

math::fixed_t QuantaToFixedForPresentation(std::int32_t quanta);

}  // namespace net
}  // namespace nl
