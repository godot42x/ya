# Frame Graph 顶层编排与资源收口计划

> 状态：2026-07-18 建立，作为后续渲染主线执行计划。
>
> 本计划接续 `../render-resource-and-graph-refactor/` 已完成的 resource factory、
> resource state tracker、RenderGraph core、Deferred graph-backed execution 和自动化基线。
> 旧计划继续作为历史记录和资源 API 清理清单；从现在起，Deferred/Forward 主链的
> 顶层编排、frame resource、pass parameter 和全量 RenderGraph 迁移以本计划为准。

## 1. 目标

把当前“RenderGraph 包住各个 Stage 的 execute，但 Stage 仍暗中持有 frame buffer、
descriptor 和局部 executor”的过渡实现，收敛为一条可以从顶层代码直接读懂的单向数据流：

```text
Scene/ECS/Asset state
  -> RenderFrameExtractor
  -> immutable RenderFrameData + FrameViewInput
  -> FrameResourceUploader
  -> FrameResourceBindings (GPU owners + imported handles)
  -> Pipeline FrameGraph Builder
  -> RGCompiledGraph
  -> pass parameter resolve / descriptor binding
  -> one world-frame RenderGraphExecutor
  -> command submission
  -> presentation
```

最终要求：

1. 顶层 orchestrator 能按代码顺序展示 Shadow、GBuffer/Forward、SSAO、Lighting、Overlay、Postprocess 和 viewport output。
2. 所有影响本帧依赖、barrier、GPU 写后读、resize/replacement 的 texture/buffer 都以 graph handle 出现在 pass 声明中。
3. Stage/pass module 不再拥有 attachment、per-flight frame/view/light/skinning buffer，也不再拥有主链 executor。
4. execute callback 不查询 scene/runtime singleton，不创建资源，不更新 graph 结构，只消费已准备好的 pass parameters 并录制命令。
5. Deferred 先证明目标结构成立；Forward 复用同一 frame-resource 和 pass-parameter 契约后再迁移。
6. Vulkan 主路径稳定后再清理资产 Texture API 和剩余 compatibility path；OpenGL 仍冻结。
7. RenderGraph 对 transient buffer 执行 lifetime analysis、physical slot alias 和跨帧池化；小型 CPU upload buffer 使用 per-flight arena，避免只把大量独立 allocation 从 Stage 搬到新 owner。

## 2. 非目标与停止线

本计划不要求：

- 删除所有带 `Stage` 或 `Pass` 名称的类。类可以继续作为 PSO、pipeline layout、material upload cache 和 draw recorder。
- 把 mesh vertex/index buffer、资产 texture、material cache、PSO 或长期 sampler 交给每帧 RenderGraph 创建。
- 首轮不实现 texture aliasing、async compute、多 queue、pass fusion、GPU-driven draw 或公开 editor extension API。buffer pooling/aliasing 属于本计划必做能力。
- 把 swapchain acquire、queue submit、present 强行建模成普通 graph pass。
- 一开始抽象跨 Deferred/Forward 的万能 orchestrator。先做 Deferred 专用实现，第二个消费者出现后再抽公共结构。
- 为了消灭文件名而搬代码，或在迁移途中进行目录重排、OpenGL 修复和无关 facade 清理。

若某个任务不能减少隐藏资源 owner、隐式依赖、局部 executor、反向 descriptor 注入或图外录制，
就不计入本计划的有效进度。

## 3. 当前代码事实

### 3.1 当前顶层流程

`RenderRuntime::renderFrame()` 当前顺序是：

```text
begin/acquire + command buffer begin
  -> apply pending pipeline switch
  -> active pipeline tick
  -> sync editor viewport context
  -> independent Presentation RenderGraph
  -> graph 外 presentation capture callback
  -> submit/present
```

世界渲染入口位于：

- `Engine/Source/Runtime/App/RenderRuntime.cpp`
- `Engine/Source/Runtime/App/RenderRuntimeFrame.cpp`
- `Engine/Source/Runtime/App/Common/IRenderPipeline.h`

场景数据先由 `RenderFrameExtractor` 生成 `RenderFrameData`，这已经形成正确的
`ECS -> extracted frame snapshot -> renderer` 方向，应保留并加强，不把 scene 查询重新放回 pass。

