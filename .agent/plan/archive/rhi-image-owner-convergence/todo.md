# RHI 图像 owner 收口 TODO

> 更新时间：2026-08-17

## 当前激活切片

Phase 4 前置收口 — 清理 framework public header 中剩余 `RenderImage` 泄漏

## Phase 1 — RenderTexture 建模

### 已完成

- [x] 新增 `RHI/Core/RenderTexture.h/.cpp`
- [x] 新增 `RHI/include/RHI/Core/RenderTexture.h` 转发头
- [x] 定义 `RenderTexture` 的 create / wrap / adopt-owner 入口
- [x] 统一 owner 访问面：resource / image / defaultView / extent / format / retainedResources
- [x] 给 `RenderGraphImportUtils` 增加 `RenderTexture` import overload
- [x] 跑 `xmake b ya-rhi`
- [x] 跑 `xmake b ya-render-graph`

### 完成标准

- [x] `RenderTexture` 已成为正式公开类型
- [x] graph 能 import `RenderTexture`
- [x] 本轮没有新增基于 `RenderImage` 的公共接口

## Phase 2 — Graph exported texture 收口

- [x] `RenderGraphExecutionResult` 改为导出 `std::shared_ptr<RenderTexture>`
- [x] `RenderGraphResourceRegistry` texture entry / resolve 接口切到 `RenderTexture`
- [x] 调整 `PostProcessingStage` / `BloomPostprocessing` / forward/deferred exported output 消费方
- [x] 跑 `xmake b ya-render-3d`

## Phase 3 — Runtime-facing 输出收口

- [x] `IRenderPipeline` exported runtime texture 语义切到 `RenderTexture`
- [x] `RenderRuntime` / `AppRenderServices` / `GameRuntime` 自动化截图链路同步
- [x] 跑 `xmake b ya-game-runtime`
- [x] 如需要，跑 `xmake b ya-game-editor`

## 当前待迁 consumer

- [x] `Applications/GameRuntime/Lifecycle/AppAutomation.h`
- [x] `Applications/GameRuntime/Automation/AppAutomationControlService.*`
- [x] `Applications/GameRuntime/Utility/AppScreenshotCapture.*`
- [x] `Applications/GameRuntime/Lifecycle/GameRuntimeFrameOrchestrator.cpp`

## 下一激活切片

Phase 4 前置收口 — 清理 framework public header 中剩余 `RenderImage` 泄漏

- [x] `RenderSharedResourceProvider.*`
- [x] `EnvironmentLightingProcessor.* / EnvironmentLightingDetail.h`
- [x] `PostProcessingStage.* / Bloom / SSAO / PBR LUT`
- [x] 评估 `OffscreenJob.h` 与 `FrameBuffer.h` 的 owner 语义归属（统一到 `ImageResource`）

## Phase 4 — 删除过渡桥

- [x] 删除 `RenderImage`
- [x] 删除 `ImageResourceRef`
- [x] 审计 public headers，确认不再泄漏旧语义
