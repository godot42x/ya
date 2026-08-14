# 当前渲染流程与迁移评估

## 结论

当前工程不是“没有 RenderGraph”，而是处于第二阶段迁移前的过渡状态：

- RenderGraph core、registry、executor 和 state tracker 已经是真实运行系统。
- Deferred 世界渲染已经 graph-backed，主要 pass 顺序和 attachment state 已进入一张主图。
- Forward 只有 shadow group 和 viewport 外壳部分 graph-backed，主要 surface passes 仍由 Stage 固定顺序执行。
- 两条 pipeline 的 frame GPU buffer、descriptor binding、局部 executor 和 output snapshot 仍分散在 Stage/pass module。

所以接下来最重要的不是继续逐个给 Stage 套 graph pass，而是先建立顶层 frame resource 与 pass parameter 契约，
使 Graph 成为执行顺序、GPU resource dependency、physical lifetime 和实际 binding 的共同事实源。

补充判断：原计划只覆盖 persistent buffer 跨帧复用和 per-flight buffer 长期持有，明确延后了
transient aliasing，因此不能交付 RenderGraph 的物理 buffer 复用收益。该项现提升为迁移前置：
Graph compiler 必须计算 transient buffer lifetime，registry 必须按 physical slot 分配/池化，
不重叠生命周期的兼容逻辑 buffer 必须能够共享物理 buffer。

## 当前单帧流程

```text
AppFrameLoop
  |
  +-- ResourceResolveSystem
  |     resolve asset/runtime texture/material/environment state
  |
  +-- RenderFrameExtractor
  |     ECS -> camera/lights/draw items/skinning palettes -> RenderFrameData
  |
  +-- RenderRuntime::renderFrame
        |
        +-- acquire + begin command buffer
        +-- active pipeline tick
        |     +-- pipeline/stage prepare
        |     +-- update per-stage UBO/SSBO/descriptors
        |     +-- build graph around stage execute callbacks
        |     +-- prepare registry resources
        |     +-- inject resolved resources back into stages
        |     +-- execute world graph
        +-- editor viewport sync
        +-- independent presentation graph + ImGui
        +-- graph-external capture callback
        +-- submit + present
```

数据大方向已经是单向的：ECS 经 extractor 变成 `RenderFrameData`。问题主要从 pipeline 开始：
Stage 的 prepare/owner/descriptor cache 与顶层 graph 相互回读，形成第二套隐式资源流。

## 主要缺口

| 领域 | 当前状态 | 全量迁移要求 | 优先级 |
|---|---|---|---|
| 顶层编排 | Deferred 有大函数，Forward 仍有隐藏固定顺序 | 每条 pipeline 有清晰 orchestrator/build 入口 | P0 |
| Persistent identity | 依赖 frame-local handle/index 顺序 | stable key 与安全 replacement | P0 |
| Transient buffer reuse | 每个 logical handle 独立创建 physical buffer | lifetime analysis + physical slot alias + cross-frame pool | P0 |
| CPU upload reuse | 每个 Stage 创建小型 per-flight UBO | aligned per-flight upload arena/suballocation | P0 |
| Frame buffer owner | 分散在 GBuffer/SSAO/Skybox/Forward/Shadow | pipeline-level FrameResourceSet | P0 |
| Pass resources | setup 声明与 execute 成员读取并存 | typed params 同时驱动 setup/resolve | P0 |
| Descriptor binding | resolved image 回灌 Stage，缓存 view handle | pass-scoped binding + declaration validation | P1 |
| Executor | 主图外仍有多个局部 executor/standalone path | world frame 一个 executor，utility 边界明确 | P1 |
| Debug/output | pipeline snapshot、Stage getter、registry resolve 混用 | execution result 显式 export owner | P1 |
| Presentation/capture | 独立 graph + graph 外 callback | capture graph-declared；presentation 是否合图另行调查 | P2 |
| Forward | 主 surface passes 未 graph 化 | 复用 Deferred 验证后的契约迁移 | P2 |
| Texture API | asset/upload/render attachment 语义仍混合 | upload service、删除 App::get/factory/render texture | P3 |
| Draw submission | 已有 extracted draw items，尚无统一 DrawList | 顶层 graph 完成后独立推进 | 后续 |

## 哪些 GPU API 是前置

全量 Graph 迁移前必须先完成：

1. Persistent resource stable key，否则可选 pass、重排和多 viewport 会破坏跨帧 physical identity。
2. Buffer range/state 和 capacity replacement 契约，否则 skinning/indirect/cull 只能作为不透明 imported buffer。
3. Explicit execution result/export owner，否则 editor/debug/screenshot 会继续反查 registry 或 Stage。
4. Pass-scoped resolve/access validation，否则 graph declaration 与实际 descriptor binding 仍可能漂移。
5. FrameResourceSet owner，否则顶层 Graph 只能 import Stage 私有资源，无法成为资源流程真相源。
6. Transient buffer lifetime/slot allocation，否则每个 graph logical buffer 仍对应一次独立物理 allocation。
7. Per-flight upload arena，否则 frame/light/SSAO/skybox 等小 UBO 只是从 Stage 搬到一个新的“大 owner”，没有减少 allocation。

可以后置到 Deferred/Forward orchestrator 完成后：

- Texture decode/upload 全面拆分。
- 删除所有资源创建中的 `App::get()`。
- 完整 generated ShaderParameterBlock。
- DrawPacket/DrawList、Editor Extension、texture aliasing、async compute、OpenGL 恢复。

把后置项提前会扩大同时变化的系统数量，但不会直接解决顶层 Graph 不可读的问题。

## 目标流程

```text
Scene/Asset authoring state
  -> resolve durable assets
  -> extract immutable RenderFrameData
  -> upload current-flight FrameResourceSet
  -> import durable/frame resources into graph
  -> build typed pipeline graph
       Shadow
       -> GBuffer or Forward Surface
       -> SSAO/Lighting
       -> Skybox/Overlay
       -> Postprocess
       -> exported viewport/debug outputs
  -> compile dependencies and state plan
  -> resolve typed pass parameters / descriptors
  -> execute once
  -> presentation/capture boundary
  -> submit/present
```

## 推荐执行顺序

1. 先固化基线，以及 stable identity、access、export、buffer lifetime/physical-slot 四类 Graph core 契约。
2. 实现 transient buffer pool/alias barrier 和 per-flight upload arena，并用 core tests 证明 logical allocation 少于 physical allocation。
3. 把 Deferred frame/light/skinning/SSAO/skybox/shadow buffer owner 上移；小 UBO 进入 arena，GPU-only scratch 优先 graph-owned transient。
4. 用 typed pass parameters 消除 setup/execute 双重资源来源。
5. 提取 DeferredFrameGraphOrchestrator，删除主链局部 executor 和 output 回灌。
6. 收敛 descriptor binding，再以相同模式迁移 Forward。
7. 最后清理 Texture/upload/global factory API 和 compatibility path。

详细任务编号、依赖、验收和提交边界见 `todo.md`；worker 操作规则见 `worker-guide.md`。
