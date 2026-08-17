# RHI 图像 owner 收口计划：ImageResource / Texture / RenderTexture 主链路

> 建立日期：2026-08-16
> 状态：进行中
> 目标：把图像资源主链路收口为 `IImage/IImageView -> ImageResource -> Texture | RenderTexture -> RenderGraph / RenderRuntime consumer`，逐步删除 `RenderImage` 作为公开业务语义。

## 0. 结论摘要

本阶段不再继续补“方便桥接”的 facade，也不做名字并存的长期兼容层。正式语义收口如下：

1. `ImageResource` 是唯一底层图像 owner。
2. `Texture` 只表达资产 / 采样纹理语义。
3. `RenderTexture` 只表达运行时可写图像语义：viewport、shadow、postprocess、presentation import/export、中间 RT。
4. `RenderImage` 退化为过渡壳，只允许暂存于尚未迁完的旧接口与内部实现，不再作为新的公共语义继续扩散。
5. `RenderGraph` 的公开边界逐步改为 owner + view-spec，不再公开泄漏 raw `shared_ptr<IImageView>` / `IImageView*` 给 graph consumer。
6. `RenderRuntime / Render3D` 的对外输出逐步统一为 `RenderTexture`，而不是 `RenderImage`。

## 1. 当前现状

当前代码已经迈出半步，但仍处在双模型中间态：

- `ImageResource` 已存在，能承载 `IImage + defaultView + retainedResources + desc`；
- `Texture` 已经切到 `std::shared_ptr<ImageResource> resource` 持有模式；
- 但 `RenderGraph`、`RenderRuntime`、`Render3D` 仍然大量以 `RenderImage` 为运行时 owner 语义；
- 一些 debug / shadow view 公共接口已经开始往 `ImageResource` 收，但 exported texture / viewport output / postprocess output 还没切到 `RenderTexture`。

因此本计划采用渐进迁移，而不是一次性删除 `RenderImage`。

## 2. 硬规则

### 2.1 允许存在的主语义

- RHI primitive：`IImage`、`IImageView`
- 唯一底层 owner：`ImageResource`
- 上层语义对象：`Texture`、`RenderTexture`

### 2.2 明确禁止

- 不再新增公开 API，让模块外 consumer 同时理解 `Texture + RenderImage + IImageView` 三套语义。
- 不再新增 graph/runtime public API 直接接受 `shared_ptr<IImageView>` 作为业务输入。
- 不把 `RenderTexture` 做成“内部其实还是 `Texture` 伪装”的桥。
- 不为了短期兼容，再造第二个 `ImageResourceRef` 风格桥接结构。

### 2.3 默认边界

- driver-level view 只允许存在于 RHI/backend、graph registry、descriptor update helper、command recording 内部。
- graph consumer 与 runtime consumer 默认只理解 owner 级对象。

## 3. 类型职责

### 3.1 ImageResource

唯一底层 owner，负责：

- immutable desc；
- `image + defaultView`；
- retained resource lifetime；
- owner 级访问：format / extent / image / defaultView / retainedResources。

### 3.2 Texture

资产 / 采样纹理语义，负责：

- 文件路径、channels、fromMemory / fromData / createCubeMap 等资产入口；
- 指向 `ImageResource`；
- 不再持有独立的 image/view 双份真相。

### 3.3 RenderTexture

运行时可写图像语义，负责：

- color/depth target、中间 RT、viewport output、presentation import/export；
- 指向 `ImageResource`；
- 提供 runtime-oriented 的 create / wrap / adopt owner 入口；
- 作为 graph exported texture 与 runtime output 的正式公开语义。

### 3.4 RenderImage

仅作为过渡壳：

- 当前仍可在未迁完的内部 owner 链路中短期存在；
- 不再允许新增新的 public-facing 依赖点；
- 进入最终删除路径。

## 4. 实施阶段

## Phase 1 — 建立 RenderTexture，不改 graph/runtime 主行为

目标：把缺失的正式 runtime owner 类型先补齐。

范围：

- 新增 `RenderTexture` 类型与公开头；
- 提供 create / wrap / adopt `ImageResource` 入口；
- 为 `RenderGraphImportUtils` 增加 `RenderTexture` owner-first import 支持；
- 保持旧 `RenderImage` 路径继续工作，但不再新增依赖。

完成标准：

- `Texture` 与 `RenderTexture` 都能通过统一 owner 访问接口暴露 `ImageResource`；
- graph 可以无损 import `RenderTexture`；
- 没有新增基于 `RenderImage` 的公开能力。

## Phase 2 — 收 RenderGraph 公共边界

目标：让 graph exported / imported texture 的公开语义转到 owner-first。

范围：

- `RenderGraphExecutionResult` exported textures 改为 `std::shared_ptr<RenderTexture>`；
- `RGImportedTextureDesc` 继续保留 `resource + optional viewDesc` 主模型；
- graph public helper 逐步移除 raw `shared_ptr<IImageView>` 业务入口；
- registry 内部负责 imported subresource view materialization 与缓存。

完成标准：

- graph public headers 不再把 exported texture 暴露为 `RenderImage`；
- graph public helpers 不再鼓励模块外直接传 raw image view。

## Phase 3 — 收 RenderRuntime / Render3D 公共表面

目标：把 runtime-facing output 从 `RenderImage` 切到 `RenderTexture`。

范围：

- viewport output / depth / entity-id / bloom / postprocess / presentation outputs 改为 `RenderTexture`；
- `RenderPipelineDebugOutputCatalog`、`IRenderPipeline`、`RenderRuntime`、`AppRenderServices` 统一 owner 语义；
- shadow face/cascade raw view getter 继续禁止外溢。

完成标准：

- runtime-facing public headers 不再以 `RenderImage` 作为 exported runtime texture 语义；
- consumer 只通过 `Texture / RenderTexture / ImageResource` 理解图像资源。

## Phase 4 — 删除过渡桥与残留噪声

范围：

- 删除 `RenderImage`；
- 删除 `ImageResourceRef`；
- 删除只为双模型兼容保留的 overload / helper / adapter；
- 审计 public headers，确保没有新的 raw image/view 泄漏点。

完成标准：

- 图像 owner 只有 `ImageResource`；
- 上层语义对象只有 `Texture` 与 `RenderTexture`；
- driver view 只留在内部。

## 5. 非目标

- 这次不重构 Vulkan/OpenGL backend 对象模型；
- 这次不重写 RenderGraph 调度算法；
- 这次不顺手做 texture asset authoring 系统扩张；
- 这次不做新的 facade 或万能 resource variant。

## 6. 验证策略

至少按阶段验证：

- `xmake b ya-rhi`
- `xmake b ya-render-graph`
- `xmake b ya-render-3d`
- `xmake b ya-game-runtime`
- 若公共面牵到 editor，再补 `xmake b ya-game-editor`

静态审计目标：

- graph public headers 中逐步消除 `shared_ptr<IImageView>` 业务入口；
- runtime-facing public headers 中逐步消除 exported `RenderImage` 语义；
- 不再继续新增 `ImageResourceRef` 新用法。
