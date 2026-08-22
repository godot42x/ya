# 渲染管线去重 + RenderRuntime 服务化拆分

> 建立日期：2026-08-22
> 状态：规划中（未动代码）
> 目标：消除 Forward/Deferred 两套渲染管线的平行重复；把 `RenderRuntime` 上帝类按职责 Service 化，逐步瘦身为协调器。

## 0. 结论摘要

本次是「架构债治理」而非「功能开发」。两条主线彼此独立、可并行推进，但都遵循两条铁律：

> **铁律 A：只做结构迁移，不改行为**。任何一步都必须保证「迁移前后渲染结果与资源生命周期不变」，以「构建通过 + 现有测试全绿 + 关键示例可运行」为验收底线。
>
> **铁律 B：只抽「资源/数据的公共机制」，不抽「渲染策略的公共骨架」**。Forward / Deferred 是两种不同渲染策略，其编排流程（tick / shadow state / pass 顺序）语义不同、且样本量只有 2，抽象会锁死未来第三条管线（光追 / clustered forward 等），重蹈 UE 式僵化。**不做 `IBasePipeline`。**

三条主线的优先级：

1. **Pipeline 无状态工具抽取**（低风险、立即见效）
2. **PerFlightFrameResourceSet 资源机制基类抽取**（中风险、消除样板）
3. **RenderRuntime 服务化拆分**（影响面大、需分阶段）

## 1. 现状与问题

### 1.1 Forward / Deferred 平行重复

两条管线都实现 `IRenderPipeline` 三接口，但公共骨架没有抽出共享层，导致「接口相同 + 实现平行复制」。

已确认的重复点，按「是否该抽」分类：

**✅ 该抽（无歧义的资源/数据机制，非渲染策略）**：

- `makeShadowDebugResource`：`ForwardRenderPipeline.cpp:20-34` 与 `DeferredRenderPipeline.cpp:36` **逐字重复**（纯工具，构造 debug resource 对象，无语义分叉）。
- `ensureSkinningCapacity` / `calculateSkinningCapacity`：两套 `FrameResourceSet` 的 skinning 上传内存容量管理逻辑几乎相同（内存分配机制，非策略）。

**❌ 不该抽（渲染策略骨架，抽了锁死）**：

- `shouldSkipTick`：两管线看似同为「断言 cmdBuf + 检查 app stopped」，但语义随策略演化，未来 Forward/Deferred 的 tick 前置条件可能分叉。
- `buildShadowState` / `captureShadowSettings` / `syncShadowSettings`：私有方法签名"看似"一一对应，实为两条管线的 shadow 状态机，语义不同。
- `buildViewportRenderTargetSpec` / `buildForwardViewportRenderTargetSpec`：RT 规格由各管线的 pass 决定，属于策略。
- `ForwardFrameGraphOrchestrator` vs `DeferredFrameGraphOrchestrator`：`build()` 编排的是各管线自己的 graph，BuildDependencies/BuildInputs 本就是策略差异，**不抽共享基类**。

### 1.2 RenderRuntime 上帝类

`RenderRuntime.h`（324 行）一个 struct 承载 4 类职责，成员约 40 个（第 143-184 行），cpp 已拆成 5 个文件但**类本身没拆**：

- `RenderRuntime.cpp` — 生命周期
- `RenderRuntimeFrame.cpp` — 帧编排
- `RenderRuntime.Resources.cpp` — 资源管理
- `RenderRuntimeViewportDebug.cpp` — 调试目录（27 KB）
- `RenderRuntimeViewportSnapshot.cpp` — viewport 快照

已抽成独立 Service 的雏形（方向正确、未走完）：`_sharedResourceProvider`、`_diagnostics`、`_offscreen`、`_environmentLightingProcessor`、`_terrainProcessor`、`_gameplayResourceBinding`。

待 Service 化的内聚状态块：

| 职责块 | 当前散在 RenderRuntime 的成员 | 目标 Service |
|---|---|---|
| 管线管理 | `_forwardPipeline` / `_deferredPipeline` / `_renderPipeline` / `_pendingRenderPipeline` / `_pendingActivePipelineReload` / `initForwardPipeline` / `initDeferredPipeline` / `applyPendingRenderPipelineSwitch` | `PipelineCoordinator` |
| Presentation 图 | `_presentationGraphExecutors` / `_presentationImages` / `_presentationPostProcessor` / `_presentationPostProcessState` / `rebuildPresentationImages` / `renderPresentationPass` | `PresentationGraphService` |
| Viewport 状态 | `_viewportRect` / `_viewportFrameBufferScale` / `_bWorldSceneRenderEnabled` / `ensureViewportRectInitialized` | 并入 viewport 相关 Service |

