# FGB PicoFaceModule

[![build](https://github.com/Aqamoe/FGB-PicoFaceModule/actions/workflows/build.yml/badge.svg)](https://github.com/Aqamoe/FGB-PicoFaceModule/actions/workflows/build.yml)

[FaceGaze Bridge](https://github.com/Aqamoe/FaceGaze-Bridge) 的 PICO 面捕原生模块。
直接接收 **PICO 互联（PICO Connect）** 发出的 UDP 面捕数据，输出 Unified Expressions 表情、
睁眼度与逐眼方向。不需要 VRCFT，不注入任何进程，也不需要管理员权限。

> 状态：0.1.0。协议解析、映射层与 ABI 生命周期已有单元测试和合成 UDP 端到端冒烟；
> 真机（PICO 4 Pro / Enterprise）验收待补，见「已知限制」。

## 功能

- **表情**：52 个 ARKit 系数 → 58 个 Unified Expressions 形状（眉、眼周、下颌、脸颊、唇形、舌）。
- **眼睛**：`openness = 1 - EyeBlink`；方向由 `EyeLookIn/Out/Up/Down` 合成，正前方 `(0,0,-1)` 是合法结果。
- **两种传输协议都支持**：
  - `faceTrackingTransferProtocol = 2` → UDP **29765**，16 字节头，`trackingType == 2` 才是面部数据；
  - `faceTrackingTransferProtocol = 1` → UDP **29763**，VRCFT 1.3 之前的裸结构。
- **断连可恢复**：10 秒没有数据时在模块卡片提示原因，数据恢复后自动回到活跃。
- **可诊断**：端口冲突、包长异常、非面部 `trackingType` 都有明确日志，不静默失败。

## 不提供

| 数据 | 原因 |
|---|---|
| 瞳孔直径 | 面捕包没有该字段 |
| 2D 注视点 | 面捕包没有；逐眼 2D 点请用 PICO gaze 模块 |
| 头姿 | 面捕包没有，由其它头姿源提供 |
| 精确 3D 眼球方向 | 方向来自表情系数，置信度标 0.6；适合头像眼睛，不用于 foveation |

需要逐眼 2D 注视点时，可以把本模块和 gaze 模块组合：`expression` / `eyes` 角色给本模块，
`gaze` 角色给 gaze 模块。

## 环境要求

| 项 | 要求 |
|---|---|
| 系统 | Windows 10 / 11 x64 |
| 宿主 | FaceGaze Bridge，需支持原生模块 ABI v2 |
| 头显 | PICO 4 Pro / PICO 4 Enterprise，头显内已开启面部追踪/眼动并完成校准 |
| 串流 | PICO 互联已连接（USB 或局域网均可） |
| 其它 | 关闭 VRCFaceTracking：它和本模块抢同一组 UDP 端口 |

## 安装

1. 从 Releases 下载 `pico-connect-face-*.zip`，或按「从源码构建」自行打包。
2. FaceGaze Bridge →「模块库 → 原生模块 → 安装原生模块 (.zip)」。
3. 「模块」页把 **PICO Connect Face** 设为启动，按需分配角色（通常是 `expression` 与 `eyes`）。
4. 重启引擎；连上头显后模块卡片应显示活跃。

### 配置 PICO 互联

编辑 `%APPDATA%\PICO Connect\settings.json`：

```json
{
  "faceTrackingMode": 1,
  "faceTrackingTransferProtocol": 2
}
```

改完必须重启 PICO 互联。该文件有时会被客户端改回去，串流异常时先复查这两项。
`faceTrackingTransferProtocol` 填 1 或 2 都可以：1 走 29763 旧协议，2 走 29765 新协议。

## 从源码构建

需要 Visual Studio 2022（x64）与 CMake ≥ 3.24。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

产物在 `build/Release/`：`pico-connect-face.dll`、`fgb-native.json`、`icon.svg`。

打包成可安装的 zip：

```powershell
cmake --build build --config Release --target package-module
# → build/Release/pico-connect-face-<version>.zip
```

## 测试

```powershell
ctest --test-dir build -C Release --output-on-failure
```

- `pico-face-protocol-tests`：新旧协议解析、长度与 `trackingType` 校验、NaN 清洗。
- `pico-face-mapping-tests`：形状映射、眼睛方向、数值夹取、`shape_count` / `struct_size` 兼容。

端到端冒烟（会绑定 29765/29763，先关闭 VRCFT）：

```powershell
cmake --build build --config Release --target smoke-test
```

它按宿主的调用顺序加载 DLL，用合成 UDP 包驱动 `update()`，并检查提交帧的属性、
形状与眼睛数据。

## 目录结构

```
src/main.cpp               UDP 接收线程、生命周期、日志与状态上报
src/pico_face_protocol.*   PICO 面捕包解析
src/pico_face_mapping.*    PICO 系数 → Unified Expressions / 眼睛
include/fgb/               FaceGaze Bridge ABI 头（Apache-2.0）
module/                    fgb-native.json 与图标
tests/                     协议单测、映射单测、ABI 冒烟
```

## 协议要点

**新协议（UDP 29765，`faceTrackingTransferProtocol = 2`）**

| 偏移 | 长度 | 字段 |
|---|---|---|
| 0 | 2 | 起始码 |
| 2 | 1 | `trackingType`（2 = 面部） |
| 3 | 1 | `subType` |
| 4 | 1 | `multiPack` |
| 5 | 1 | `currentIndex` |
| 6 | 2 | `version` |
| 8 | 8 | 时间戳 |
| 16 | 892 | 体：8 字节时间戳 + 72 个 float 系数 + `videoInputValid[10]` + `laughingProb` + `emotionProb[10]` + `reserved[128]` |

协议 2 实测单包 **912 字节**：前 908 字节有效，尾部 4 字节本模块不解析。

**旧协议（UDP 29763，`faceTrackingTransferProtocol = 1`）**：没有头，从偏移 0 开始是同样的
892 字节体，也没有 `trackingType`。模块按接收端口区分协议。

## 排错

| 现象 | 处理 |
|---|---|
| 卡片显示「无法绑定 UDP 29765：端口已被占用」 | 关闭 VRCFaceTracking（或另一个接收端）后重启引擎 |
| 10 秒后提示未收到数据 | 检查 PICO 互联连接、头显面捕开关与 `settings.json` |
| 表情不动、眼睛正常 | 角色没分给 `expression`，或头显只开了眼动 |
| 状态页没有「2D 点」 | 正常：面捕包不包含 2D 注视点 |
| 日志出现「忽略非面部数据」 | 29765 上收到了 `trackingType != 2` 的包，通常是其它追踪数据 |

## 已知限制

- 真机验收待补：目前只在合成 UDP 包上做过端到端验证。
- 逐眼方向由表情系数推导，仅适合头像眼睛；渲染注视请使用真正的 GAZE 源。
- 方向符号与幅度按 VRCFT PICO 模块的行为实现，尚未在真机逐项比对。
- 仅支持 Windows x64。

## 致谢

- UDP 包格式与表情映射规则参考 [VRCFTPicoModule](https://github.com/lonelyicer/VRCFTPicoModule)；
  上游未声明许可证，本仓库是独立的 C++ 实现。
- ABI 头与宿主：[FaceGaze Bridge](https://github.com/Aqamoe/FaceGaze-Bridge)。

## 许可

Apache License 2.0，见 [LICENSE](LICENSE) 与 [NOTICE](NOTICE)。
