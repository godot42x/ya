# Stage I/O Inventory

这份清单服务于 `render-architecture-refactor` 的 `Phase 6`。

目标不是描述实现细节，而是回答三个 Render Graph 前置问题：

1. 每个 stage 在执行时真正读哪些 GPU 资源
2. 每个 stage 在执行时真正写哪些 GPU 资源
3. 每个 stage 还依赖哪些隐式 runtime / scene / service 输入

## 总结

当前 deferred 路线已经有比较清晰的 stage 分层，但仍存在两个主要问题：

- stage 输入还混有 `std::function` service 回调和少量全局查询
- 资源重建仍主要由 pipeline/stage 在运行时即时触发，不是声明式规格变化

当前 forward 路线的 `ForwardViewportStage` 仍是最不 graph-friendly 的部分，因为它把：

- skybox
- PBR
- Phong
- Unlit
- Simple
- Debug

都揉在一个大 stage 里。虽然它已经开始改成显式 service callback 注入，但执行期仍然混着 scene/service 查询和多类 draw pass。

## Stage Inventory

### ShadowStage

- 角色：
  阴影图生成 facade，内部委托给 `BasicShadowMapTechnique`
- 显式输入：
  - `RenderStageContext`
  - `ShadowSettings`，由 `applySettings()` 每帧注入
  - 外部提供的 shadow render target
- GPU 读取：
  - `RenderFrameData` 中的 directional / point light 和 draw items
  - 各 mesh / material / skinning 资源
- GPU 写入：
  - directional shadow depth
  - point shadow depth / cubemap faces
- 隐式依赖：
  - `BasicShadowMapTechnique` 内部资源和 pipeline 组织
- Graph 化判断：
  - 可以作为 `ShadowPassGroup`
  - 但内部最好继续拆成 directional / point / optional cull/indirect 子 pass

### GBufferStage

- 角色：
  写入 deferred GBuffer 和 depth
- 显式输入：
  - `RenderStageContext`
  - `ShadowRuntimeState`
- GPU 读取：
  - `RenderFrameData` draw buckets
  - material resource/param descriptor sets
  - skinning buffer
- GPU 写入：
  - GBuffer RT0..RT3
  - GBuffer depth
  - frame/light UBO
  - skinning SSBO
- 隐式依赖：
  - fallback `UnlitMaterial`
  - material factory / material pools
- Graph 化判断：
  - 可直接映射为 `GBufferPass`
  - `frame/light UBO` 与 `skinning SSBO` 更像 frame extraction 产物，后续应从 stage 内 ownership 分离

### SSAOStage

- 角色：
  从 GBuffer 生成 AO texture
- 显式输入：
  - `RenderStageContext`
  - GBuffer render target
  - AO output texture
- GPU 读取：
  - GBuffer color 0/1
  - GBuffer depth
  - SSAO noise texture
  - projection / inverse projection / view
- GPU 写入：
  - SSAO AO texture
- 隐式依赖：
  - config manager 持久化参数
  - default sampler / texture library
- Graph 化判断：
  - 可直接映射为单一 `SSAOPass`
  - 很适合变成“读 GBuffer，写 AO”的标准 graph node

### LightStage

- 角色：
  deferred fullscreen light accumulation
- 显式输入：
  - `RenderStageContext`
  - `GBufferStage*`
  - GBuffer render target
  - optional SSAO texture
  - `ShadowRuntimeState`
  - environment lighting DSL
  - `getSceneEnvironmentLightingDescriptorSet` callback
- GPU 读取：
  - GBuffer RT0..RT3
  - SSAO texture
  - frame/light DS from `GBufferStage`
  - environment lighting descriptor set
  - shadow directional depth
  - point shadow cubemap array
- GPU 写入：
  - viewport/light accumulation color target
- 隐式依赖：
  - scene environment descriptor set callback
  - primitive quad mesh cache
- Graph 化判断：
  - 可直接映射为 `DeferredLightPass`
  - 需要把 environment lighting 从 callback 改成显式 resource input
  - `GBufferStage*` 最终应降为显式 frame/light resource handle 输入

