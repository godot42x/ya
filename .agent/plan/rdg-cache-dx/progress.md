# 进度

## 2026-09-29 P0 落地：skinning 跨帧缓存

- 诊断：HelloMaterial 稳态每帧 1–3 个 DDQ 析构，调用栈定为
  `RenderGraphResourceRegistry::sync` → `retireRetainedResources`；label 定为
  Deferred / DirectionalShadow / PointShadow 三路 SkinningSSBO，同 desc、
  每帧新指针。根因：`RenderSubmissionPool::acquire` 每帧清空 `_families`，
  family 帧内有效，`prepareSceneFamilySkinning` 每帧重建 64KB buffer。
- 改动：新增 device 级 `SceneSkinningCache`（scene 键、flight 环、字节比对
  上传、DDQ 退役）；`prepareFrameRecord` 预录制解析进
  `RenderViewSceneResources::skinningBuffer`；三处 beginView 改从该通道取；
  family 删帧内 gpu 包与 snapshot 绑定；`prepareSceneFamilySkinning` 只做
  描述符集绑定。commit 533003be（代码加测试一次提交）。
- 验证：`ya-render-3d-test` 182 全过（含 9 个新增 cache 回归测试）；
  HelloMaterial 200 帧除 warmup 外零 flush；sol2 的 2 个 panic 经无改动对照
  确认为存量问题，与本次无关。
- 剩余：P1–P4 未开始。临时诊断代码已 revert，无残留。
