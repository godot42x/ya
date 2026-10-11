# Progress

## 2026-10-11 — R15a：deferred 点光数据 SSBO 化（无上限）

- 调研先行（用户要求）：Unity URP 定长数组 / Godot Forward+ SSBO+cluster / UE 逐灯 draw 三方案对比，用户拍板 SSBO 动态数组；撤销了先做的"定长 32"接线。
- 改动：`LightPass.slang` 的 `LightData` 去掉 `pointLights[]`（UBO 352B），新增 `[[vk::binding(2,0)]] StructuredBuffer<PointLight> uPointLights`（元素 36B，布局以生成头为准）；set-0 布局加 StorageBuffer binding；`ViewPayloads` 携带 span，`writeUploadSlice` 按实际上传（0 灯时绑一个全零元素——shader 静态访问要求绑定存在）；快照与 `RenderFrameData` 的灯列表从 `std::array<,6>` 改 `std::vector`（收集无上限）；forward 两条拷贝环加 `min(numPointLights, MAX_POINT_LIGHTS)` cap（forward shader 环不变）。
- 阴影不动：预算机制（`maxShadowedPointLights`，超预算 `farPlane=0` 短路）天然隔离，cube 数组/阴影资源维持 ≤6。
- 验证：`ya-testing` 全量过（2 例预存内容失败除外）；Lights_500 实跑 `Deferred Light` GPU p50 30.31ms、frame p50 34.96ms（6 灯时代 frame ~9ms）——500 盏真实着色，GPU 主导，cluster（R2）的动机被这组数字直接证实。
- 生成文件经 `xmake ya-shader` 重生成（LightPass.slang.h：LightData 352B + PointLight 36B）。
