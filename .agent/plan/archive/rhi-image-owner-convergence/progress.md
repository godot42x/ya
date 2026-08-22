# RHI 图像 owner 收口进度记录

> 建立日期：2026-08-16
> 作用：记录每轮完成内容、验证证据、遗留风险与下一步接力点。

## 2026-08-16 — 计划建档与当前基线落盘

### 本轮完成

- 建立 `rhi-image-owner-convergence/` 计划目录；
- 明确本轮正式主链路：`IImage/IImageView -> ImageResource -> Texture | RenderTexture -> RenderGraph / RenderRuntime consumer`；
- 记录当前代码真实状态：`ImageResource` 已落地，`Texture` 已 owner-first，`RenderImage` 仍大量存在于 graph/runtime。

### 当前结论

- 不能假装当前仓库已经完成 `ImageResource` 收口；
- 下一刀应先补 `RenderTexture` 类型本体，再切 graph/runtime 公共面；
- `RenderGraphExecutionResult -> RenderTexture` 是正确方向，但应建立在正式类型已经落地之上。

### 下一轮直接接力点

1. 落 `RenderTexture` 类型与公开头；
2. 给 graph import helper 增加 `RenderTexture` owner-first 入口；
3. 跑 `ya-rhi` / `ya-render-graph` 构建验证；
4. 再开始切 exported texture 与 runtime-facing 输出。

## 2026-08-16 — Phase 1 完成，进入 Graph owner 收口

### 本轮完成

- 新增 `RenderTexture` 正式类型与公开头；
- `RenderTexture` 已提供 `create / wrap / adopt` 入口，并统一透出 owner 访问面：`resource / image / defaultView / extent / format / retainedResources`；
- `RenderGraphImportUtils` 新增 `RenderTexture` owner-first import overload；
- 保持现有 `RenderImage` 旧调用点可继续工作，没有再新增基于 `RenderImage` 的公共新能力。

### 验证证据

- `xmake b ya-rhi`
- `xmake b ya-render-graph`

### 当前结论

- `RenderTexture` 本体已经足够作为 graph/runtime exported texture 的正式公开语义；
- 下一刀不该再补 owner facade，而应该直接切 `RenderGraphExecutionResult` 与 registry 的 exported texture owner；
- 只有等 graph exported texture 收口后，再继续收 runtime-facing output，才不会把 `RenderImage` 继续扩散到 pipeline 接口。

### 下一轮直接接力点

1. `RenderGraphExecutionResult` exported texture 改为 `std::shared_ptr<RenderTexture>`；
2. `RenderGraphResourceRegistry` texture entry / resolve 接口同步切到 `RenderTexture`；
3. 调整 `PostProcessingStage`、Bloom、forward/deferred exported output 的直接消费方；
4. 跑 `xmake b ya-render-3d`，再决定是否顺势推进 runtime-facing 接口。

## 2026-08-16 — Phase 2 完成，并推进 Runtime / GUI owner 收口

### 本轮完成

- `RenderGraphExecutionResult` exported texture 已切到 `std::shared_ptr<RenderTexture>`；
- `RenderGraphResourceRegistry` 的 texture entry / resolve / retained-resource 路径已切到 `RenderTexture` owner 语义；
- `RenderRuntime` runtime-facing 输出已切到 `std::shared_ptr<RenderTexture>`，包括 postprocess / active viewport / viewport display / presentation image；
- GUI compose 与 host target 也已同步切到 `RenderTexture`：`Render2DComposePass`、`GUIRenderSurface`、`GUIPresentationTarget`、`GUIAppHost` 已不再把 `RenderImage` 作为公开 target 语义。

### 验证证据

- `xmake b ya-render-3d`

### 当前结论

- graph exported texture 与 runtime-facing output 的 owner 收口已经打通；
- 现在剩余的主要泄漏点不在 graph/runtime 主链，而在 GameRuntime / Editor 等 consumer 侧仍把截图、automation、presentation capture 建立在 `RenderImage` 上；
- 下一刀应该先迁 GameRuntime automation/screenshot 链路，确保 runtime app 方向对外只理解 `RenderTexture` owner，而不是继续暴露 backend image owner 细节。

### 下一轮直接接力点