### ViewportOverlayStage

- 角色：
  deferred viewport 中的 skybox + debug overlay
- 显式输入：
  - `RenderStageContext`
  - skybox descriptor callback
  - debug render system callback
  - active scene callback
  - resource resolve system callback
- GPU 读取：
  - scene skybox cubemap descriptor set
  - primitive cube mesh
  - debug draw data / simple material overlay draw data
- GPU 写入：
  - viewport color
  - viewport depth
- 隐式依赖：
  - `Scene*`
  - `ResourceResolveSystem*`
  - `DebugRenderSystem`
- Graph 化判断：
  - 需要拆成至少两个逻辑 pass：
    - `SkyboxPass`
    - `DebugOverlayPass`
  - 当前 callback/service 依赖太多，不适合直接映射为单一稳定 graph node

### PostProcessingStage

- 角色：
  bloom + basic postprocess + tone mapping
- 显式输入：
  - `ICommandBuffer*`
  - input texture
  - viewport extent
  - optional `FrameContext*`
- GPU 读取：
  - viewport HDR texture
  - bloom intermediate textures
  - postprocess state
- GPU 写入：
  - bloom extract
  - bloom blur ping/pong
  - bloom composite
  - final postprocess texture
- 隐式依赖：
  - config manager
  - runtime `waitIdle()` on resize/recreate
- Graph 化判断：
  - 应拆成固定 pass 链：
    - BloomExtract
    - BloomBlurX/Y loop
    - BloomComposite
    - ToneMap/FinalPostProcess
  - 当前 `execute()` 里带即时重建逻辑，需要先改成资源规格脏标记

### ForwardViewportStage

- 角色：
  forward 路线的大一统 viewport stage
- 显式输入：
  - `RenderStageContext`
  - `ShadowRuntimeState`
  - shadow descriptor set
- GPU 读取：
  - `RenderFrameData` 的全部主要 draw buckets
  - skybox descriptor set
  - environment lighting descriptor set
  - material resource/param descriptor sets
  - skinning SSBO
  - debug resources
- GPU 写入：
  - viewport color
  - viewport depth
  - frame/light/material UBOs
  - skinning SSBO
- 隐式依赖：
  - active scene callback
  - resource resolve callback
  - skybox descriptor callback
  - environment lighting descriptor callback
  - frame clock / frame index callback
  - `PrimitiveMeshCache`
- Graph 化判断：
  - 目前不适合直接 graph 化
  - 至少要先拆成：
    - `ForwardSkyboxPass`
    - `ForwardOpaquePBRPass`
    - `ForwardOpaquePhongPass`
    - `ForwardOpaqueUnlitPass`
    - `ForwardDebugPass`
  - 并继续把 callback/service 依赖压成更稳定的 pass input

## Remaining Hidden Dependencies

下面这些依赖仍是 Render Graph 前最值得清掉的：

- `ForwardViewportStage` 虽然已去掉直接 `App::get()` 反查，但仍依赖多种 scene/service callback，尚未收敛成稳定 pass input
- `LightStage` / `ViewportOverlayStage` 中的 scene-resource callback
- `PostProcessingStage::execute()` 中的即时 `waitIdle() + recreate`
- deferred pipeline 中 `refreshDirtyResources()` 的即时 dirty flush + pipeline update

## Worth Doing Next

按 Render Graph 价值排序，下一步最值得做的是：

1. 先把 `ForwardViewportStage` 拆成更小的 pass-oriented stage
2. 把 `LightStage`、`ViewportOverlayStage` 的 callback 输入改成显式 resource/service input struct
3. 把 `PostProcessingStage` 的即时重建改成“规格脏标记 + frame boundary 重建”
4. 把 `GBufferStage` / `LightStage` 之间共享的 frame/light 资源从 stage owner 改成 frame resource owner

## Low Priority / Can Wait

这些事现在不值得优先做：

- 继续把更多调用从 `RenderRuntime` 转发改成 `RenderSharedResourceProvider`
- 目录整理
- 大规模 typed sync handle 改造
