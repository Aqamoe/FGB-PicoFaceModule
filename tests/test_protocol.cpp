#include "pico_face_protocol.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

void WriteU16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

void WriteU64(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        bytes[offset + static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF);
    }
}

void WriteF32(std::vector<std::uint8_t>& bytes, std::size_t offset, float value) {
    std::memcpy(bytes.data() + offset, &value, sizeof value);
}

std::vector<std::uint8_t> MakeV2Packet(std::uint8_t tracking_type, std::size_t size = 912) {
    std::vector<std::uint8_t> bytes(size, 0);
    bytes[0] = 0x74;
    bytes[1] = 0x01;
    bytes[2] = tracking_type;
    WriteU16(bytes, 6, 2);
    WriteU64(bytes, 8, 123456789ull);
    WriteU64(bytes, 16, 987654321ull);
    for (std::size_t i = 0; i < pico_face::kBlendShapeCount; ++i) {
        WriteF32(bytes, 24 + i * 4, static_cast<float>(i) / 100.0f);
    }
    return bytes;
}

std::vector<std::uint8_t> MakeLegacyPacket(std::size_t size = 892) {
    std::vector<std::uint8_t> bytes(size, 0);
    WriteU64(bytes, 0, 5555555ull);
    for (std::size_t i = 0; i < pico_face::kBlendShapeCount; ++i) {
        WriteF32(bytes, 8 + i * 4, static_cast<float>(i) / 100.0f);
    }
    return bytes;
}

bool Near(float a, float b) {
    return std::fabs(a - b) < 1e-5f;
}

void TestNewPacket() {
    auto bytes = MakeV2Packet(2);
    pico_face::Packet packet;
    Check(pico_face::ParseNewPacket(bytes.data(), bytes.size(), packet), "新协议：应解析成功");
    Check(packet.protocol == pico_face::Protocol::FaceV2, "新协议：protocol");
    Check(packet.tracking_type == 2, "新协议：trackingType");
    Check(packet.version == 2, "新协议：version");
    Check(packet.start_code1 == 0x74 && packet.start_code2 == 0x01, "新协议：起始码");
    Check(packet.datagram_bytes == 912, "新协议：包长");
    Check(packet.device_ts == 123456789ull, "新协议：设备时间戳");
    Check(Near(packet.weights[0], 0.0f), "新协议：第 0 项");
    Check(Near(packet.weights[51], 0.51f), "新协议：第 51 项");
}

void TestNewPacketEdgeCases() {
    pico_face::Packet packet;

    auto not_face = MakeV2Packet(1);
    Check(!pico_face::ParseNewPacket(not_face.data(), not_face.size(), packet),
          "新协议：trackingType != 2 应拒绝");

    auto short_packet = MakeV2Packet(2, 907);
    Check(!pico_face::ParseNewPacket(short_packet.data(), short_packet.size(), packet),
          "新协议：长度不足 908 应拒绝");

    auto exact = MakeV2Packet(2, 908);
    Check(pico_face::ParseNewPacket(exact.data(), exact.size(), packet),
          "新协议：908 字节应接受");
}

void TestLegacyPacket() {
    auto bytes = MakeLegacyPacket();
    pico_face::Packet packet;
    Check(pico_face::ParseLegacyPacket(bytes.data(), bytes.size(), packet), "旧协议：应解析成功");
    Check(packet.protocol == pico_face::Protocol::Legacy, "旧协议：protocol");
    Check(packet.device_ts == 5555555ull, "旧协议：时间戳");
    Check(Near(packet.weights[7], 0.07f), "旧协议：第 7 项");

    auto short_packet = MakeLegacyPacket(891);
    Check(!pico_face::ParseLegacyPacket(short_packet.data(), short_packet.size(), packet),
          "旧协议：长度不足 892 应拒绝");
}

void TestNanSanitized() {
    auto bytes = MakeV2Packet(2);
    const std::uint32_t nan_bits = 0x7FC00000u;
    std::memcpy(bytes.data() + 24, &nan_bits, sizeof nan_bits);
    pico_face::Packet packet;
    Check(pico_face::ParseNewPacket(bytes.data(), bytes.size(), packet), "NaN：应解析成功");
    Check(packet.weights[0] == 0.0f, "NaN：应清成 0");
}

} // namespace

int main() {
    TestNewPacket();
    TestNewPacketEdgeCases();
    TestLegacyPacket();
    TestNanSanitized();

    if (g_failures == 0) {
        std::printf("全部协议测试通过\n");
        return 0;
    }
    std::printf("%d 项失败\n", g_failures);
    return 1;
}