### 3.2 Deferred 当前状态

`DeferredRenderPipeline::executeDeferredMainGraph()` 已经在一张图中声明：

```text
Shadow -> GBuffer -> SSAO -> Deferred Light -> Skybox -> Scene Overlay
       -> Viewport Overlay -> Bloom/ToneMap
```

但是它仍有以下过渡结构：

- `GBufferStage` 持有 frame/light/skinning per-flight buffer 和 descriptor。
- `SSAOStage` 持有 frame UBO、input descriptor、noise texture，并保留独立 executor。
- `ViewportOverlayStage` 持有 skybox frame UBO 和 descriptor。
- shadow directional/point/cull pass 各自仍可构建并执行局部 graph，且持有局部 executor。
- pipeline 在 graph `prepare()` 后把 resolved image 回灌给 Light/SSAO/Postprocess，再调用 Stage `prepare()`。
- pass callback 最终调用 Stage `execute()`，实际 descriptor 绑定资源与 graph setup 声明不是同一个结构化参数源。

因此当前准确表述是“Deferred 已完成 graph-backed execution”，不是“Deferred frame graph 架构完成”。

### 3.3 Forward 当前状态

Forward shadow group 已能 append 到 graph，但 viewport 内的 PBR、Phong、Unlit、Skybox、Simple、Debug、
Direction Overlay 和 Postprocess 仍由 `ForwardViewportStage` 与其子 pass 按固定顺序录制。
Forward 不能在顶层 graph 中枚举全部 pass/resource dependency，也仍保留 stage-owned per-flight buffer。

### 3.4 GPU 资源 API 当前状态

已具备：

- `IRenderResourceFactory` 可创建 buffer/image/view/sampler。
- buffer 静态 factory 已删除，主要调用点已迁移。
- `RenderImage` 已承接 graph intermediate/attachment 的 image + default view owner。
- `RenderGraphResourceRegistry` 已支持 texture/buffer 创建、import、replacement 和 deferred retirement。
- `ResourceStateTracker` 已支持 image subresource state 和 graph state plan。

尚未闭环：

- `Texture` 仍通过 `App::get()` 获取 resource factory，仍有 `createRenderTexture()`。
- `BufferCreateInfo`、`ImageCreateInfo`、`ImageViewCreateInfo` 与 graph desc 的职责和 immutable/replacement 规则没有完全统一。
- persistent graph resource 依赖每帧重建时的 handle/index 稳定，缺少显式稳定 key。
- buffer state/range、host-write 和 dynamic capacity replacement 契约仍不完整。
- descriptor 实际绑定没有从 graph pass declaration 派生或校验。
- transient logical buffer 当前逐个创建 physical buffer，没有 lifetime interval、physical slot plan、alias barrier 或复用统计。
- frame/light/SSAO/skybox 等小型 CPU-written UBO 当前分别 allocation，尚无 completion-safe per-flight upload arena。

## 4. 目标架构

### 4.1 顶层对象边界

首个实现采用 Deferred 专用对象，避免过早抽象：

```text
DeferredRenderPipeline
  owns DeferredFrameGraphOrchestrator
  owns DeferredFrameResourceSet
  owns one RenderGraphExecutor
  owns pass modules (pipeline/PSO/material cache only)
```

建议文件：

- `DeferredRender/DeferredFrameGraphOrchestrator.h/.cpp`
- `DeferredRender/DeferredFrameResources.h/.cpp`
- `DeferredRender/DeferredFrameGraphTypes.h`

当 Forward 开始迁移并出现第二个真实消费者后，再提取以下公共能力：

- `Common/FrameGraphResourceSet` 中确实相同的 frame/view/skinning buffer owner
- `Common/FrameGraphBlackboard` 或等价的 typed resource table
- pass parameter resolve/validation helper

不得先创建空的公共基类再让 Deferred/Forward 继承。

### 4.2 资源分类

每种资源必须明确属于以下一类：

