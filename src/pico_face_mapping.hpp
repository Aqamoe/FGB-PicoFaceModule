#pragma once

#include "fgb_module_api.h"
#include "pico_face_protocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

// PICO 系数 → Unified Expressions / 逐眼属性的纯映射层。
// 不碰 socket、不碰宿主状态，方便单测。
namespace pico_face {

enum class Target : std::size_t {
    BrowInnerUpLeft,
    BrowInnerUpRight,
    BrowOuterUpLeft,
    BrowOuterUpRight,
    BrowLowererLeft,
    BrowPinchLeft,
    BrowLowererRight,
    BrowPinchRight,
    EyeSquintLeft,
    EyeSquintRight,
    EyeWideLeft,
    EyeWideRight,
    JawOpen,
    JawLeft,
    JawRight,
    JawForward,
    MouthClosed,
    CheekSquintLeft,
    CheekSquintRight,
    CheekPuffLeft,
    CheekPuffRight,
    NoseSneerLeft,
    NoseSneerRight,
    MouthUpperUpLeft,
    MouthUpperUpRight,
    MouthLowerDownLeft,
    MouthLowerDownRight,
    MouthFrownLeft,
    MouthFrownRight,
    MouthDimpleLeft,
    MouthDimpleRight,
    MouthUpperLeft,
    MouthLowerLeft,
    MouthUpperRight,
    MouthLowerRight,
    MouthPressLeft,
    MouthPressRight,
    MouthRaiserLower,
    MouthRaiserUpper,
    MouthCornerPullLeft,
    MouthCornerSlantLeft,
    MouthCornerPullRight,
    MouthCornerSlantRight,
    MouthStretchLeft,
    MouthStretchRight,
    LipFunnelUpperLeft,
    LipFunnelUpperRight,
    LipFunnelLowerLeft,
    LipFunnelLowerRight,
    LipPuckerUpperLeft,
    LipPuckerUpperRight,
    LipPuckerLowerLeft,
    LipPuckerLowerRight,
    LipSuckUpperLeft,
    LipSuckUpperRight,
    LipSuckLowerLeft,
    LipSuckLowerRight,
    TongueOut,
    Count,
};

inline constexpr std::size_t kTargetCount = static_cast<std::size_t>(Target::Count);

inline constexpr std::array<const char*, kTargetCount> kTargetNames = {
    "BrowInnerUpLeft",       "BrowInnerUpRight",     "BrowOuterUpLeft",
    "BrowOuterUpRight",      "BrowLowererLeft",      "BrowPinchLeft",
    "BrowLowererRight",      "BrowPinchRight",       "EyeSquintLeft",
    "EyeSquintRight",        "EyeWideLeft",          "EyeWideRight",
    "JawOpen",               "JawLeft",              "JawRight",
    "JawForward",            "MouthClosed",          "CheekSquintLeft",
    "CheekSquintRight",      "CheekPuffLeft",        "CheekPuffRight",
    "NoseSneerLeft",         "NoseSneerRight",       "MouthUpperUpLeft",
    "MouthUpperUpRight",     "MouthLowerDownLeft",   "MouthLowerDownRight",
    "MouthFrownLeft",        "MouthFrownRight",      "MouthDimpleLeft",
    "MouthDimpleRight",      "MouthUpperLeft",       "MouthLowerLeft",
    "MouthUpperRight",       "MouthLowerRight",      "MouthPressLeft",
    "MouthPressRight",       "MouthRaiserLower",     "MouthRaiserUpper",
    "MouthCornerPullLeft",   "MouthCornerSlantLeft", "MouthCornerPullRight",
    "MouthCornerSlantRight", "MouthStretchLeft",     "MouthStretchRight",
    "LipFunnelUpperLeft",    "LipFunnelUpperRight",  "LipFunnelLowerLeft",
    "LipFunnelLowerRight",   "LipPuckerUpperLeft",   "LipPuckerUpperRight",
    "LipPuckerLowerLeft",    "LipPuckerLowerRight",  "LipSuckUpperLeft",
    "LipSuckUpperRight",     "LipSuckLowerLeft",     "LipSuckLowerRight",
    "TongueOut",
};

struct TargetSet {
    std::array<std::int32_t, kTargetCount> index;

    TargetSet() { index.fill(-1); }
    std::int32_t operator[](Target target) const {
        return index[static_cast<std::size_t>(target)];
    }
};

// 按名字向宿主查下标；返回成功解析的数量。
std::size_t ResolveTargets(const FgbHostServices& host, TargetSet& targets);

// 填 attributes、88 形状与逐眼属性。会按 slot->struct_size 判断宿主支持到哪一段。
void WriteFrame(const Weights& weights, const TargetSet& targets, FgbFrameSlot* slot,
                float& smooth_mouth_left, float& smooth_mouth_right);

} // namespace pico_face
