// PICO Connect 面捕 → FaceGaze Bridge 原生模块。
//
// 直接监听 PICO 互联发出的 UDP 面捕包（29765 新协议 / 29763 旧协议），
// 在 update() 里把 52 个 ARKit 系数映射到 Unified Expressions，
// 同时给出睁眼度与逐眼方向。不需要 VRCFT，也不需要注入任何进程。

#include "fgb_module_api.h"
#include "pico_face_mapping.hpp"
#include "pico_face_protocol.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr std::uint64_t kNoDataTimeoutNs = 10ull * 1000ull * 1000ull * 1000ull;

std::uint64_t NowNs() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

// ---- Winsock 生命周期 ----

struct WinsockState {
    std::mutex mutex;
    int refs = 0;
};

WinsockState& Winsock() {
    static WinsockState state;
    return state;
}

bool AcquireWinsock(std::string& error) {
    auto& state = Winsock();
    std::lock_guard lock(state.mutex);
    if (state.refs == 0) {
        WSADATA data{};
        const int result = WSAStartup(MAKEWORD(2, 2), &data);
        if (result != 0) {
            error = "WSAStartup 失败，错误码 " + std::to_string(result);
            return false;
        }
    }
    ++state.refs;
    return true;
}

void ReleaseWinsock() {
    auto& state = Winsock();
    std::lock_guard lock(state.mutex);
    if (state.refs > 0 && --state.refs == 0) {
        WSACleanup();
    }
}

bool BindUdp(std::uint16_t port, SOCKET& out, std::string& error) {
    const SOCKET handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle == INVALID_SOCKET) {
        error = "socket() 失败，WSA " + std::to_string(WSAGetLastError());
        return false;
    }

    int receive_buffer = 512 * 1024;
    ::setsockopt(handle, SOL_SOCKET, SO_RCVBUF,
                 reinterpret_cast<const char*>(&receive_buffer), sizeof receive_buffer);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(handle, reinterpret_cast<const sockaddr*>(&address), sizeof address) ==
        SOCKET_ERROR) {
        const int code = WSAGetLastError();
        ::closesocket(handle);
        error = "无法绑定 UDP " + std::to_string(port);
        if (code == WSAEADDRINUSE) {
            error += "：端口已被占用，请关闭 VRCFT 或另一个 PICO 接收端";
        } else if (code == WSAEACCES) {
            error += "：端口访问被拒绝（可能被独占或安全软件拦截）";
        } else {
            error += "，WSA " + std::to_string(code);
        }
        return false;
    }

    out = handle;
    return true;
}

struct RxSocket {
    SOCKET handle = INVALID_SOCKET;
    std::uint16_t port = 0;
};

struct Instance {
    const FgbHostServices* host = nullptr;

    std::array<RxSocket, 2> sockets = {{{INVALID_SOCKET, pico_face::kPortFaceV2},
                                        {INVALID_SOCKET, pico_face::kPortFaceLegacy}}};
    std::thread rx_thread;
    std::atomic<bool> stop{false};
    bool winsock_acquired = false;

    std::mutex frame_mutex;
    pico_face::Weights latest{};
    bool has_new = false;
    std::atomic<std::uint64_t> last_rx_ns{0};
    std::atomic<std::uint32_t> rejected{0};

    std::mutex log_mutex;
    std::vector<std::pair<int, std::string>> pending_logs;

    std::uint64_t start_ns = 0;
    bool active_reported = false;
    bool timeout_reported = false;

    bool reported_v2 = false;
    bool reported_legacy = false;
    bool select_error_reported = false;

    float smooth_mouth_left = 0.0f;
    float smooth_mouth_right = 0.0f;

    pico_face::TargetSet targets;

    void QueueLog(int level, std::string text);
    void FlushLogs();
    void RxLoop();
    void HandleDatagram(const std::uint8_t* data, std::size_t size, std::uint16_t port);
    void CloseSockets();
};

void Instance::QueueLog(int level, std::string text) {
    std::lock_guard lock(log_mutex);
    if (pending_logs.size() < 32) {
        pending_logs.emplace_back(level, std::move(text));
    }
}

void Instance::FlushLogs() {
    std::vector<std::pair<int, std::string>> logs;
    {
        std::lock_guard lock(log_mutex);
        logs.swap(pending_logs);
    }
    for (const auto& entry : logs) {
        host->log(host->ctx, entry.first, entry.second.c_str());
    }
}

