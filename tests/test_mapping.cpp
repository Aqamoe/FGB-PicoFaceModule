#include "pico_face_mapping.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

bool Near(float a, float b) {
    return std::fabs(a - b) < 1e-4f;
}

pico_face::TargetSet IdentityTargets() {
    pico_face::TargetSet targets;
    for (std::size_t i = 0; i < pico_face::kTargetCount; ++i) {
        targets.index[i] = static_cast<std::int32_t>(i);
    }
    return targets;
}

FgbFrameSlot MakeSlot() {
    FgbFrameSlot slot{};
    slot.struct_size = sizeof(FgbFrameSlot);
    slot.abi_version = FGB_MODULE_ABI_V2;
    slot.shape_count = static_cast<std::uint32_t>(pico_face::kTargetCount);
    return slot;
}

std::size_t IndexOf(pico_face::Target target) {
    return static_cast<std::size_t>(target);
}

void TestExpressionAndEyes() {
    using namespace pico_face;

    Weights weights{};
    weights[static_cast<std::size_t>(BlendShape::JawOpen)] = 0.5f;
    weights[static_cast<std::size_t>(BlendShape::EyeBlinkL)] = 0.75f;
    weights[static_cast<std::size_t>(BlendShape::EyeLookInL)] = 0.5f;
    weights[static_cast<std::size_t>(BlendShape::EyeLookUpL)] = 0.5f;
    weights[static_cast<std::size_t>(BlendShape::MouthSmileL)] = 0.7f;
    weights[static_cast<std::size_t>(BlendShape::MouthSmileR)] = 0.7f;

    FgbFrameSlot slot = MakeSlot();
    float smooth_left = 0.0f;
    float smooth_right = 0.0f;
    WriteFrame(weights, IdentityTargets(), &slot, smooth_left, smooth_right);

    Check((slot.attributes & FGB_ATTR_EXPRESSION) != 0, "attributes: expression");
    Check((slot.attributes & FGB_ATTR_LEFT_OPENNESS) != 0, "attributes: left openness");
    Check((slot.attributes & FGB_ATTR_RIGHT_OPENNESS) != 0, "attributes: right openness");
    Check((slot.attributes & FGB_ATTR_LEFT_GAZE) != 0, "attributes: left gaze");
    Check((slot.attributes & FGB_ATTR_RIGHT_GAZE) != 0, "attributes: right gaze");
    Check((slot.attributes & FGB_ATTR_HEAD) == 0, "attributes: 不声明头姿");
    Check((slot.attributes & FGB_ATTR_POINT_VIEWPORT) == 0, "attributes: 不声明视口点");

    const std::size_t jaw = IndexOf(Target::JawOpen);
    Check(slot.shape_valid[jaw] != 0, "JawOpen: valid");
    Check(Near(slot.shapes[jaw], 0.5f), "JawOpen: 0.5");
    Check(Near(slot.shapes[IndexOf(Target::MouthClosed)], 0.0f), "MouthClosed: 0");

    Check(Near(slot.left_eye.openness, 0.25f), "左眼 openness = 1 - blink");
    Check(Near(slot.right_eye.openness, 1.0f), "右眼 openness = 1");

    const float length = std::sqrt(0.5f * 0.5f + 0.5f * 0.5f + 1.0f);
    Check(Near(slot.left_eye.gaze_dir[0], 0.5f / length), "左眼 gaze x");
    Check(Near(slot.left_eye.gaze_dir[1], 0.5f / length), "左眼 gaze y");
    Check(Near(slot.left_eye.gaze_dir[2], -1.0f / length), "左眼 gaze z");
    Check(Near(slot.right_eye.gaze_dir[0], 0.0f), "右眼 gaze x = 0");
    Check(Near(slot.right_eye.gaze_dir[2], -1.0f), "右眼 gaze z = -1");

    Check(Near(slot.shapes[IndexOf(Target::MouthCornerPullLeft)], 0.7f), "左嘴角上提");
    Check(Near(slot.shapes[IndexOf(Target::MouthCornerSlantLeft)], 0.7f), "左嘴角斜拉");
    Check(Near(slot.shapes[IndexOf(Target::MouthCornerPullRight)], 0.7f), "右嘴角上提");
}

