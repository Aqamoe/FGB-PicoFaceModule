# 真机验收清单

> 0.1.0 起记录：协议解析与映射有单测、并有走真实 ABI 的合成 UDP 冒烟；
> 下表为 **真机（PICO 4 Pro / Enterprise）** 验收项，仍未逐项完成。

| 项 | 值 |
|---|---|
| 头显 | PICO 4 Pro / Enterprise（记录固件版本） |
| 串流 | PICO 互联（记录版本） |
| 宿主 | FaceGaze Bridge（记录版本） |
| 模块 | Release tag |

- [ ] `%APPDATA%\PICO Connect\settings.json`：`faceTrackingMode = 1`、`faceTrackingTransferProtocol = 2`
- [ ] 关闭 VRCFT（29765/29763 冲突）
- [ ] 安装 zip → 设为启动 → 重启引擎 → 连接头显后卡片显示活跃
- [ ] 眨眼 / 张嘴 / 微笑 / 皱眉 逐项对照 UR 参数
- [ ] 左右眼方向：看左、看右、看上、看下，确认没有反轴
- [ ] 断开串流 10 秒内卡片提示断流；重连后自动恢复
- [ ] 把 `faceTrackingTransferProtocol` 改成 1（旧协议 29763）重复上面的检查