| 类别 | owner | graph 表达 | 例子 |
|---|---|---|---|
| Asset/Durable | AssetManager、Mesh、Material 或 shared provider | Imported | albedo texture、environment cubemap、vertex/index buffer |
| Frame durable | pipeline-level FrameResourceSet | Imported，每 flight 显式 handle | frame/light/skinning UBO/SSBO、CPU 上传的 indirect template |
| Graph persistent | registry，以稳定 resource key 识别 | Persistent | viewport、GBuffer、history、稳定 debug output |
| Graph transient | registry，只在 compiled frame 使用 | Transient | 临时 blur ping/pong、临时 compute scratch |
| External | swapchain/offscreen job owner | Imported，声明 initial/final state | presentation image、job source/result |

规则：

- 资源是否由 Graph 创建与资源是否必须在 Graph 中声明是两件事。
- Asset/Frame durable buffer 可以保持图外 owner，但参与 pass 时必须 import 并声明访问。
- PSO、pipeline layout、descriptor pool 和 material upload cache 不属于 graph resource。
- pass module 不得拥有 Frame durable 或 Graph persistent/transient 资源。

### 4.3 Stable resource identity

为跨帧 persistent resource 引入稳定逻辑 key，建议最小 API：

```cpp
struct RGResourceKey
{
    std::string name;
};

RGTextureHandle createPersistentTexture(RGResourceKey key, const RGTextureDesc& desc);
RGBufferHandle createPersistentBuffer(RGResourceKey key, const RGBufferDesc& desc);
```

registry 对 persistent resource 使用 key + type 作为跨帧物理身份；handle 只在当前 graph 内解析。
同 key/spec 复用，spec 变化触发安全 replacement，同 key/type 冲突或同帧 desc 冲突直接编译失败。

首轮不为 transient/imported resource 引入全局名称。buffer aliasing 由 compiled lifetime/slot plan 驱动，
不使用 persistent key；texture aliasing 仍延后。

### 4.4 Transient buffer reuse 与 upload arena

“buffer 复用”拆成三种不同机制，不能混用：

1. persistent reuse：同 stable key 跨帧复用同一物理 buffer，spec 变化时 replacement。
2. transient aliasing/pooling：多个 logical transient buffer 根据编译后生命周期共享 physical slot，slot 在后续 frame 继续复用。
3. upload arena：CPU-written 小型 UBO/上传切片共享 current-flight host-visible backing buffer。

Graph compiler 必须在拓扑排序后生成：

```cpp
struct RGBufferLifetime
{
    RGBufferHandle buffer;
    uint32_t firstUse;
    uint32_t lastUse;
};

struct RGTransientBufferSlot
{
    uint32_t slot;
    uint64_t size;
    EBufferUsage usage;
    EMemoryUsage memoryUsage;
    std::vector<RGBufferHandle> logicalBuffers;
};
```

首版 slot 规则：

- 只处理 `Transient` buffer；Imported、Persistent、exported/readback buffer 不参与 alias。
- 两个 logical buffer 的 `[firstUse, lastUse]` 不重叠才可共享 slot。
- `memoryUsage` 必须兼容；首版优先 `GpuOnly`，slot size 取成员最大 size，usage 取并集。
- alignment 必须由 backend-agnostic resource limits/factory contract 提供，不能在上层写 Vulkan 常量。
- registry 为 physical slot 创建一个 buffer，并维护 frame-local logical handle -> physical slot 映射。
- executor 在 logical identity 切换处执行 alias memory barrier并重置该 physical buffer 的 tracked logical state。
- physical slot pool 属于 executor/pipeline scope；跨帧按兼容 spec 复用，resize/usage 扩张走安全 replacement。

CPU-written buffer 的限制：如果两个 logical buffer 都在 command recording 前写入，同一物理区间 alias 会导致后写覆盖前写，
因此它们不能进入上述 alias allocator。小型 UBO 改用 per-flight upload arena 的不同 offset/range；只有把写入建模成
graph upload/transfer pass 后，才允许按执行期 lifetime alias。

`FrameUploadArena` 最小契约：

- 每 flight 独立 backing buffer，只有对应 fence 完成后才能 reset/reuse。
- 按 uniform/storage alignment 分配 `BufferSlice { owner, offset, size }`。
- descriptor 使用现有 `DescriptorBufferInfo.offset/range`，不要求首版改成 dynamic descriptor type。
- capacity growth 在 frame boundary replacement，旧 backing owner 保活到 submit completion。