void TestClamping() {
    using namespace pico_face;

    Weights weights{};
    weights[static_cast<std::size_t>(BlendShape::BrowInnerUp)] = 2.0f;
    weights[static_cast<std::size_t>(BlendShape::MouthFrownL)] = -1.0f;

    FgbFrameSlot slot = MakeSlot();
    float smooth_left = 0.0f;
    float smooth_right = 0.0f;
    WriteFrame(weights, IdentityTargets(), &slot, smooth_left, smooth_right);

    Check(Near(slot.shapes[IndexOf(Target::BrowInnerUpLeft)], 1.0f), "超上限被夹到 1");
    Check(Near(slot.shapes[IndexOf(Target::MouthFrownLeft)], 0.0f), "负值被夹到 0");
}

void TestCheekPuffSplit() {
    using namespace pico_face;

    Weights weights{};
    weights[static_cast<std::size_t>(BlendShape::CheekPuff)] = 0.5f;
    weights[static_cast<std::size_t>(BlendShape::MouthLeft)] = 1.0f;

    FgbFrameSlot slot = MakeSlot();
    float smooth_left = 0.0f;
    float smooth_right = 0.0f;
    WriteFrame(weights, IdentityTargets(), &slot, smooth_left, smooth_right);

    Check(Near(smooth_left, 0.5f), "嘴角平滑：第一帧取一半");
    Check(Near(slot.shapes[IndexOf(Target::CheekPuffLeft)], 0.5f), "左脸颊 = puff");
    Check(Near(slot.shapes[IndexOf(Target::CheekPuffRight)], 1.0f), "右脸颊 = puff + 嘴角差");
}

void TestShapeCountGuard() {
    using namespace pico_face;

    Weights weights{};
    weights[static_cast<std::size_t>(BlendShape::JawOpen)] = 0.5f;

    FgbFrameSlot slot = MakeSlot();
    slot.shape_count = 13; // 只允许写到 JawOpen（12）
    float smooth_left = 0.0f;
    float smooth_right = 0.0f;
    WriteFrame(weights, IdentityTargets(), &slot, smooth_left, smooth_right);

    Check(slot.shape_valid[IndexOf(Target::JawOpen)] != 0, "shape_count 内：JawOpen 写入");
    Check(slot.shape_valid[IndexOf(Target::MouthDimpleLeft)] == 0, "shape_count 外：跳过");
    Check((slot.attributes & FGB_ATTR_EXPRESSION) != 0, "attributes：仍声明表情");
}

void TestStructSizeGuards() {
    using namespace pico_face;

    Weights weights{};
    weights[static_cast<std::size_t>(BlendShape::JawOpen)] = 0.5f;
    float smooth_left = 0.0f;
    float smooth_right = 0.0f;

    FgbFrameSlot tiny{};
    tiny.struct_size = 16;
    WriteFrame(weights, IdentityTargets(), &tiny, smooth_left, smooth_right);
    Check(tiny.attributes == 0, "旧宿主：连 attributes 都没有时不写");

    FgbFrameSlot shapes_only{};
    shapes_only.struct_size = static_cast<std::uint32_t>(
        offsetof(FgbFrameSlot, right_eye) + sizeof(FgbEyeAttributes) - 1);
    shapes_only.shape_count = static_cast<std::uint32_t>(kTargetCount);
    WriteFrame(weights, IdentityTargets(), &shapes_only, smooth_left, smooth_right);
    Check((shapes_only.attributes & FGB_ATTR_EXPRESSION) != 0, "只有形状区：声明表情");
    Check((shapes_only.attributes & FGB_ATTR_LEFT_OPENNESS) == 0, "只有形状区：不声明眼睛");
    Check(shapes_only.left_eye.openness == 0.0f, "只有形状区：不写眼睛");
}

} // namespace

int main() {
    TestExpressionAndEyes();
    TestClamping();
    TestCheekPuffSplit();
    TestShapeCountGuard();
    TestStructSizeGuards();

    if (g_failures == 0) {
        std::printf("全部映射测试通过\n");
        return 0;
    }
    std::printf("%d 项失败\n", g_failures);
    return 1;
}
