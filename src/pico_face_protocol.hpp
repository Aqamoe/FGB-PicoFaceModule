#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// PICO Connect 面捕 UDP 包格式。
//
// 两个端口对应 settings.json 里的 faceTrackingTransferProtocol：
//   29765（=2）：16 字节头 + 892 字节体，头里 trackingType == 2 才是面部数据。
//   29763（=1）：VRCFT 1.3 之前的裸体，没有头也没有 trackingType。
//
// 实测（协议 2）单包 912 字节：908 字节有效数据 + 尾随 4 字节，
// 尾部不参与解析，按 >= 908 接受。
namespace pico_face {

inline constexpr std::uint16_t kPortFaceV2 = 29765;
inline constexpr std::uint16_t kPortFaceLegacy = 29763;

inline constexpr std::size_t kBlendShapeCount = 72;
inline constexpr std::size_t kHeaderBytes = 16;
inline constexpr std::size_t kBodyBytes = 892; // 8 + 72*4 + 10*4 + 4 + 10*4 + 128*4
inline constexpr std::size_t kFacePacketMinBytes = kHeaderBytes + kBodyBytes;
inline constexpr std::uint8_t kTrackingTypeFace = 2;

// PICO 的 72 项系数顺序（前 52 项是 ARKit 命名，后 20 项是视素）。
enum class BlendShape : std::size_t {
    EyeLookDownL = 0,
    NoseSneerL = 1,
    EyeLookInL = 2,
    BrowInnerUp = 3,
    BrowDownR = 4,
    MouthClose = 5,
    MouthLowerDownR = 6,
    JawOpen = 7,
    MouthUpperUpR = 8,
    MouthShrugUpper = 9,
    MouthFunnel = 10,
    EyeLookInR = 11,
    EyeLookDownR = 12,
    NoseSneerR = 13,
    MouthRollUpper = 14,
    JawRight = 15,
    BrowDownL = 16,
    MouthShrugLower = 17,
    MouthRollLower = 18,
    MouthSmileL = 19,
    MouthPressL = 20,
    MouthSmileR = 21,
    MouthPressR = 22,
    MouthDimpleR = 23,
    MouthLeft = 24,
    JawForward = 25,
    EyeSquintL = 26,
    MouthFrownL = 27,
    EyeBlinkL = 28,
    CheekSquintL = 29,
    BrowOuterUpL = 30,
    EyeLookUpL = 31,
    JawLeft = 32,
    MouthStretchL = 33,
    MouthPucker = 34,
    EyeLookUpR = 35,
    BrowOuterUpR = 36,
    CheekSquintR = 37,
    EyeBlinkR = 38,
    MouthUpperUpL = 39,
    MouthFrownR = 40,
    EyeSquintR = 41,
    MouthStretchR = 42,
    CheekPuff = 43,
    EyeLookOutL = 44,
    EyeLookOutR = 45,
    EyeWideR = 46,
    EyeWideL = 47,
    MouthRight = 48,
    MouthDimpleL = 49,
    MouthLowerDownL = 50,
    TongueOut = 51,
    PP = 52,
    CH = 53,
    VisemeO = 54,
    VisemeOh = 55,
    VisemeI = 56,
    VisemeU = 57,
    VisemeRR = 58,
    VisemeXX = 59,
    VisemeAa = 60,
    VisemeIi = 61,
    VisemeFF = 62,
    VisemeUU = 63,
    VisemeTH = 64,
    VisemeKK = 65,
    VisemeSS = 66,
    VisemeE = 67,
    VisemeDD = 68,
    VisemeEE = 69,
    VisemeNN = 70,
    VisemeSil = 71,
    Count,
};

static_assert(static_cast<std::size_t>(BlendShape::Count) == kBlendShapeCount,
              "PICO 面捕系数数量必须是 72");

using Weights = std::array<float, kBlendShapeCount>;

inline float Weight(const Weights& weights, BlendShape shape) {
    return weights[static_cast<std::size_t>(shape)];
}

enum class Protocol { Legacy, FaceV2 };

struct Packet {
    Protocol protocol = Protocol::Legacy;
    std::uint8_t start_code1 = 0;
    std::uint8_t start_code2 = 0;
    std::uint8_t tracking_type = 0;
    std::uint8_t sub_type = 0;
    std::uint16_t version = 0;
    std::uint64_t device_ts = 0;
    std::size_t datagram_bytes = 0;
    Weights weights{};
};

bool ParseNewPacket(const std::uint8_t* data, std::size_t size, Packet& out);
bool ParseLegacyPacket(const std::uint8_t* data, std::size_t size, Packet& out);

} // namespace pico_face