void Instance::HandleDatagram(const std::uint8_t* data, std::size_t size, std::uint16_t port) {
    pico_face::Packet packet;
    const bool parsed = port == pico_face::kPortFaceV2
                            ? pico_face::ParseNewPacket(data, size, packet)
                            : pico_face::ParseLegacyPacket(data, size, packet);
    if (!parsed) {
        const std::uint32_t count = rejected.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count == 1 || count == 25 || count == 250) {
            char message[224];
            if (port == pico_face::kPortFaceV2 && size >= 3) {
                std::snprintf(message, sizeof message,
                              "忽略 UDP %u 上的非面部数据（累计 %u 个，长度 %zu，trackingType=%u）",
                              static_cast<unsigned>(port), count, size,
                              static_cast<unsigned>(data[2]));
            } else {
                std::snprintf(message, sizeof message,
                              "忽略 UDP %u 上的异常数据（累计 %u 个，长度 %zu）",
                              static_cast<unsigned>(port), count, size);
            }
            QueueLog(FGB_LOG_WARN, message);
        }
        return;
    }

    {
        std::lock_guard lock(frame_mutex);
        latest = packet.weights;
        has_new = true;
    }
    last_rx_ns.store(NowNs(), std::memory_order_relaxed);

    if (packet.protocol == pico_face::Protocol::FaceV2 && !reported_v2) {
        reported_v2 = true;
        char message[224];
        std::snprintf(message, sizeof message,
                      "已收到 PICO 面捕数据：UDP %u（协议 2），包长 %zu 字节，version=%u，"
                      "起始码 0x%02X 0x%02X",
                      static_cast<unsigned>(port), size,
                      static_cast<unsigned>(packet.version),
                      static_cast<unsigned>(packet.start_code1),
                      static_cast<unsigned>(packet.start_code2));
        QueueLog(FGB_LOG_INFO, message);
    } else if (packet.protocol == pico_face::Protocol::Legacy && !reported_legacy) {
        reported_legacy = true;
        char message[160];
        std::snprintf(message, sizeof message,
                      "已收到 PICO 面捕数据：UDP %u（旧协议），包长 %zu 字节",
                      static_cast<unsigned>(port), size);
        QueueLog(FGB_LOG_INFO, message);
    }
}

