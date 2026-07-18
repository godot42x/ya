# Frame Graph 顶层编排与资源收口进度

## 当前状态

- 计划建立日期：2026-07-18
- 当前阶段：P0 基线与计划接替
- 当前执行任务：无
- 下一可执行任务：FG-001

## 初始代码审计

### 已完成基础

- resource factory 已覆盖 Vulkan buffer/image/view/sampler 创建，buffer 静态 factory 已删除。
- ResourceStateTracker 已有 subresource state；RenderGraph compiler/registry/executor 已进入真实运行路径。
- Deferred 已有统一 `executeDeferredMainGraph()`，主图包含 shadow、GBuffer、SSAO、light、overlay 和 postprocess。
- Forward shadow group 已 graph-backed；presentation、screenshot/offscreen utility 已有独立 graph 使用案例。
- 固定 automation config、pipeline switch、resize、shadow、SSAO、postprocess、shutdown/readback 基线已存在于旧计划目录。

### 当前关键缺口

- Deferred 顶层图仍从 Stage getter import frame/light/skinning buffer。
- GBuffer、SSAO、Skybox、Forward passes、Shadow passes 仍持有各自 per-flight GPU buffer。
- SSAO、Postprocess、Bloom、Directional/Point/Cull shadow 等模块仍存在局部 executor/standalone graph 入口。
- graph prepare 后仍有 resolved resource 回灌、Stage prepare 和 descriptor cache 更新；setup 与实际 binding 不是同一参数真相源。
- world graph、presentation graph 和 graph 外 capture callback 构成三段 orchestration。
- persistent physical identity 依赖每帧 handle 创建顺序，缺少 stable resource key。
- Texture 仍含全局 `App::get()` factory 查询、upload orchestration 和 render texture 创建职责。
- 原始计划明确延后 transient aliasing，只能提供 persistent key 跨帧复用和 per-flight owner 复用，不能提供不同 logical transient buffer 的物理 allocation 复用。

### 当前数据流

```text
AppFrameLoop
  -> ResourceResolveSystem / RenderFrameExtractor
  -> RenderRuntime::renderFrame
     -> begin/acquire
     -> active pipeline tick
        -> Stage prepare/resource upload
        -> build graph around Stage execute callbacks
        -> graph prepare
        -> resolved resources injected back into Stage descriptors
        -> execute
     -> editor sync
     -> independent presentation graph
     -> graph-external capture callback
     -> submit/present
```

### 目标数据流

```text
resolve assets -> extract immutable frame snapshot
  -> pipeline FrameResourceSet upload/import
  -> top-level FrameGraphOrchestrator build
  -> compile + resolve pass parameters
  -> one world executor
  -> explicit exported outputs
  -> presentation/capture boundary
  -> submit/present
```

## 决策记录

### 2026-07-18：Stage 不作为删除目标

Stage/pass module 可以继续拥有 PSO、pipeline layout、descriptor allocator 和 material upload cache。
需要迁出的对象是 attachment、graph intermediate、per-flight frame GPU buffer、局部主链 executor 和隐式跨 pass resource binding。

### 2026-07-18：先 Deferred 专用，后提公共层

`DeferredFrameResourceSet`、`DeferredFrameGraphResources` 和 `DeferredFrameGraphOrchestrator`
先作为 Deferred 专用类型落地。Forward 成为第二个消费者后，只抽取代码事实证明相同的部分。

### 2026-07-18：DrawList 不作为 orchestrator 前置

当前 `RenderFrameExtractor -> RenderFrameData` 已提供单向 scene snapshot。首轮继续消费现有 draw items，
先完成资源 owner、pass parameters 和 graph orchestration，再独立推进 DrawPacket/DrawList，避免三条主线同时变化。

### 2026-07-18：Presentation 默认保持独立图

swapchain image index、ImGui 和 acquire/final-state 形成独立边界。先把 capture 变成 graph-declared pass；
完成世界图 orchestrator 后再调查合图，不能为了“一张图”牺牲边界清晰度。

### 2026-07-18：Buffer 复用提升为迁移前置

计划修正为同时交付三层能力：persistent stable-key reuse、transient lifetime/physical-slot alias + cross-frame pool、
以及 CPU-written 小 UBO 的 per-flight upload arena。transient alias 首版只覆盖兼容且不重叠的 buffer；
CPU 在 execute 前预写的数据不能共享同一 alias 区间，除非后续把 upload 建模成 graph pass。

完成门禁必须包含 logical/physical bytes、slot assignment、pool hit/miss 和 reuse ratio；core test 必须证明
physical slot 数少于 logical transient buffer 数，真实 Deferred consumer 至少证明 graph slot 的跨帧 pool hit。

## 任务记录模板

```text
### YYYY-MM-DD：FG-NNN 标题

- 状态：开始 / 完成 / 停止调查
- 代码事实：
- 实现：
- 未做：
- 测试：
- artifacts：
- commit：
- 下一任务：
```