## 2. 硬规则

1. **只做结构迁移，不改行为**。禁止顺手「优化」渲染逻辑、禁止改变 pass 顺序、禁止改变资源创建/释放时机。
2. **接口层不变**：`IRenderPipeline`、`IRenderRuntimeServices` 对外签名在拆分过程中保持不变，App 层无感。
3. **不平行造新接口**：复用现有 Service 模式（`_offscreen`、`_environmentLightingProcessor` 等），不引入第二套 Service 基类约定。
4. **retained lifetime 必须复查**：任何 owner 挪动都要确认 `retainedResources` / `retireResource` 语义不丢（参见 `resource-system` skill 与本轮 memory）。
5. **分阶段小步提交**：每个 phase 单独 commit，commit message 用 `[render]` / `[render-graph]` 前缀，与 AGENTS.md 约定一致。

## 3. 实施阶段

### Phase 1 — Pipeline 无状态工具抽取（低风险）

目标：只消除**逐字重复的无歧义纯工具**，不碰任何渲染策略方法。

范围：

- 新增 `PipelineCommon.h/.cpp`（或纳入 `Common/` 现有目录），只收纳：
  - `makeShadowDebugResource`
- Forward / Deferred 两处调用点改为引用公共实现。
- **明确不做**：`shouldSkipTick` / `buildShadowState` / `captureShadowSettings` / `syncShadowSettings` / viewport RT spec 构建（属策略，见 1.1 ❌ 分类）。

完成标准：

- `makeShadowDebugResource` 全仓库只剩一份定义。
- `xmake b ya-render-3d` 通过。

### Phase 2 — PerFlightFrameResourceSet 资源机制基类抽取（中风险）

目标：只消除两套资源集的**资源/内存管理样板**（非渲染策略）。

范围：

- 抽出 `PerFlightFrameResourceSetBase`，只收纳**资源机制**：
  - `_uploadArena` + skinning descriptor set layout/pool
  - `ensureSkinningCapacity` / `calculateSkinningCapacity`
- Forward / Deferred 各自派生，保留各自的 descriptor 集合、`Binding` 数组与 pass 相关状态。
- **明确不做**：把两套资源集的 descriptor 集合 / `Binding` / pass 状态也抽进基类（那是策略差异，抽了锁死）。

完成标准：

- 两条管线资源集逻辑不变，测试全绿。

### Phase 3 — RenderRuntime 服务化拆分（影响面大）

目标：把 RenderRuntime 从上帝类瘦身为协调器。

范围（按依赖顺序，每小步独立提交）：

- 3.1 `PipelineCoordinator` Service：接管管线选择/切换/重建。**注意**：它只负责"当前激活哪条管线、何时切换、如何重建"的**协调状态**，不定义 Forward/Deferred 的**渲染策略接口**（策略仍由各 Pipeline 类各自实现，`PipelineCoordinator` 只持有 `IRenderPipeline*`，不为两者强行统一骨架）。
- 3.2 `PresentationGraphService`：接管 presentation 图与 postprocess。
- 3.3 Viewport 状态收敛：并入现有 viewport 相关 Service。

完成标准：

- `RenderRuntime` 只保留协调与转发，无管线/presentation/viewport 的直接状态。
- `IRenderRuntimeServices` 对外签名不变。

## 4. 非目标

- 不重写 RenderGraph 调度算法。
- 不重写 Vulkan/OpenGL backend 对象模型。
- 不做 Forward/Deferred 的「统一为单一路径」合并（两者是不同渲染策略，保留并行）。
- **不抽象 `IBasePipeline` / 不把 Forward/Deferred 的编排流程统一成公共骨架**（样本量仅 2，抽象即锁死）。
- 不顺手做材质/纹理 asset 系统扩张。

## 5. 验证策略

每个 phase 至少：

- `xmake b ya-render-3d`
- `xmake b ya-render-graph`
- `xmake b ya-game-runtime`
- 跑 `ya-testing` 相关测试（RenderGraphCoreTest 等）

关键示例可运行验收：

```bash
python3 Script/ya.py run --project Example/HelloMaterial/HelloMaterial.yaproject
```

静态审计目标：

- `makeShadowDebugResource` / `ensureSkinningCapacity` / `calculateSkinningCapacity` 只存在一份。
- `RenderRuntime` 成员数量下降，管线/presentation/viewport 状态不再直接挂在 Runtime 上。
- `shouldSkipTick` / `buildShadowState` / `captureShadowSettings` / viewport RT spec 等**策略方法仍各自独立**，未被强行合并。
- 无新增 `RenderImage` / raw `IImageView` 公开泄漏（呼应 rhi-image-owner-convergence 计划的边界）。
