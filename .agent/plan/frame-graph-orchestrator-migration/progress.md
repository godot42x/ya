# Frame Graph 顶层编排与资源收口进度

## 当前状态

- 计划建立日期：2026-07-18
- 当前阶段：P1 RenderGraph 核心前置
- 当前执行任务：无
- 下一可执行任务：FG-109

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

### 2026-08-02：FG-102 完成

- 状态：完成
- 代码事实：
  - `RenderGraphResourceRegistry` 之前以 `RGTextureHandle` / `RGBufferHandle` 作为主索引。
  - 即使 graph 已经有 stable persistent key，registry 仍会把 persistent resource 当成 frame-local live set 一部分处理；
    创建顺序变化或本帧暂时未声明时，会丢失跨帧物理身份。
- 实现：
  - 为 persistent texture / buffer 新增 stable-key cache：
    - `key -> persistent entry`
    - `current graph handle -> bound entry`
  - 将 “persistent cache” 与 “current graph handle resolution” 分离。
  - 当前显式策略：
    - transient/imported：未出现在本帧 graph 中时按现有规则 prune
    - persistent：未出现在本帧 graph 中时只解除当前 handle 绑定，不销毁 key cache
  - spec 变化仍走安全 replacement，并通过 `DeferredDeletionQueue` 延迟退休旧 owner。
- 未做：
  - 这一步没有扩展 pass-scoped resolve/access validation；该项仍属于 `FG-103`
  - 没有实现 pipeline/presentation 级 scope owner；当前 persistent cache 生命周期仍与 registry 一致
- 测试：
  - `python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:ResourceStateTrackerTest.*'`
  - 结果：75 tests passed
  - 新增覆盖：
    - persistent resource 在创建顺序变化下仍复用
    - persistent resource 在中间一帧未声明后可重新绑定并复用
- artifacts：
  - `RenderGraphCoreTest.ResourceRegistryReusesStableResourcesAcrossSyncs`
  - `RenderGraphCoreTest.ResourceRegistryKeepsPersistentResourcesAcrossTemporaryOmission`
- 下一任务：
  - 进入 `FG-103`，为 pass-scoped resolve / access 增加 validation，避免 executor resolve 越权和 stale handle 静默通过

### 2026-08-02：FG-103 完成

- 状态：完成
- 实现：
  - `RGRenderContext::resolveTexture()` / `resolveBuffer()` 现在先检查资源是否由当前 pass 声明。
  - `copyBuffer()`、`copyTextureToBuffer()`、`copyTexture()` 额外检查 transfer source/destination access。
  - 新增 declaration/access query API，供 debug tooling 和测试复用同一套判定逻辑：
    - `hasDeclaredTextureUsage()`
    - `hasDeclaredBufferUsage()`
    - `hasDeclaredTextureAccess()`
    - `hasDeclaredBufferAccess()`
- 行为边界：
  - 非法 resolve/copy 在开发期通过 `YA_CORE_ASSERT` 报告 pass 名、handle 和声明 access。
  - 当前不把 descriptor binding、shader reflection 或 material binding 纳入本任务；这些属于后续 `FG-501+`。
- 测试：
  - `python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:ResourceStateTrackerTest.*'`
  - 结果：77 tests passed
  - 新增：
    - `RenderContextReportsDeclaredTextureAndBufferUsage`
    - `RenderContextReportsTransferAccessRequirements`
- 下一任务：
  - 进入 `FG-104`，收口 buffer range/state、host-write 和 dynamic capacity replacement contract

### 2026-08-02：FG-104 完成

- 状态：完成
- 实现：
  - `RGBufferUsage` 新增显式 `RGBufferRange{offset,size}`，pass 可以按范围声明 buffer hazard，而不是一律 whole-buffer。
  - buffer access 从含糊的 `read/write/readWrite` 拆成显式语义：
    - `uniformRead()`
    - `storageRead()`
    - `storageWrite()`
    - `storageReadWrite()`
    - `indirectRead()`
    - `transferSrc()/transferDst()`
  - compiler 现在按“编译后 pass kind + buffer access + normalized range”生成 `BufferResourceState`，并据此建立 range-aware dependency：
    - 非重叠 range 不再平白串行化
    - 重叠 read/write、write/read、write/write 形成依赖
    - `StorageReadWrite` 不再误报 read-before-write
  - Deferred / directional shadow / point shadow 的 graph buffer callsite 已迁到显式 access API，不再继续使用模糊 buffer `read()`。
- 行为边界：
  - 这一步只补单 queue 下的 range-aware hazard 和 state 语义，不扩展到多 queue ownership 或通用 hazard optimizer。
  - `HostWrite/HostRead` 仍主要体现在 imported buffer initial/final state；不会把 host 写入伪装成 graph pass access。