compiled graph/debug diagnostics 必须输出：logical transient buffer count/bytes、physical slot count/bytes、
跨帧 pool hit/miss、alias assignment 和 reuse ratio。没有这些数据，不算完成 buffer 复用。

### 4.5 Frame resource owner

`DeferredFrameResourceSet` 首轮接管并分类：

- frame/view uniform slice per flight（来自 `FrameUploadArena`）
- light uniform slice per flight（来自 `FrameUploadArena`）
- skinning storage buffer per flight 与 capacity/replacement
- SSAO frame uniform slice per flight（来自 `FrameUploadArena`）
- skybox frame uniform slice per flight（来自 `FrameUploadArena`）

第二批再处理 shadow，必须先分类而不是全部搬进 FrameResourceSet：

- directional/point shadow frame/face/skinning buffer
- point shadow cull frustum、draw command、visible instance buffer
- point shadow indirect renderer instance buffer

FrameResourceSet 负责 allocation、capacity、CPU upload 和 completion-safe replacement；不负责 pass 顺序和 draw。
它返回 owner-backed import descriptors 或 typed import inputs，不返回供长期缓存的裸指针。

- CPU 在 graph execute 前写入且整个 frame 使用的数据属于 Frame durable 或 upload arena slice。
- 只由 GPU pass 写入/读取的 scratch buffer 优先改为 Graph transient，进入 slot alias/pool。
- CPU fallback 会预写的 buffer 在上传时序 graph 化前保持 Frame durable，不允许伪装成可 alias transient。

### 4.6 Typed graph resources and pass parameters

顶层构图使用 typed resource table，首版可以是简单 struct，不急于做通用 blackboard：

```cpp
struct DeferredFrameGraphResources
{
    RGBufferHandle frame;
    RGBufferHandle light;
    RGBufferHandle skinning;
    std::array<RGTextureHandle, 4> gbufferColors;
    RGTextureHandle depth;
    std::optional<RGTextureHandle> ssao;
    RGTextureHandle hdrColor;
    RGTextureHandle finalColor;
};
```

每个 pass 使用一份不可跨帧保存的参数对象：

```cpp
struct DeferredLightPassParameters
{
    RGBufferHandle frame;
    RGBufferHandle light;
    std::array<RGTextureHandle, 4> gbuffer;
    std::optional<RGTextureHandle> ssao;
    std::optional<RGTextureHandle> shadow;
    RGTextureHandle output;
};
```

setup 从同一个参数对象声明 read/write；execute 从同一个参数对象 resolve/bind。
禁止 setup 声明一套 handle、execute 再从 Stage 成员读取另一套 image/buffer。

### 4.7 Descriptor binding 迁移层级

不等待完整 Slang reflection 才开始迁移，按两步落地：

1. 内部 `RGPassBindingContext`：通过 graph handle resolve image/buffer，更新现有 descriptor set，并在 debug 下校验所有 resolve 的 handle 已由本 pass 声明。
2. 在 Deferred 和 Forward 至少各迁一个材质路径后，再抽 `ShaderParameterBlock`/generated binder；禁止手写 shader-facing mirror struct。

第一步完成前，Stage 可以继续持有 descriptor pool/layout/set，但不能缓存 graph-owned image/view owner，
也不能从 pipeline setter 接收 SSAO/GBuffer output。第二步完成后 descriptor set 的写入从 Stage prepare 移到 pass binding。

### 4.8 Orchestrator 责任

`DeferredFrameGraphOrchestrator::build()` 必须在一个函数或顺序清晰的同级 helper 中展示：

```text
importFrameResources
importSceneResources
appendShadowPasses
appendGBufferPass
appendSSAOPass (optional)
appendDeferredLightPass
appendSkyboxPass
appendSceneOverlayPass
appendViewportOverlayPass
appendPostprocessPasses
exportDebugAndViewportOutputs
```

helper 只追加 pass 和返回 typed handles；不得自行 execute graph。主链只允许一个 world-frame executor。

Presentation 首轮继续是独立 graph，因为其 physical image identity 跟 swapchain image index 绑定；
世界图完成后再做一次调查，若能在不破坏 acquire/final-state 边界的情况下 append 到同一 graph，才合并。
无论是否合并，presentation capture 必须最终成为声明 transfer/read 依赖的 graph pass，而不是裸 callback。