1. 迁 `GameRuntime` 的 automation frame context 到 `RenderTexture`；
2. 迁 `AppScreenshotCapture` 与 `AppAutomationControlService` 到 owner-first 资源语义；
3. 跑 `xmake b ya-game-runtime`；
4. 再审 editor 侧残留 `RenderImage` consumer。

## 2026-08-16 — Phase 3 完成，Runtime / Editor consumer 已切到 RenderTexture

### 本轮完成

- `GameRuntime` automation frame context、automation control service、screenshot capture 链路已切到 `std::shared_ptr<RenderTexture>`；
- `GameEditor` 的 viewport display、entity-id pick、GUI workbench display、editor compose target 已切到 `RenderTexture` owner 语义；
- editor/runtime 两条应用分支都已重新接到同一条 owner-first 主链：`ImageResource -> RenderTexture -> consumer`。

### 验证证据

- `xmake b ya-game-runtime`
- `xmake b ya-game-editor`

### 当前结论

- `RenderImage` 已不再作为 Runtime App / Editor App 的公开图像 owner 语义；
- 现存残留主要退回到更底层或更内部的实现面：`OffscreenJob`、`FrameBuffer`、environment lighting / postprocess pipeline 内部 helper，以及少量 import utils / compat helper；
- 下一阶段不该再在应用层继续追 `RenderImage`，而是要按模块边界逐块消掉这些基础设施残留。

### 下一轮直接接力点

1. 先切 `RenderSharedResourceProvider` / `EnvironmentLightingProcessor` 这些仍向外暴露 `RenderImage` 的 framework public header；
2. 再评估 `OffscreenJob` / `FrameBuffer` 是否一起切到 `RenderTexture`，还是收口成纯内部执行语义；
3. 最后删除 `ImageResourceRef` 与 `RenderImage` 过渡桥。

## 2026-08-17 — Phase 4 前置收口推进，Environment Lighting / Shared Resource 已转 owner-first

### 本轮完成

- `RenderSharedResourceProvider` 的 BRDF LUT owner 已切到 `std::shared_ptr<RenderTexture>`；
- `PBRGenerateBrdfLUT`、`ImageResourceRef`、`EnvironmentLightingProcessor`、`EnvironmentLightingDetail` 的公开 owner 字段/入口已改为 `RenderTexture`；
- environment-lighting 场景/预览/runtime state 不再经 public header 暴露 `std::shared_ptr<RenderImage>`；
- `CubeMap2PBRIrradianceMap` / `CubeMap2PBRPrefilteredEnv` 的输入 owner 已切到 `RenderTexture*`；
- 现阶段保留 offscreen job 内部 `RenderImage` 执行语义，但在 framework 对外边界通过 `RenderTexture::adopt(...)` 回到 owner-first 主链。

### 验证证据

- `xmake b ya-render-3d`
- `xmake b ya-game-editor`

### 当前结论

- framework public header 中最大的 owner 泄漏点已经从 environment-lighting / shared-resource 一侧收回；
- 剩余更值得继续清理的是 postprocess / bloom / ssao 这类仍直接公开 `RenderImage*` 参数或 include 的模块；
- `OffscreenJob` / `FrameBuffer` 仍应按“内部执行语义 vs 对外 owner 语义”拆开评估，而不是直接做一轮全量替换。

### 下一轮直接接力点

1. 收 `PostProcessingStage` / `BloomPostprocessing` 的 owner-facing 参数到 `RenderTexture`；
2. 清掉 `SSAOStage` 这类历史 include / helper 残留；
3. 重新审计 public headers 中剩余 `RenderImage` 命中，再决定 `FrameBuffer` / `OffscreenJob` 的归属和删桥顺序。

## 2026-08-17 — RenderImage 类型删除，全部收敛到 ImageResource

### 本轮完成