- 测试：
  - `python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:ResourceStateTrackerTest.*'`
  - 结果：80 tests passed
  - 新增：
    - `CompileTracksExplicitUniformAndStorageBufferStates`
    - `CompileDoesNotAddDependenciesForNonOverlappingImportedBufferRanges`
    - `CompileAddsDependenciesForOverlappingBufferRanges`
  - 额外烟测：
    - `xmake b ya-editor`
    - 结果：build ok
- 下一任务：
  - 进入 `FG-105`，定义 frame graph execution result/export owner，避免 graph 外继续隐式抓 registry 内部 owner

### 2026-08-03：FG-105 完成

- 状态：完成
- 代码事实：
  - `DeferredRenderPipeline`、`PostProcessingStage`、`BloomPostprocessing` 此前都在 graph prepare/execute 后直接回头查询 `RenderGraphExecutor` 内部 registry，把 graph-owned `RenderImage` owner 偷渡给 graph 外状态。
  - 这种模式让“哪些输出允许逃逸 graph”没有显式契约，调用方也必须知道 executor 内部有一个可查询 registry。
- 实现：
  - `RenderGraph` 新增显式 `exportTexture()` 声明；`RGCompiledGraph` 记录已验证的 exported texture plan。
  - 新增 `RenderGraphExecutionResult`，只发布按名称导出的 texture shared owner，不再把 registry 本身暴露为跨边界契约。
  - `RenderGraphExecutor::prepare()` / `execute()` 现在可同步产出 execution result；compile 阶段会拒绝无效 exported handle 和重复 export 名称。
  - Deferred 主图改为显式 export：
    - GBuffer Color0..3
    - GBuffer Depth
    - Viewport Color
    - SSAO Output（存在时）
  - `PostProcessingStage` / `BloomPostprocessing` 改为通过 `RenderGraphExecutionResult` 捕获 prepared resources；standalone execute 路径不再依赖 executor registry 内部查询。
- 未做：
  - 这一步没有删除 `RenderGraphExecutor::getRegistry()`；registry 仍保留给 graph 内部同步与现有低层测试使用，但不再是 Deferred/PostProcess 这条 owner 逃逸链的公开依赖。
  - 还没有把其他潜在 graph 外 consumer 全部迁到 exported-result 契约；本提交只收口当前已知的 Deferred/PostProcess/Bloom 主路径。
- 测试：
  - `python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:ResourceStateTrackerTest.*'`
  - 结果：82 tests passed
  - `xmake b ya-editor`
  - 结果：build ok
- artifacts：
  - `RenderGraphCoreTest.PrepareCapturesExplicitExportedTexturesOnly`
  - `RenderGraphCoreTest.ExportedTextureOwnerSurvivesReplacementAcrossPrepare`
- 下一任务：
  - 进入 `FG-106`，为 transient buffer 编译 first/last-use lifetime interval，给后续 physical slot allocation 提供确定性输入

### 2026-08-03：FG-106 完成

- 状态：完成
- 代码事实：
  - `RenderGraph` 之前已经会为 transient buffer 填 first/last pass index，但 `compiled.transientBufferLifetimes` 本身仍按 buffer 创建顺序输出。
  - 这会让“lifetime interval 是编译后真相源”这件事不完整：下游 physical slot allocation 还得自己重新整理顺序，branch/merge、explicit dependency、optional-unused 的确定性也没有被完整锁进测试。
- 实现：
  - compiler 现在在收集完 transient buffer 的 first/last use 后，按以下稳定规则排序 `compiled.transientBufferLifetimes`：
    - used 在前，unused 在后
    - used buffer 按 `firstPassIndex -> lastPassIndex`
    - 同区间再按 handle 稳定打破平局
  - 保持 imported/persistent buffer 不进入 transient lifetime plan，继续作为后续 alias allocator 的输入边界。
- 未做：
  - 这一步只提供 deterministic compiled lifetime interval；还没有生成 physical slot coloring / allocation plan，那是下一步 `FG-107`。
  - diagnostics 仍停留在 logical/used/unused 统计，还没有 physical reuse ratio。
- 测试：
  - `python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:ResourceStateTrackerTest.*'`
  - 结果：84 tests passed
  - `xmake b ya-editor`
  - 结果：build ok
- artifacts：
  - `RenderGraphCoreTest.CompileOrdersTransientBufferLifetimesByTopologicalUse`
  - `RenderGraphCoreTest.CompileTransientBufferLifetimesFollowExplicitDependenciesAndIgnoreNonTransientBuffers`
