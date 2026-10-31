#include "pico_face_mapping.hpp"

#include <cmath>

namespace pico_face {
namespace {

float Clamp01(float value) {
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

void Put(const TargetSet& targets, FgbFrameSlot* slot, Target target, float value) {
    const std::int32_t index = targets[target];
    if (index < 0 || static_cast<std::uint32_t>(index) >= slot->shape_count ||
        static_cast<std::uint32_t>(index) >= FGB_MAX_SHAPES) {
        return;
    }
    slot->shapes[index] = Clamp01(value);
    slot->shape_valid[index] = 1;
}

void ApplyExpressions(const Weights& weights, const TargetSet& targets, FgbFrameSlot* slot,
                      float& smooth_mouth_left, float& smooth_mouth_right) {
    const auto p = [&weights](BlendShape shape) { return Weight(weights, shape); };
    const auto put = [&targets, slot](Target target, float value) {
        Put(targets, slot, target, value);
    };

    // 眉
    const float brow_inner_up = p(BlendShape::BrowInnerUp);
    put(Target::BrowInnerUpLeft, brow_inner_up);
    put(Target::BrowInnerUpRight, brow_inner_up);
    put(Target::BrowOuterUpLeft, p(BlendShape::BrowOuterUpL));
    put(Target::BrowOuterUpRight, p(BlendShape::BrowOuterUpR));
    const float brow_down_left = p(BlendShape::BrowDownL);
    const float brow_down_right = p(BlendShape::BrowDownR);
    put(Target::BrowLowererLeft, brow_down_left);
    put(Target::BrowPinchLeft, brow_down_left);
    put(Target::BrowLowererRight, brow_down_right);
    put(Target::BrowPinchRight, brow_down_right);

    // 眼周表情
    put(Target::EyeSquintLeft, p(BlendShape::EyeSquintL));
    put(Target::EyeSquintRight, p(BlendShape::EyeSquintR));
    put(Target::EyeWideLeft, p(BlendShape::EyeWideL));
    put(Target::EyeWideRight, p(BlendShape::EyeWideR));

    // 下颌
    const float jaw_open = p(BlendShape::JawOpen);
    const float roll_lower = p(BlendShape::MouthRollLower);
    put(Target::JawOpen, jaw_open);
    put(Target::JawLeft, p(BlendShape::JawLeft));
    put(Target::JawRight, p(BlendShape::JawRight));
    put(Target::JawForward, p(BlendShape::JawForward));
    put(Target::MouthClosed, p(BlendShape::MouthClose));

    // 脸颊。PICO 只给合并的 CheekPuff，用嘴角偏移把它分到左右。
    put(Target::CheekSquintLeft, p(BlendShape::CheekSquintL));
    put(Target::CheekSquintRight, p(BlendShape::CheekSquintR));
    smooth_mouth_left += (p(BlendShape::MouthLeft) - smooth_mouth_left) * 0.5f;
    smooth_mouth_right += (p(BlendShape::MouthRight) - smooth_mouth_right) * 0.5f;
    const float cheek_puff = p(BlendShape::CheekPuff);
    float cheek_puff_left = cheek_puff;
    float cheek_puff_right = cheek_puff;
    if (cheek_puff > 0.1f) {
        constexpr float kDiffThreshold = 0.1f;
        if (smooth_mouth_left > smooth_mouth_right + kDiffThreshold) {
            cheek_puff_right = cheek_puff + smooth_mouth_left;
        } else if (smooth_mouth_right > smooth_mouth_left + kDiffThreshold) {
            cheek_puff_left = cheek_puff + smooth_mouth_right;
        }
    }
    put(Target::CheekPuffLeft, cheek_puff_left);
    put(Target::CheekPuffRight, cheek_puff_right);

    // 鼻
    put(Target::NoseSneerLeft, p(BlendShape::NoseSneerL));
    put(Target::NoseSneerRight, p(BlendShape::NoseSneerR));

    // 嘴唇
    put(Target::MouthUpperUpLeft, p(BlendShape::MouthUpperUpL));
    put(Target::MouthUpperUpRight, p(BlendShape::MouthUpperUpR));
    put(Target::MouthLowerDownLeft, p(BlendShape::MouthLowerDownL));
    put(Target::MouthLowerDownRight, p(BlendShape::MouthLowerDownR));

    float frown_left = p(BlendShape::MouthFrownL);
    float frown_right = p(BlendShape::MouthFrownR);
    if (jaw_open > 0.1f) {
        frown_left *= 0.5f;
        frown_right *= 0.5f;
    } else if (roll_lower > 0.2f) {
        frown_left = frown_left * 2.5f + roll_lower;
        frown_right = frown_right * 2.5f + roll_lower;
    }
    put(Target::MouthFrownLeft, frown_left);
    put(Target::MouthFrownRight, frown_right);

    put(Target::MouthDimpleLeft, p(BlendShape::MouthDimpleL));
    put(Target::MouthDimpleRight, p(BlendShape::MouthDimpleR));
    put(Target::MouthUpperLeft, p(BlendShape::MouthLeft));
    put(Target::MouthLowerLeft, p(BlendShape::MouthLeft));
    put(Target::MouthUpperRight, p(BlendShape::MouthRight));
    put(Target::MouthLowerRight, p(BlendShape::MouthRight));
    put(Target::MouthPressLeft, p(BlendShape::MouthPressL));
    put(Target::MouthPressRight, p(BlendShape::MouthPressR));
    put(Target::MouthRaiserLower, p(BlendShape::MouthShrugLower));
    put(Target::MouthRaiserUpper, p(BlendShape::MouthShrugUpper));

    // 微笑。与 VRCFT PICO 模块保持一致：左眼的 Slant 多减一次 MouthRollLower。
    const float smile_left = p(BlendShape::MouthSmileL) - roll_lower;
    const float smile_right = p(BlendShape::MouthSmileR) - roll_lower;
    if (roll_lower < 0.2f) {
        put(Target::MouthCornerPullLeft, smile_left);
        put(Target::MouthCornerSlantLeft, smile_left - roll_lower);
        put(Target::MouthCornerPullRight, smile_right);
        put(Target::MouthCornerSlantRight, smile_right);
    } else {
        put(Target::MouthCornerPullLeft, 0.0f);
        put(Target::MouthCornerSlantLeft, 0.0f);
        put(Target::MouthCornerPullRight, 0.0f);
        put(Target::MouthCornerSlantRight, 0.0f);
    }

    put(Target::MouthStretchLeft, p(BlendShape::MouthStretchL));
    put(Target::MouthStretchRight, p(BlendShape::MouthStretchR));

    // 唇形。PICO 的 Pucker 在唇压住时同时承担 Funnel。
    const float pucker = p(BlendShape::MouthPucker);
    const float funnel = p(BlendShape::MouthFunnel);
    const bool funnel_left = pucker > 0.3f && p(BlendShape::MouthPressL) < 0.2f;
    const bool funnel_right = pucker > 0.3f && p(BlendShape::MouthPressR) < 0.2f;
    const float funnel_value_left = funnel_left ? pucker : funnel;
    const float funnel_value_right = funnel_right ? pucker : funnel;
    put(Target::LipFunnelUpperLeft, funnel_value_left);
    put(Target::LipFunnelUpperRight, funnel_value_right);
    put(Target::LipFunnelLowerLeft, funnel_value_left);
    put(Target::LipFunnelLowerRight, funnel_value_right);

    put(Target::LipPuckerUpperLeft, pucker);
    put(Target::LipPuckerUpperRight, pucker);
    put(Target::LipPuckerLowerLeft, pucker);
    put(Target::LipPuckerLowerRight, pucker);

    const float roll_upper = p(BlendShape::MouthRollUpper);
    put(Target::LipSuckUpperLeft, roll_upper);
    put(Target::LipSuckUpperRight, roll_upper);
    put(Target::LipSuckLowerLeft, roll_lower);
    put(Target::LipSuckLowerRight, roll_lower);

    put(Target::TongueOut, p(BlendShape::TongueOut));
}

void SetGazeDirection(float x, float y, float (&direction)[3]) {
    const float length = std::sqrt(x * x + y * y + 1.0f);
    direction[0] = x / length;
    direction[1] = y / length;
    direction[2] = -1.0f / length;
}

void ApplyEyes(const Weights& weights, FgbFrameSlot* slot) {
    const auto p = [&weights](BlendShape shape) { return Weight(weights, shape); };

    // 睁眼度直接取反眨眼权重；方向由四向 look 权重合成，正前方 (0,0,-1) 是合法结果。
    slot->left_eye.openness = Clamp01(1.0f - p(BlendShape::EyeBlinkL));
    slot->left_eye.openness_confidence = 1.0f;
    slot->left_eye.gaze_confidence = 0.6f;
    SetGazeDirection(p(BlendShape::EyeLookInL) - p(BlendShape::EyeLookOutL),
                     p(BlendShape::EyeLookUpL) - p(BlendShape::EyeLookDownL),
                     slot->left_eye.gaze_dir);

    slot->right_eye.openness = Clamp01(1.0f - p(BlendShape::EyeBlinkR));
    slot->right_eye.openness_confidence = 1.0f;
    slot->right_eye.gaze_confidence = 0.6f;
    SetGazeDirection(p(BlendShape::EyeLookOutR) - p(BlendShape::EyeLookInR),
                     p(BlendShape::EyeLookUpR) - p(BlendShape::EyeLookDownR),
                     slot->right_eye.gaze_dir);
}

} // namespace

std::size_t ResolveTargets(const FgbHostServices& host, TargetSet& targets) {
    std::size_t resolved = 0;
    for (std::size_t i = 0; i < kTargetCount; ++i) {
        targets.index[i] = host.shape_index(host.ctx, kTargetNames[i]);
        if (targets.index[i] >= 0) {
            ++resolved;
        }
    }
    return resolved;
}

void WriteFrame(const Weights& weights, const TargetSet& targets, FgbFrameSlot* slot,
                float& smooth_mouth_left, float& smooth_mouth_right) {
    constexpr std::uint32_t kAttributesEnd =
        static_cast<std::uint32_t>(offsetof(FgbFrameSlot, attributes) + sizeof(std::uint64_t));
    constexpr std::uint32_t kShapesEnd =
        static_cast<std::uint32_t>(offsetof(FgbFrameSlot, shape_valid) + FGB_MAX_SHAPES);
    constexpr std::uint32_t kEyesEnd = static_cast<std::uint32_t>(
        offsetof(FgbFrameSlot, right_eye) + sizeof(FgbEyeAttributes));

    if (slot->struct_size < kAttributesEnd) {
        return;
    }

    std::uint64_t attributes = 0;
    if (slot->struct_size >= kShapesEnd) {
        attributes |= FGB_ATTR_EXPRESSION;
    }
    if (slot->struct_size >= kEyesEnd) {
        attributes |= FGB_ATTR_LEFT_OPENNESS | FGB_ATTR_RIGHT_OPENNESS |
                      FGB_ATTR_LEFT_GAZE | FGB_ATTR_RIGHT_GAZE;
    }
    slot->attributes = attributes;

    if (slot->struct_size >= kShapesEnd) {
        ApplyExpressions(weights, targets, slot, smooth_mouth_left, smooth_mouth_right);
    }
    if (slot->struct_size >= kEyesEnd) {
        ApplyEyes(weights, slot);
    }
}

} // namespace pico_face