void Instance::RxLoop() {
    std::array<std::uint8_t, 2048> buffer{};
    while (!stop.load(std::memory_order_relaxed)) {
        fd_set readable;
        FD_ZERO(&readable);
        SOCKET highest = 0;
        bool any = false;
        for (const auto& socket : sockets) {
            if (socket.handle == INVALID_SOCKET) {
                continue;
            }
            FD_SET(socket.handle, &readable);
            if (socket.handle > highest) {
                highest = socket.handle;
            }
            any = true;
        }
        if (!any) {
            return;
        }

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 100 * 1000;
        const int ready =
            ::select(static_cast<int>(highest) + 1, &readable, nullptr, nullptr, &timeout);
        if (ready == SOCKET_ERROR) {
            if (!select_error_reported) {
                select_error_reported = true;
                QueueLog(FGB_LOG_WARN, "select() 失败，WSA " + std::to_string(WSAGetLastError()));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        select_error_reported = false;
        if (ready == 0) {
            continue;
        }

        for (const auto& socket : sockets) {
            if (socket.handle == INVALID_SOCKET || FD_ISSET(socket.handle, &readable) == 0) {
                continue;
            }
            const int received =
                ::recv(socket.handle, reinterpret_cast<char*>(buffer.data()),
                       static_cast<int>(buffer.size()), 0);
            if (received <= 0) {
                continue;
            }
            HandleDatagram(buffer.data(), static_cast<std::size_t>(received), socket.port);
        }
    }
}

void Instance::CloseSockets() {
    for (auto& socket : sockets) {
        if (socket.handle != INVALID_SOCKET) {
            ::closesocket(socket.handle);
            socket.handle = INVALID_SOCKET;
        }
    }
}

int initialize(void** out_self, const FgbHostServices* host, const FgbInitParams* params) {
    (void)params;
    if (out_self == nullptr || host == nullptr || host->ctx == nullptr) {
        return 0;
    }

    auto* self = new (std::nothrow) Instance();
    if (self == nullptr) {
        return 0;
    }
    self->host = host;

    std::string error;
    if (!AcquireWinsock(error)) {
        host->report_status(host->ctx, FGB_STATUS_ERROR, error.c_str());
        delete self;
        return 0;
    }
    self->winsock_acquired = true;

    int bound = 0;
    std::string bind_failures;
    for (auto& socket : self->sockets) {
        SOCKET handle = INVALID_SOCKET;
        std::string socket_error;
        if (BindUdp(socket.port, handle, socket_error)) {
            socket.handle = handle;
            ++bound;
        } else {
            if (!bind_failures.empty()) {
                bind_failures += "；";
            }
            bind_failures += socket_error;
        }
    }

    if (bound == 0) {
        host->report_status(host->ctx, FGB_STATUS_ERROR, bind_failures.c_str());
        self->CloseSockets();
        ReleaseWinsock();
        delete self;
        return 0;
    }
    if (!bind_failures.empty()) {
        host->log(host->ctx, FGB_LOG_WARN, bind_failures.c_str());
    }

    const std::size_t resolved = pico_face::ResolveTargets(*host, self->targets);
    if (resolved != pico_face::kTargetCount) {
        char message[160];
        std::snprintf(message, sizeof message, "宿主只解析到 %zu/%zu 个表情形状，其余将跳过",
                      resolved, pico_face::kTargetCount);
        host->log(host->ctx, FGB_LOG_WARN, message);
    }

    std::string ports;
    for (const auto& socket : self->sockets) {
        if (socket.handle == INVALID_SOCKET) {
            continue;
        }
        if (!ports.empty()) {
            ports += "/";
        }
        ports += std::to_string(socket.port);
    }
    const std::string waiting = "等待 PICO 面捕数据（UDP " + ports + "）";
    host->report_status(host->ctx, FGB_STATUS_STARTING, waiting.c_str());

    self->start_ns = NowNs();
    try {
        self->rx_thread = std::thread(&Instance::RxLoop, self);
    } catch (...) {
        host->report_status(host->ctx, FGB_STATUS_ERROR, "无法创建面捕接收线程");
        self->CloseSockets();
        ReleaseWinsock();
        delete self;
        return 0;
    }

    *out_self = self;
    return 1;
}

void update(void* opaque) {
    auto* self = static_cast<Instance*>(opaque);
    if (self == nullptr) {
        return;
    }
    self->FlushLogs();

    pico_face::Weights weights{};
    bool fresh = false;
    {
        std::lock_guard lock(self->frame_mutex);
        if (self->has_new) {
            weights = self->latest;
            self->has_new = false;
            fresh = true;
        }
    }

    if (fresh) {
        if (!self->active_reported || self->timeout_reported) {
            self->host->report_status(self->host->ctx, FGB_STATUS_ACTIVE, "");
            self->active_reported = true;
            self->timeout_reported = false;
        }
    } else if (!self->timeout_reported) {
        const std::uint64_t last = self->last_rx_ns.load(std::memory_order_relaxed);
        const std::uint64_t reference = last != 0 ? last : self->start_ns;
        if (NowNs() - reference > kNoDataTimeoutNs) {
            self->timeout_reported = true;
            self->host->report_status(
                self->host->ctx, FGB_STATUS_ERROR,
                "10 秒未收到 PICO 面捕数据：请确认 PICO 互联已连接、头显已开启面捕，"
                "且 settings.json 的 faceTrackingTransferProtocol 为 1 或 2");
        }
    }

    if (!fresh) {
        return;
    }

    FgbFrameSlot* slot = self->host->acquire_frame_slot(self->host->ctx);
    if (slot == nullptr) {
        return;
    }
    pico_face::WriteFrame(weights, self->targets, slot, self->smooth_mouth_left,
                          self->smooth_mouth_right);
    self->host->commit_frame_slot(self->host->ctx, slot, 0);
}

void teardown(void* opaque) {
    auto* self = static_cast<Instance*>(opaque);
    if (self == nullptr) {
        return;
    }
    self->stop.store(true, std::memory_order_relaxed);
    if (self->rx_thread.joinable()) {
        self->rx_thread.join();
    }
    self->CloseSockets();
    if (self->winsock_acquired) {
        ReleaseWinsock();
        self->winsock_acquired = false;
    }
    self->host->report_status(self->host->ctx, FGB_STATUS_STOPPED, "");
    delete self;
}

const FgbModule kModule = {
    sizeof(FgbModule),
    FGB_MODULE_ABI_V2,
    "com.picogaze.pico-connect-face",
    "PICO Connect Face",
    FGB_CAP_EXPRESSION | FGB_CAP_EYES,
    &initialize,
    &update,
    &teardown,
};

} // namespace

extern "C" FGB_MODULE_EXPORT const FgbModule* fgb_module_query_v2(
    std::uint32_t host_abi_version) {
    if (host_abi_version != FGB_MODULE_ABI_V2) {
        return nullptr;
    }
    return &kModule;
}