- 下一任务：
  - 进入 `FG-107`，基于 compiled lifetime interval 生成 transient buffer physical slot allocation plan

### 2026-08-03：FG-107 完成

- 状态：完成
- 代码事实：
  - FG-106 已经提供了按最终拓扑序排列的 transient buffer lifetime interval，但还没有把 logical buffer 映射到可供 registry 使用的 physical slot。
  - 当前后端资源工厂没有跨后端 alignment limits 查询，且 slot materialization 属于下一步 registry 工作；本任务不能硬编码 Vulkan 对齐常量或提前创建 GPU buffer。
- 实现：
  - `RGBufferDesc` 增加显式 `alignment` 契约，默认值为 1；RenderGraph 创建时拒绝零对齐。
  - compiler 对 used transient lifetime 执行确定性 first-fit coloring：只允许 lifetime 不重叠且 `memoryUsage` 相同的 logical buffer 复用 slot。
  - slot descriptor 的 `size` 取成员最大值，`usage` 取并集，`alignment` 取成员最大值；logical handle 到 slot 的 assignment 作为 compiled graph 的显式输出。
  - imported / persistent / unused buffer 不进入 slot assignment；保留现有 lifetime 统计。
  - debug dump 增加 assignment、slot descriptor 和 physical slot/byte/alias 诊断；`physicalReuse=compiler-plan` 明确表示尚未 materialize GPU owner。
  - persistent/registry buffer descriptor 比较纳入 alignment，避免契约变化静默复用旧资源。
- 未做：
  - 还没有让 `RenderGraphResourceRegistry` 创建或跨帧池化 physical slot buffer；这属于 `FG-108`。
  - 还没有 alias boundary barrier/state reset；这属于 `FG-109`。
- 测试：
  - `python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:ResourceStateTrackerTest.*'`
  - 结果：86 tests passed
  - 新增：
    - `CompileAllocatesDeterministicTransientBufferSlots`
    - `CompileDoesNotAliasOverlappingOrIncompatibleTransientBuffers`
  - 额外烟测：`xmake b ya-editor`
  - 结果：build ok
- artifacts：
  - `RGTransientBufferAssignment`
  - `RGTransientBufferSlotPlan`
  - `RGTransientBufferDiagnostics.physicalSlotCount/physicalBytes/aliasedBufferCount`
- commit：待提交
- 下一任务：
  - 进入 `FG-108`，让 registry materialize 并跨帧池化 physical buffer slots

### 2026-08-03：FG-108 完成

- 状态：完成
- 代码事实：
  - FG-107 只生成了 compiler slot plan；此前 executor 仍按每个 transient logical handle 各自创建 buffer，slot plan 没有进入运行时 owner 链。
  - registry 的旧 `sync(graph)` 调用仍被测试和 standalone 工具使用，不能让 registry 在没有 compiled graph 的情况下自行偷偷编译。
- 实现：
  - `RenderGraphExecutor::prepare()` 把同一次 compile 得到的 `RGCompiledGraph` 传给 registry；registry 只按 compiled slot plan materialize transient owner。
  - 同一 slot 的多个 logical handle 绑定同一个 `IBuffer`，未使用的 transient buffer 不创建 owner；imported/persistent 继续走原有路径。
  - registry 增加跨帧 transient pool：优先命中 `memoryUsage` 相同、capacity/usage/alignment 足够的 owner；capacity 或 memory class 不兼容时创建新 owner并保留旧 pool entry。
  - pooled transient owner 的释放只发生在 registry clear/destructor，handle projection 被 prune 时不会提前 retire GPU buffer。
  - 保留 `sync(graph, nullptr)` 兼容路径，未提供 compiled plan 时继续按 logical buffer materialize 原行为。
- 未做：
  - 还没有同一 physical slot 在 logical identity 切换时的 alias boundary barrier/state reset；这属于 `FG-109`。
  - pool 当前不做 LRU/容量上限回收；先保证 completion-safe reuse 和可测的跨帧 pool hit。
- 测试：
  - `python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:ResourceStateTrackerTest.*'`
  - 结果：88 tests passed
  - 新增：
    - `ResourceRegistryMaterializesOneBufferPerCompiledTransientSlot`
    - `ResourceRegistryReusesTransientBufferPoolAcrossFramesAndGrowsSafely`
  - 额外烟测：`xmake b ya-editor`
  - 结果：build ok
- artifacts：
  - `RenderGraphResourceRegistry::sync(const RenderGraph&, const RGCompiledGraph*)`
  - transient pooled owner materialization and reuse tests
- commit：待提交
- 下一任务：
  - 进入 `FG-109`，实现 buffer alias boundary barrier 与 state reset

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
