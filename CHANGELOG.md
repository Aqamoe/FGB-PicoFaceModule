# 更新日志

## [0.2.0]
### 变更
- 构建改为 **FGB-Module-Kit + sdk.lock.json** 模型：仓库不再保留 `include/fgb/fgb_module_api.h` 副本，
  ABI 头由锁文件指向的 FaceGaze Bridge Release 在配置阶段拉取并校验 SHA256。
- CI 换成 Kit 的复用工作流（构建 / 单测 / 真实 ABI 冒烟 / 打包 / 包内清单校验 / 发布前审计），
  并新增每周 SDK 自动升级 PR。
- 依赖与打包规则收敛到 Kit：产物名与内容不变（`pico-connect-face-<版本>.zip`）。

### 不变
- `ModuleId` 仍是 `com.picogaze.pico-connect-face`，DLL 名与包结构不变：已安装用户可直接收到升级。
- 协议解析（UDP 29765 新协议 / 29763 旧协议）与 52 ARKit → Unified Expressions 映射逻辑未改。

## [0.1.0]
首个版本：PICO 互联 UDP 面捕 → Unified Expressions 表情、睁眼度与逐眼方向。
