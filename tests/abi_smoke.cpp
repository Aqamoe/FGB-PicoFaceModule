// 端到端冒烟：按宿主的调用顺序加载 DLL，用合成 UDP 包喂数据。
//
// 用法：pico-face-abi-smoke <pico-connect-face.dll>
// 退出码：0 = 通过；2 = 端口被占用（先关 VRCFT 再跑）；1 = 其他失败。

#include "fgb_module_api.h"
#include "pico_face_protocol.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

struct Harness {
    FgbFrameSlot slot{};
    FgbFrameSlot last{};
    bool committed = false;
    bool saw_active = false;
    int last_status = -1;
    std::string last_detail;
    std::vector<std::string> shape_names;
};

int32_t HostShapeIndex(void* ctx, const char* name) {
    auto* harness = static_cast<Harness*>(ctx);
    if (name == nullptr) {
        return -1;
    }
    for (std::size_t i = 0; i < harness->shape_names.size(); ++i) {
        if (harness->shape_names[i] == name) {
            return static_cast<std::int32_t>(i);
        }
    }
    harness->shape_names.emplace_back(name);
    return static_cast<std::int32_t>(harness->shape_names.size() - 1);
}

FgbFrameSlot* HostAcquire(void* ctx) {
    auto* harness = static_cast<Harness*>(ctx);
    harness->slot = FgbFrameSlot{};
    harness->slot.struct_size = sizeof(FgbFrameSlot);
    harness->slot.abi_version = FGB_MODULE_ABI_V2;
    harness->slot.shape_count = FGB_MAX_SHAPES;
    return &harness->slot;
}

void HostCommit(void* ctx, FgbFrameSlot* slot, std::uint64_t /*capture_ts_ns*/) {
    auto* harness = static_cast<Harness*>(ctx);
    harness->last = *slot;
    harness->committed = true;
}

void HostLog(void* /*ctx*/, int level, const char* message) {
    std::printf("[log %d] %s\n", level, message != nullptr ? message : "");
}

void HostStatus(void* ctx, int state, const char* detail) {
    auto* harness = static_cast<Harness*>(ctx);
    harness->last_status = state;
    harness->last_detail = detail != nullptr ? detail : "";
    if (state == FGB_STATUS_ACTIVE) {
        harness->saw_active = true;
    }
}

int32_t IndexOf(const Harness& harness, const char* name) {
    for (std::size_t i = 0; i < harness.shape_names.size(); ++i) {
        if (harness.shape_names[i] == name) {
            return static_cast<std::int32_t>(i);
        }
    }
    return -1;
}

void SetWeight(std::vector<std::uint8_t>& packet, pico_face::BlendShape shape, float value) {
    const std::size_t offset = 24 + static_cast<std::size_t>(shape) * 4;
    std::memcpy(packet.data() + offset, &value, sizeof value);
}

std::vector<std::uint8_t> MakeFacePacket() {
    std::vector<std::uint8_t> packet(912, 0);
    packet[0] = 0x74;
    packet[1] = 0x01;
    packet[2] = 2; // trackingType = face
    packet[6] = 2; // version
    SetWeight(packet, pico_face::BlendShape::JawOpen, 0.5f);
    SetWeight(packet, pico_face::BlendShape::EyeBlinkL, 0.75f);
    SetWeight(packet, pico_face::BlendShape::EyeLookInL, 0.5f);
    SetWeight(packet, pico_face::BlendShape::EyeLookUpL, 0.5f);
    SetWeight(packet, pico_face::BlendShape::MouthSmileL, 0.7f);
    return packet;
}

bool SendTo(const SOCKET socket, const sockaddr_in& target, const std::vector<std::uint8_t>& data) {
    return ::sendto(socket, reinterpret_cast<const char*>(data.data()),
                    static_cast<int>(data.size()), 0,
                    reinterpret_cast<const sockaddr*>(&target), sizeof target) ==
           static_cast<int>(data.size());
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("用法: %s <pico-connect-face.dll>\n", argv[0]);
        return 1;
    }

    const HMODULE dll = ::LoadLibraryA(argv[1]);
    if (dll == nullptr) {
        std::printf("LoadLibrary 失败，错误码 %lu\n", ::GetLastError());
        return 1;
    }

    using QueryFn = const FgbModule* (*)(std::uint32_t);
    const auto query =
        reinterpret_cast<QueryFn>(::GetProcAddress(dll, "fgb_module_query_v2"));
    if (query == nullptr) {
        std::printf("找不到 fgb_module_query_v2\n");
        return 1;
    }
    Check(query(1) == nullptr, "ABI 1 应返回 NULL");

    const FgbModule* module = query(FGB_MODULE_ABI_V2);
    Check(module != nullptr, "ABI 2 应返回模块");
    if (module == nullptr) {
        return 1;
    }

    Harness harness;
    FgbHostServices services{};
    services.struct_size = sizeof(FgbHostServices);
    services.abi_version = FGB_MODULE_ABI_V2;
    services.log = &HostLog;
    services.report_status = &HostStatus;
    services.acquire_frame_slot = &HostAcquire;
    services.commit_frame_slot = &HostCommit;
    services.shape_index = &HostShapeIndex;
    services.ctx = &harness;

    FgbInitParams params{};
    params.struct_size = sizeof(FgbInitParams);
    params.abi_version = FGB_MODULE_ABI_V2;
    params.tick_hz = 90;
    params.module_dir = ".";
    params.host_version = "smoke";

    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);

    void* self = nullptr;
    if (module->initialize(&self, &services, &params) != 1) {
        std::printf("initialize 失败：%s\n", harness.last_detail.c_str());
        WSACleanup();
        return harness.last_detail.find("占用") != std::string::npos ? 2 : 1;
    }

    std::printf("模块已初始化：%d 个形状名被查询\n",
                static_cast<int>(harness.shape_names.size()));

    const SOCKET sender = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(pico_face::kPortFaceV2);
    ::inet_pton(AF_INET, "127.0.0.1", &target.sin_addr);

    const auto packet = MakeFacePacket();
    std::atomic<bool> stop{false};
    std::thread sender_thread([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            SendTo(sender, target, packet);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    for (int i = 0; i < 200; ++i) {
        module->update(self);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    stop.store(true, std::memory_order_relaxed);
    sender_thread.join();
    ::closesocket(sender);

    Check(harness.committed, "应至少提交一帧");
    Check(harness.saw_active, "收到数据后应上报 ACTIVE");
    Check((harness.last.attributes & FGB_ATTR_EXPRESSION) != 0, "帧属性：expression");
    Check((harness.last.attributes & FGB_ATTR_LEFT_OPENNESS) != 0, "帧属性：左眼 openness");
    Check((harness.last.attributes & FGB_ATTR_LEFT_GAZE) != 0, "帧属性：左眼方向");

    const int32_t jaw = IndexOf(harness, "JawOpen");
    Check(jaw >= 0 && harness.last.shape_valid[jaw] != 0, "JawOpen 有效");
    if (jaw >= 0) {
        Check(std::fabs(harness.last.shapes[jaw] - 0.5f) < 1e-4f, "JawOpen = 0.5");
    }
    Check(std::fabs(harness.last.left_eye.openness - 0.25f) < 1e-4f, "左眼 openness = 0.25");
    Check(harness.last.left_eye.gaze_dir[0] > 0.0f, "左眼 gaze x > 0");

    module->teardown(self);
    WSACleanup();

    if (g_failures == 0) {
        std::printf("端到端冒烟通过\n");
        return 0;
    }
    std::printf("%d 项失败\n", g_failures);
    return 1;
}