- 删除 `RHI/Core/RenderImage.h/.cpp` 与 `RHI/include/RHI/Core/RenderImage.h` 转发头；
- `FrameBuffer` 的 color/depth/resolve attachment 已从 `RenderImage` 收敛到 `ImageResource`；
- `OffscreenJob` 的 `outputImage` / `CreateOutputFn` / `ExecuteFn` 已收敛到 `ImageResource`；
- `EquidistantCylindrical2CubeMap` / `CubeMap2PBRIrradianceMap` / `CubeMap2PBRPrefilteredEnv` 的 output 已收敛到 `ImageResource*`；
- `EnvironmentLightingProcessor` 的离屏桥接 `adoptRenderTexture` 与 `createRenderableSkyboxResource` 已收敛到 `ImageResource`；
- `PostProcessingStage` / `BloomPostprocessing` 的 owner-facing 参数已收敛到 `RenderTexture*`，并清掉 `SSAOStage` 历史 include/helper 残留；
- 清理死代码 `getRenderImageLabel` / `retainRenderImageResources`，以及 `LightStage` / `RenderingInfoUtils` / `RenderGraphImportUtils` 的残留 include/forward-declare；
- 重命名命名残留：`createPresentationRenderTexture`、`createRenderableSkyboxResource`，并修正 `Image.h` 注释。

### 验证证据

- `xmake b ya-render-3d`
- `xmake b ya-game-editor`

### 当前结论

- `RenderImage` 作为纯空壳 owner 类型已从仓库删除，底层 owner 只有 `ImageResource`；
- 上层语义对象保留 `Texture`（资产采样）与 `RenderTexture`（运行时可写）；
- 剩余唯一过渡桥是 `ImageResourceRef`（RenderTexture | Texture 二选一），删除它需要把 EnvironmentLighting source-cubemap 传递路径统一到 `shared_ptr<ImageResource>`。

### 下一轮直接接力点

1. 删除 `ImageResourceRef`：`resolveSceneSkyboxResource` 与 environment source-cubemap 传递统一到 `shared_ptr<ImageResource>`；
2. 同步 `RenderGraphImportUtils` / `DeferredRenderPipeline` / `DeferredFrameGraphPasses` 的 import helper 到 `shared_ptr<ImageResource>`；
3. 删除 `ImageResourceRef.h/.cpp` 与转发头后，做一次 public-header 全量审计收尾。

## 2026-08-17 — ImageResourceRef 删除，Environment Lighting 统一到 ImageResource owner

### 本轮完成

- 删除 `RHI/Core/ImageResourceRef.h` 与 `RHI/include/RHI/Core/ImageResourceRef.h` 转发头；
- `EnvironmentLightingResultProvider::resolveSceneSkyboxResource` 返回类型改为 `std::shared_ptr<ImageResource>`；
- `EnvironmentLightingSceneResources` 的 cubemap/irradiance/prefilter 由 `ImageResourceRef` 改为 `std::shared_ptr<ImageResource>`；
- 新增 `detail::ownerResourceOf(renderImage, texture)`，把 RenderTexture/Texture 二选一统一为底层 `ImageResource`；
- `CubeMap2PBRIrradianceMap` / `CubeMap2PBRPrefilteredEnv` 的 input 从 inputImage+inputTexture 双参数收敛为单个 `ImageResource* input`；
- `RenderGraphImportUtils` / `DeferredRenderPipeline` / `DeferredFrameGraphPasses` 的 import helper 已改为直接接受 `std::shared_ptr<ImageResource>`；
- `RenderSharedResourceProvider` / `AppLifecycle` 的消费点同步到 owner-first 语义，并补齐 `getImageResourceLabel`。

### 验证证据

- `xmake b ya-render-3d`
- `xmake b ya-game-editor`

### 当前结论

- `ImageResourceRef`（RenderTexture | Texture 二选一桥）已删除；environment-lighting 的 source/resource 传递统一为 `shared_ptr<ImageResource>`；
- `RenderImage` 与 `ImageResourceRef` 两个过渡桥均已清除，公开头不再出现旧 owner 语义；
- 图像资源链路现在收敛为：`IImage/IImageView -> ImageResource -> Texture | RenderTexture -> consumer`。

### 收尾状态

- Phase 4 两个删除目标（`RenderImage`、`ImageResourceRef`）均完成；
- public-header 静态审计通过：`RenderImage` 类型与 `ImageResourceRef` 均为 0 命中；
- 后续若还有清理，只会在 backend/RHI primitive 内部（`IImage` / `IImageView`）做局部收敛，不再属于本计划的 owner 收口范围。
