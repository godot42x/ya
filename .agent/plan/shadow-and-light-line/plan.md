# Shadow & Light Line — 点光源/聚光灯/阴影（roadmap R2/R3/R15）

2026-10-11 立项（承接 showcase-roadmap §3 R 组）。目标：G3.9 关卡光照所需的多光源与阴影能力，并把 PerfLab 的 Lights 系列压测从"被上限截断"变成真实曲线。

## 边界
- 延迟管线为灯光主战场；forward 保持 6 灯小环（带宽考虑）。
- 聚光灯（R15b）未开工：`PointLightData` 的 spot 字段是遗留通道，组件/着色/阴影都还没有。

## 已拍板（2026-10-11，用户方向）
1. **灯光数据不定长上限**：调研 Unity URP（定长 UBO，移动 8/桌面 16-32 变体）、Godot Forward+（SSBO 动态 + cluster）、UE 延迟（逐灯 draw）后，用户拍板 deferred 点光数据走 **SSBO 动态数组**（无上限、零预留）；未来可演进为 UE 式逐灯 draw 或 Forward+ cluster 管线（cluster 归 R2 线）。
2. **阴影预算与灯光上限解耦**：`maxShadowedPointLights`（settings 旋钮，现 ≤6）决定谁投影；超预算灯 `farPlane=0`，shader 短路不采样。

## Phase
- **R15a SSBO 灯光数据（已落地，见 progress）**：LightPass 点光数据 UBO→SSBO；快照/frameData 灯列表动态化；forward 加 6 灯 cap。
- **R15b 聚光灯**：SpotLightComponent（或 Point 扩展）+ LightPass 锥角着色 + serializer/inspector/脚本通道。
- **R2 点光源阴影 GPU 剔除 + indirect 修稳默认开**（含历史回归复测；开工前需与用户过方案问答）。
- **R15c 聚光灯阴影贴图**（排在 R2 之后；**G3.9 依赖它**——关卡展示需要带阴影的聚光灯）：聚光灯 2D 阴影图（view/projection 矩阵进 GPU 元素）或复用 cube 通道，与 R2 的剔除结构一起定。
- **R3 CSM 多级修稳默认开**（可与 R2 同一条阴影技术线）。
- 后续演进选项（用户提出，未排期）：UE 式逐灯 draw；Forward+ cluster 管线。

## 验收
- Lights_200/500 灯光数不再截断（LightPass GPU 时间随灯数真实增长）。
- R2/R3 落地时：同基准命令前后数字（执行规则 3）+ 历史回归场景复测。
