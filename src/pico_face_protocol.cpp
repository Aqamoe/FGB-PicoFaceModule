#include "pico_face_protocol.hpp"

#include <cmath>
#include <cstring>

namespace pico_face {
namespace {

std::uint16_t ReadU16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}

std::uint64_t ReadU64(const std::uint8_t* p) {
    std::uint64_t value = 0;
    for (int i = 7; i >= 0; --i) {
        value = (value << 8) | p[i];
    }
    return value;
}

float ReadF32(const std::uint8_t* p) {
    float value = 0.0f;
    std::memcpy(&value, p, sizeof value);
    return std::isfinite(value) ? value : 0.0f;
}

void ReadWeights(const std::uint8_t* first, Packet& out) {
    for (std::size_t i = 0; i < kBlendShapeCount; ++i) {
        out.weights[i] = ReadF32(first + i * sizeof(float));
    }
}

} // namespace

bool ParseNewPacket(const std::uint8_t* data, std::size_t size, Packet& out) {
    if (data == nullptr || size < kFacePacketMinBytes) {
        return false;
    }
    if (data[2] != kTrackingTypeFace) {
        return false;
    }

    out = Packet{};
    out.protocol = Protocol::FaceV2;
    out.start_code1 = data[0];
    out.start_code2 = data[1];
    out.tracking_type = data[2];
    out.sub_type = data[3];
    out.version = ReadU16(data + 6);
    out.device_ts = ReadU64(data + 8);
    out.datagram_bytes = size;

    const std::uint8_t* body = data + kHeaderBytes;
    ReadWeights(body + 8, out); // 体开头是 8 字节时间戳
    return true;
}

bool ParseLegacyPacket(const std::uint8_t* data, std::size_t size, Packet& out) {
    if (data == nullptr || size < kBodyBytes) {
        return false;
    }

    out = Packet{};
    out.protocol = Protocol::Legacy;
    out.device_ts = ReadU64(data);
    out.datagram_bytes = size;
    ReadWeights(data + 8, out);
    return true;
}

} // namespace pico_face
