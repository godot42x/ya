# RenderGraph 清晰化进度

## 2026-08-02

- 建立 RenderGraph 清晰化计划。
- 基于当前代码复核了 `RenderGraph`、`RenderGraphExecutor`、`RenderGraphResourceRegistry`、`DeferredRenderPipeline` 和 `RenderRuntime`。
- 确认本轮真正的前置依赖在 graph/runtime seam，而不是 Scene、AssetManager、ResourceResolveSystem 或 render thread。
- 将计划调整为 Gate 0 / Gate 1 / Gate 2 / Gate 3，避免在执行语义闭环前引入高层 DSL、aliasing 或 async compute。

## 2026-08-02：执行前审查

- 发现本计划与 `frame-graph-orchestrator-migration` 存在职责重叠，已改为 graph core 子计划。
- 修正 Forward 状态描述：主 surface 仍未 graph 化。
- 修正 transient aliasing 定位：本计划只提供编译产物 seam，物理 slot/reuse 由 FG-106~FG-110 负责，且仍是完整迁移的硬门禁。
- 删除首轮对 `ComputeDispatchPlan` / `CopyPlan` 的强假设，要求先有真实 consumer。
- 增加主计划任务映射和双 owner 防护。

## 2026-08-02：Gate 0 完成

- 完成 `RG-0001~RG-0005`。
- 新增 `runtime-boundary-inventory.md`，记录 Deferred、Forward、Presentation、Offscreen 的真实调用链。
- 确认 `OffscreenTaskService` 拥有独立 command buffer/submit/fence completion boundary，保持 utility 边界，不并入 world executor。
- 确认主计划 `FG-001/FG-002` 的运行时 graph dump、固定机位、draw count 和 validation 证据仍未完成，因此不提前标记主计划任务完成。

## 2026-08-02：Phase 0 审计完成

- 完成 `RG-0101~RG-0103`。
- 新增 `contract-inventory.md`，覆盖 Deferred、Forward、SSAO、Postprocess、Presentation、Shadow。
- 确认首个代码切片应只处理 imported finalization contract 和 core tests。
- 确认 stable key、transient physical reuse、upload arena、Stage owner 迁移分别由主计划持有，不在首个提交混入。
- `RG-0104` 尚未开始，下一步先定位现有 RenderGraph core test target 和可复用的 fake command buffer seam。

## 2026-08-02：第一批代码切片完成

- 完成 `RG-0104`。
- 修改 `RenderGraphExecutor`：`executeCompiled()` 现在负责 imported buffer/texture finalization，`execute()` 退化为 wrapper。
- 为 `executeCompiled()` 新增 imported buffer final state 与 imported texture final layout 回归测试。
- 保持首批切片边界：未引入 stable key、per-pass compiled plan、transient aliasing、upload arena 或 pipeline 迁移。
- 验证：
  - `python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:ResourceStateTrackerTest.*'`
  - 结果：63 tests passed
- 下一步：进入 Gate 1 剩余工作，检查 Deferred 主图是否还存在额外依赖 `execute()` wrapper 语义的隐式假设，然后决定是否直接进入 per-pass compiled plan 前的 debug dump/diagnostics 收口。

## 2026-08-02：第二批代码切片完成

- 完成 `RG-0201~RG-0204`，并提前完成 `RG-0304`。
- 修改 `RenderGraph` / `RenderGraphExecutor`：
  - imported texture/buffer finalization 被编译进 `RGCompiledGraph`
  - executor finalization 不再扫描原始 graph resource 列表，而是消费 compiled finalize plans
  - `debugDump()` 现在会输出 `importedTextureFinalizes` / `importedBufferFinalizes`
- 新增 RenderGraphCore tests：
  - `CompileBuildsImportedFinalizePlans`
  - `DebugDumpIncludesImportedFinalizePlans`
  - `ExecuteCompiledRestoresImportedBufferFinalStateAfterTransferPass`
  - `ExecuteCompiledRestoresImportedTextureFinalLayoutAfterTransferPass`
- 验证：
  - `python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:ResourceStateTrackerTest.*'`
  - 结果：65 tests passed
- 下一步：检查 Gate 1 的 runtime caller 证据是否足够关闭 `RG-0205`；若不足，先做针对 Deferred/Forward/Presentation 的轻量行为验证，再进入 `RG-0301~RG-0303/0305/0306`。

## 2026-08-02：第三批代码切片完成

- 完成 `RG-0205` 与 `RG-0301~RG-0303/0305/0306`。
- 修改 `RenderGraph` / `RenderGraphExecutor`：
  - 新增 `RGCompiledPassPlan`
  - texture/buffer state plan 在 compile 阶段即按 pass 分桶
  - executor 不再按 `compiled.order + 全局 state 数组` 双重遍历，而是顺序消费 `compiled.passPlans`
  - `debugDump()` 改为输出 pass-local state plan
- 新增 / 更新 RenderGraphCore tests：
  - `ExecuteWrapperMatchesPrepareAndExecuteCompiledForImportedFinalizeContract`
  - 原有 compile/debug tests 改为校验 pass-local compiled plan