## 5. 单向数据流规则

### 5.1 CPU 数据

```text
ECS components / Asset refs
  -> ResourceResolveSystem resolves durable assets
  -> RenderFrameExtractor copies render-visible snapshot
  -> RenderFrameData is read-only for the renderer
  -> FrameResourceUploader writes current-flight buffers
  -> passes consume handles + immutable draw inputs
```

禁止 pass execute 访问 active scene、registry、`App::get()` 或 resource resolve system。
GUI/settings 只修改 pending settings；pipeline 在下一 frame boundary 捕获不可变 frame settings。

### 5.2 GPU 数据

```text
durable/frame owners
  -> imported descriptors
  -> graph logical handles
  -> compiler dependencies + state plan
  -> registry physical resources
  -> pass binding resolve
  -> command recording
  -> command buffer retain until submit completion
```

禁止 resolved `RenderImage*`、`IImageView*` 或 graph handle 跨帧保存在 Stage。
editor/debug output 通过 frame execution result 导出 shared owner，不反向查询 Stage。

### 5.3 Draw 数据

本计划保留现有 `RenderFrameData` draw items 作为第一阶段输入，不把 DrawList 重构设为 orchestrator 前置。
但 pass module 只接收筛选后的只读 draw span/view，不能自行查询 ECS。

在 Deferred/Forward 都完成顶层 graph 后，再单独推进：

```text
RenderItem -> visibility/LOD -> DrawPacket -> sorted DrawList -> pass
```

这样避免同时改资源 ownership、graph orchestration 和 draw submission 三条高风险链。

## 6. 实施阶段

具体任务、依赖和验收见 `todo.md`。阶段顺序如下：

### Phase 0：基线和完成定义修正

- 固化当前 graph dump、固定机位截图、draw count 和 validation 基线。
- 把旧计划的“Deferred 完全 graph 化”修正为“graph-backed execution”。
- 建立隐藏 owner、局部 executor、graph 外 resource binding 的可重复 inventory。

### Phase 1：RenderGraph 核心契约补齐

- persistent resource stable key
- pass resolve/access debug validation
- buffer range/state 与 replacement tests
- execution result/export owner 契约
- transient buffer lifetime analysis、physical slot allocation、跨帧 pool 与 alias barrier
- per-flight upload arena 与 buffer slice descriptor binding

这些是后续搬 Stage owner 的前置，不先扩 Forward。

### Phase 2：Deferred frame resource owner

- 新建 Deferred 专用 FrameResourceSet。
- 先让 frame/light/SSAO/skybox 小 UBO 使用 upload arena，再迁 dedicated skinning，最后分类迁移 shadow/cull。
- GPU-only scratch 优先 graph-owned transient，CPU-prewritten buffer 保持 frame durable，直到上传 pass graph 化。
- 每批只改变 owner 与显式输入，不同时重写 draw loop 或 shader。

### Phase 3：Deferred typed resources 与 pass parameters

- 建立 `DeferredFrameGraphResources` 和各 pass parameters。
- setup 与 execute 使用同一参数对象。
- 删除 resolved image 回灌和 Stage resource getter。

### Phase 4：Deferred 顶层 orchestrator

- 从 `DeferredRenderPipeline` 提取清晰的 build/prepare/execute 边界。
- 合并主链局部 executor，只保留一个 world-frame executor。
- 导出 viewport/debug outputs，不让 editor 反查 Stage。

### Phase 5：Descriptor binding 收口

- 加入 pass-scoped resolve/binding validation。
- 依次迁移 SSAO、Deferred Light、Skybox、GBuffer frame resources。
- material descriptor cache 暂留 pass module；它只消费 durable asset bindings。

### Phase 6：Presentation/capture 边界

- screenshot/presentation capture 改为 graph-declared copy/readback。
- 调查 presentation 是否并入 world graph；有明确收益且状态边界简单才合并。
- 保持 acquire/submit/present 在 graph 外。

### Phase 7：Forward 迁移

- 复用已经被 Deferred 验证的 frame resource、pass parameter 和 binding 契约。
- 按 Shadow -> PBR/Phong -> Unlit -> Skybox/Overlay/Debug -> Postprocess 分批迁移。
- 删除 `ForwardViewportStage::executePasses()` 的隐藏固定顺序和 stage-owned frame buffers。