- Gate 1 caller 证据：
  - Deferred 主图当前走 `prepare() + executeCompiled()`
  - screenshot copy graph 等 wrapper caller 仍走 `execute()`
  - parity test 覆盖了两条入口在 imported finalize contract 上的执行一致性
- 验证：
  - `python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:ResourceStateTrackerTest.*'`
- 结果：66 tests passed
- 下一步：进入 Phase 3，收敛 raster declaration 与 execute callback 间的双重真相。

## 2026-08-02：第四批代码切片完成

- 完成 `RG-0401` 与 `RG-0402`。
- 修改 `RenderGraph` / `RenderGraphExecutor`：
  - 新增 `RGRasterPassDesc`、`RGColorAttachmentDesc`、`RGDepthAttachmentDesc`
  - `RGPassBuilder::declareRaster()` 负责在 declaration 阶段声明 raster attachment/render area/layer count，并同步建立 attachment usage
  - `RGCompiledPassPlan` 现在携带 `rasterPlan`
  - `RGRenderContext::beginDeclaredRasterRendering()` 从 compiled pass plan 消费声明好的 raster plan
- 真实 consumer 迁移：
  - Deferred 本地图内 `GBuffer / Light / Skybox / Scene Overlay / Viewport Overlay`
  - Forward graph-backed `Forward Viewport`
- 新增 RenderGraphCore tests：
  - `CompileStoresDeclaredRasterPlanInCompiledPassPlan`
  - `ExecutorCanBeginDeclaredRasterRenderingFromCompiledPassPlan`
- 验证：
  - `python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:ResourceStateTrackerTest.*'`
  - `xmake b ya-engine`
  - 结果：68 tests passed；`ya-engine` build ok
- 剩余工作：
  - `RG-0403` 仍未完成：execute callback 还没有 typed pass parameters，只是先移除了 attachment/rendering 描述的双重真相
  - `RG-0404` 只完成了 Deferred 本地 pass 与 Forward viewport 的首批 consumer，Postprocess / SSAO / utility pipeline 仍待迁移

## 2026-08-02：第五批代码切片完成

- 完成 `RG-0403` 与 `RG-0404`。
- 修改 `RenderGraph` / `RenderGraphExecutor`：
  - 新增 `RGRenderContext::RasterPassExecutionParams`
  - execute callback 现在可以从 `RGRenderContext::getRasterPassExecutionParams()` 读取 compiled raster plan，而不是依赖外部 capture 的 attachment/renderArea 真相
  - `getDeclaredRasterPlan()` / `getRasterPassExecutionParams()` 形成了当前阶段的 typed pass parameter seam
- 真实 consumer 追加迁移：
  - `BloomPostprocessing`
  - `SSAOStage`
  - `PostProcessingStage`
  - `RenderRuntimeFrame` presentation pass
  - 同时把 Forward / Deferred 已迁移 pass 的 execute 侧改为消费 typed raster params
- 新增 RenderGraphCore test：
  - `ExecutorExposesTypedRasterExecutionParamsFromCompiledPlan`
- 验证：
  - `python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:ResourceStateTrackerTest.*'`
  - `xmake b ya-engine`
  - 结果：69 tests passed；`ya-engine` build ok
- 说明：
  - 这一步仍然保持现有 `RGRenderContext&` callback 形态，没有直接改成新的 callback 签名；typed params 先通过 context view 暴露，避免本轮把所有 graph caller 一次性重写

## 2026-08-02：第六批代码切片完成

- 完成 `RG-0501~RG-0504`。
- 修改 `RenderGraph` / `RenderGraphExecutor`：
  - 新增 `ERGPassKind`
  - `RGCompiledPassPlan` 现在显式保存 `kind`
  - compiler 会为 pass 解析 `Raster / Compute / Copy` kind，并执行 kind-specific validation
  - `debugDump()` 和 compile issue 现在会输出 pass kind 与 `InvalidPassKind`
- builder 入口扩展：
  - 新增 `RGPassBuilder::declareCompute()`
  - 新增 `RGPassBuilder::declareCopy()`
  - `declareRaster()` 现在会同步设置 `ERGPassKind::Raster`
- 真实 consumer 覆盖：
  - screenshot copy graph 显式声明为 Copy
  - point shadow cull pass 显式声明为 Compute
  - `PBRGenerateBrdfLUT` 迁移到 raster declaration + typed raster params
- 新增 / 更新 RenderGraphCore tests：
  - `CompileInfersCopyPassKindForTransferOnlyPass`
  - `CompileRejectsCopyPassWithNonTransferUsage`
  - `CompileBuildsStableDependencyOrder` 增加 pass kind 断言
  - `DebugDumpIncludesPassOrderDependenciesAndIssues` 增加 kind 输出断言
- 验证：
  - `python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:ResourceStateTrackerTest.*'`
  - `xmake b ya-engine`
  - 结果：71 tests passed；`ya-engine` build ok
- 说明：
  - 这一轮的 kind 模型带兼容层：未显式声明 kind 的旧 pass 仍会在 compile 阶段解析出有效 kind，避免一次性打爆所有既有 caller
  - 像 `PointShadowPass` 这类一个 pass 内多次 begin rendering 的特殊路径，暂时保留兼容模型，不强推单一 `rasterDesc`