### Phase 8：GPU resource API 清理

- Texture decode/upload/asset owner 分层。
- 删除 `Texture::getResourceFactory()`、`Texture::createRenderTexture()` 和资源创建中的 `App::get()`。
- 统一 desc immutable、map/write/flush 和 failure behavior。
- 只做已经被 graph/frame resource 使用证明需要的公共 API，不预建新 RHI。

### Phase 9：清理和完成验收

- 删除主链局部 executor、resource setter/getter 和 obsolete Stage compatibility API。
- graph dump 能完整展示 Deferred/Forward 世界渲染流程和所有 frame GPU dependencies。
- 更新 render architecture/resource skills 与计划完成状态。

## 7. 提交策略

推荐提交类型：

- `[render/graph] add stable persistent resource identity`
- `[render/graph] validate pass resource resolution`
- `[runtime/deferred] centralize frame buffer ownership`
- `[runtime/deferred] add typed frame graph parameters`
- `[runtime/deferred] expose top-level frame graph orchestration`
- `[runtime/forward] migrate lit passes to frame graph`
- `[render/resource] separate asset texture upload`
- `[test/render] lock frame graph orchestration behavior`
- `[plan/render] define frame graph orchestration migration`

一个提交可以包含一组紧密相关的实现和测试，不把每个 getter 删除拆成零碎提交。
禁止在同一提交混入：公共 graph API + 多条 pipeline 行为迁移、shader 行为变化 + owner 搬迁、
目录整理 + 功能改动、Forward + Deferred 大范围迁移。

## 8. 每次提交的强制验证

每个实现提交前必须依次运行：

```bash
make test
make b t=HelloMaterial
```

然后按改动选择至少一个 editor smoke；触及共享 graph/resource/runtime 时运行完整矩阵：

```bash
make r t=HelloMaterial r_args="--automation-config=.agent/plan/render-resource-and-graph-refactor/deferred-baseline.automation.json --exit-after-frame=1500"
make r t=HelloMaterial r_args="--automation-config=.agent/plan/render-resource-and-graph-refactor/forward-baseline.automation.json --exit-after-frame=1500"
make r t=HelloMaterial r_args="--automation-config=.agent/plan/render-resource-and-graph-refactor/pipeline-switch-smoke.automation.json --exit-after-frame=1500"
```

资源 replacement/shutdown 改动还需：

- `viewport-resize-smoke.automation.json`
- `shadow-smoke.automation.json`
- `shutdown-readback-smoke.automation.json`

日志必须检查 `Validation Error`、`VUID-`、`[Error]`；视觉改动必须使用固定场景、固定 PBR 球体和固定相机位置比较。

## 9. 总完成标准

只有以下全部成立才可宣布“全量迁移到 RenderGraph”：

- Deferred 和 Forward 各有一个显而易见的顶层 frame graph orchestrator。
- 世界渲染每条 pipeline 每帧只使用一个主 executor；utility/offscreen job 有明确独立边界。
- graph dump 可枚举主链全部 pass、texture/buffer dependency 和最终输出。
- compiled graph 能把不重叠的兼容 transient logical buffer 分配到同一 physical slot，并在 alias 边界正确同步。
- buffer diagnostics 显示 logical/physical bytes、slot assignment、pool hit/miss 和 reuse ratio。
- frame/light/SSAO/skybox 小型 CPU-written UBO 使用 per-flight upload arena slice，不再各自持有独立 allocation。
- Stage/pass module 不持有 attachment、graph persistent/transient resource 或 per-flight frame GPU buffer。
- setup 与 execute 从同一 typed pass parameters 获取资源。
- execute 中没有资源创建、`waitIdle()`、scene/global query 或未声明的 GPU resource access。
- persistent resource 使用稳定 key，resize/spec change 走安全 replacement。
- debug/editor output 从 frame graph execution result 导出，不反向查询 Stage owner。
- presentation capture/readback 是 graph-declared dependency。
- Texture 资产 API 不再创建 render attachment，不再通过 `App::get()` 找 factory。
- `make test`、HelloMaterial build、Deferred/Forward/pipeline switch/resize/shadow/shutdown smoke 和关键截图基线全部通过。
