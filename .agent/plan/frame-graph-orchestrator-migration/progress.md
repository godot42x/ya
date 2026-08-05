# Frame Graph 顶层编排与资源收口进度

## 当前状态

- 计划建立日期：2026-07-18
- 当前阶段：P7 Forward 全量迁移
  - 当前执行任务：FG-707（Forward graph structure tests 和 pipeline switch matrix）
  - 下一架构任务：FG-707

### 2026-08-05：FG-706 完成

- 实现：
  - 新增 `ForwardFrameGraphOrchestrator`（h/cpp），Forward 顶层图构建整体从
    `executeViewportPassGraph` 迁出：shadow subgraph、Skybox/PBR/Phong/Unlit/
    Simple/Direction/Debug/Viewport Overlay 八个 viewport pass、bloom/finalize
    postprocess 全部由 orchestrator 按固定顺序构建。
  - `executeViewportPassGraph` 缩为薄封装：构建 direction gizmo 快照 -> 组装
    BuildDependencies/BuildInputs -> orchestrator.build -> executor
    prepare/execute -> 按 `PostProcessingStage::kOutputExportName` 发布
    `_currentPostprocessOutput`。
  - 新增 `describeTopology()`（与 DeferredFrameGraphOrchestrator 同构），
    为 FG-707 结构测试提供 pass order / dependency 描述。
  - 一个 world-frame executor：Forward 帧链只有 `_graphExecutor` 一个执行器；
    `executePasses` 固定顺序此前已在 FG-702 删除，本任务确认无残留。
- 未做：
  - `applyPendingResourceRefreshes()`（resize / RT format / shadow refresh）
    按 2026-07-16 旧计划复核结论保留——它是真实资源刷新 orchestration，
    不是 compat 外壳。
  - `ForwardViewportStage::execute(ctx)` conformance stub 保留（IRenderStage
    纯虚要求），与 Deferred 各 stage 的 stub 语义一致。
- 测试：
  - `xmake b ya-engine` / `xmake b ya-editor` 通过。
  - 定向单测 103 tests passed。
  - GUI smoke 仍受无窗口会话限制，待有 GUI 环境补跑。

### 2026-08-05：FG-705 完成

- 实现：
  - Forward 删除图外 `finalizeViewportPass`（原 `_postProcessStage.execute`
    standalone 路径）与 tick 中的 FinalizeViewport 阶段；postprocess 收进
    Forward 主图。
  - graph 尾部按 Deferred 同款 common contract 追加：
    `appendBloomGraphPasses` -> `appendFinalizeGraphPasses`；postprocess 输入
    为 MSAA resolve 目标（无 MSAA 时用 color），finalize 自建输出纹理并
    export 为 `PostProcessingStage::kOutputExportName`。
  - `executeViewportPassGraph` 改走带 `RGCompiledGraph` /
    `RenderGraphExecutionResult` 的 executor execute，`_currentPostprocessOutput`
    由 execution result 按导出名捕获，不再依赖 `getPreparedOutputImageShared()`。
  - postprocess 输入尺寸从 viewport resource extent 取，与图内一致（替代旧的
    `viewportRect.extent` 直接传参）。
- 未做：
  - `PostProcessingStage::execute(...)` standalone 入口仍保留给 Deferred /
    utility 兼容路径，删除属于 FG-901。
- 测试：
  - `xmake b ya-engine` / `xmake b ya-editor` 通过。
  - 定向单测 103 tests passed。
  - GUI smoke 仍受无窗口会话限制，待有 GUI 环境补跑。

### 2026-08-05：FG-704 完成

- 实现：
  - Forward 顶层图进一步拆分，序列变为 Skybox -> PBR -> Phong -> Unlit ->
    Simple -> Direction -> Debug -> Viewport Overlay，每个逻辑 pass 在 graph
    dump 中独立可见。
  - 新增 typed params：`ForwardSimplePassParams` / `ForwardDirectionPassParams` /
    `ForwardDebugPassParams` / `ForwardViewportOverlayPassParams`；删除
    `ForwardRestPassParams`。
  - Viewport Overlay 成为最后一个 pass，独占 MSAA resolve attachment、最终
    consumer layout 与 editor viewport overlays 回调。
  - Direction gizmo 改为 graph 构建前从 activeScene 预建
    `ForwardDirectionGizmoInput` 快照（镜像 Deferred overlay 快照模式），
    aux pass 只消费快照，不再在 execute 期查询 ECS。
- 未做：
  - Skybox 的 scene 输入仍经 runtime services（`getSceneSkyboxDescriptorSet` /
    resource resolve）在 stage prepare 期解析，与 FG-702 验收一致；
    环境光照 descriptor 仍由 stage execute 时查询（P5 binding 收口范围）。
- 测试：
  - `xmake b ya-engine` / `xmake b ya-editor` 通过。
  - 定向单测 103 tests passed。
  - GUI smoke 仍受无窗口会话限制，待有 GUI 环境补跑。

### 2026-08-05：FG-703 完成

- 实现：
  - Forward 顶层图新增独立 `Forward Unlit` graph pass，位于
    Phong 之后、Rest 之前，序列变为 Skybox -> PBR -> Phong -> Unlit -> Rest。
  - 新增 `ForwardUnlitPassParams`（`ForwardFrameGraphResources.h`），
    setup/execute 共用同一 params，layout 在 pass 链中保持
    `ColorAttachmentOptimal`。
  - `ForwardViewportStage` 新增 `executeUnlit(ctx, Binding)` per-pass 入口；
    `executeRest(ctx)` 只保留 Simple / Direction / Debug + editor overlays，
    因这些子 pass 不消费 per-flight binding，签名不再携带 Binding。
- 未做：
  - Simple / Direction / Debug 仍合并于 Rest pass，独立为 graph pass 是 FG-704。
- 测试：
  - `xmake b ya-engine` / `xmake b ya-editor` 通过。
  - 定向单测 103 tests passed（RenderGraphCore / ResourceStateTracker /
    Deferred 系列 / AppScreenshotCapture）。
  - GUI smoke 仍受无窗口会话限制，待有 GUI 环境补跑（同 FG-002 基线）。

### 2026-08-05：FG-702 完成

- 实现：
  - Forward 顶层图从单个 `Forward Viewport` pass 拆成四个独立 graph pass：
    `Forward Skybox`（首个 pass，负责 clear）-> `Forward PBR` -> `Forward Phong`
    -> `Forward Rest`（Unlit/Simple/Direction/Debug + editor viewport overlays，
    最后一个 pass，独占 MSAA resolve attachment）。
  - 新增 typed pass params（`ForwardFrameGraphResources.h`）：
    `ForwardSkyboxPassParams` / `ForwardPBRPassParams` / `ForwardPhongPassParams` /
    `ForwardRestPassParams`；每个 params 驱动对应 pass 的 setup 与 execute。
  - `ForwardViewportStage` 暴露 per-pass 入口（`executeSkybox/executePBR/
    executePhong/executeRest`），删除 `execute(ctx, Binding)` 组合入口与
    `executePasses()` 固定顺序循环；`execute(ctx)` 变为 conformance stub，
    不再隐藏 pass 顺序。
  - attachment 链在 Skybox/PBR/Phong 之间保持 `ColorAttachmentOptimal`，
    Rest 应用 `finalLayout`（imported 图在 graph 结束统一 finalize 到
    ShaderReadOnlyOptimal）；editor overlays 随 Rest pass 录制。
- 未做：
  - Forward frame/light/skinning buffer 的 graph 内 resolve/binding 收口
    仍属于后续统一 binding 工作（P5/FG-501 风格），本任务不改 descriptor contract。
  - Unlit/Simple/Direction/Debug 仍合并在 Rest pass 中，独立为 graph pass 是 FG-703。
- 测试：
  - `xmake b ya-engine` / `xmake b ya-editor` / `xmake b ya-testing` 通过。
  - `python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:
    ResourceStateTrackerTest.*:DeferredRenderPipelineTest.*:
    DeferredFrameResourceSetTest.*:SSAOStageTest.*:DeferredPassParamsTest.*:
    AppScreenshotCaptureTest.*'` 103 tests passed。
  - GUI smoke 受当前无窗口会话限制未能执行（SDL 启动即退出 133）；构建与
    graph 结构由编译期检查和既有单测覆盖，真实截图/validation 冒烟待有 GUI
    会话环境补跑（同 FG-002 基线）。

### 2026-08-05：FG-601 完成

- 实现：
  - presentation capture 从图外裸 `recordPresentationCapture(cmdBuf)` 收进 presentation 主图，
    通过 `RenderRuntime::FrameInput::AutomationInput::appendPresentationCapture(graph, output, extent)`
    在 graph 构建阶段 append copy/readback pass。
  - `AppScreenshotCapture::appendPresentationCapture()` 校验 pending 状态、presentation 源图
    extent/format 一致后，向同一 `RenderGraph` 添加
    `AutomationScreenshot.PresentationCopy`（transferSrc + transferDst + copyTextureToBuffer）。
  - `AppAutomation` / `AppAutomationControlService` 改为 `appendPresentationCapture(...)` 入口，
    不再向引擎暴露裸 command-buffer 回调。
  - presentation 输出图 import 显式合并 `TransferSrc`；swapchain `bEnableTransferSrc` 改为
    恒定开启，保证运行时 control-port 请求 presentation 截图也可用（不依赖启动参数）。
- 未做：presentation 与 world frame 的 executor 合并（见 FG-602）。
- 测试：`RenderGraphCoreTest.*:DeferredRenderPipelineTest.*:SSAOStageTest.*:
  DeferredPassParamsTest.*:DeferredFrameGraphOrchestratorTest.*:DeferredFrameResourceSetTest.*:
  AppScreenshotCaptureTest.*` 96 tests passed。
- commit：`[runtime/capture] declare presentation readback in graph`
- 下一任务：FG-602（plan-only 调查）。

### 2026-08-05：FG-602 完成（plan-only）

- 代码事实：
  - presentation 资源按 swapchain image 持有独立 `RenderGraphExecutor`
    （`_presentationGraphExecutors`，size == swapchain image count），并在 swapchain recreate
    （extent / image / present mode 变化）时整体 rebuild。
  - presentation pass 输入为当前 swapchain image（imported `PresentSrcKHR`），
    输出经 presentation post-processor 后进入 present；world frame 的
    Deferred/Forward pipeline 只输出 viewport/postprocess 图像，二者共享的是
    `getPostprocessOutputImageShared()` 快照，不是同一个 executor。
  - ImGui / UI 在 viewport overlay 阶段录制，presentation 阶段只有
    `recordPresentationExtensions`（模块扩展）与现在的 graph 内 capture。
- 决策：**保持 presentation 独立 executor**。
  - swapchain identity / multi-image（每 swapchain image 一个 executor）是 presentation
    特有的 scope；并入 world-frame executor 需要把 acquire/present 生命周期和
    swapchain recreate 语义全部拉进 pipeline，复杂度大于收益。
  - FG-601 已经消除了“图外裸 capture”，presentation 图本身完整可见；
    没有可删除的真实重复状态。
- 未做：不改变 presentation 独立 executor；后续 FG-603 只做顶层流程收口与文档化。
- commit：plan-only（本进度文件）
- 下一任务：FG-603。

### 2026-08-04：shadow pipeline-switch 回归修复

- 根因：Forward viewport graph 只声明了 shadow pass 的 depth attachment 写入，没有声明 viewport 内
  PBR/Phong 对 shadow atlas 的 sampled read；因此 imported finalization 发生在 viewport draw 之后，
  atlas 的部分层仍处于 `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`。
- 修复：Forward viewport graph 导入整张 shadow atlas 并在 viewport pass 中声明 `read()`，让同一张图
  在 shadow pass -> viewport pass 之间生成完整的 sampled-layout 转换；移除此前为规避问题而加入的
  `ShadowMapResources::clearAndPrimeDepthImage()` isolate 初始化补丁。
- 验证：`xmake b ya-editor` 通过；96 个 RenderGraph/ResourceState/Deferred/SSAO 定向测试通过；
  control-port 真实 `Deferred -> Forward` 切换、viewport screenshot 和 graceful quit 通过，
  info/error 会话未出现 `VUID-vkCmdDraw-None-09600`、`Validation Error` 或 `VK_ERROR`。

### 2026-08-04：计划现状整理

- 主计划 owner 仍是本目录；`render-resource-and-graph-refactor` 保留为历史基线/资源模型收口记录，
  `render-graph-clarity-refactor` 保留为 graph core 子计划归档，二者都不再作为新的实现主入口。
- `AppAutomationControlService` 的真实 `Deferred -> Forward` 切换 smoke 曾暴露出
  `Shadow Map Depth` 的 sampled layout / subresource tracking 回归；该回归已在上面的
  `2026-08-04：shadow pipeline-switch 回归修复` 中解决并完成验证。
- 因此 P3 可以继续推进 `FG-304`；正式的截图/hash pipeline-switch 基线仍由 `FG-002` 补齐，
  但不再把已修复的 shadow validation 回归当作架构任务的运行阻塞。
- 对计划文件的影响：
  - 本目录继续作为唯一主 TODO / progress；
  - 旧 `render-resource-and-graph-refactor` 中“pipeline-switch smoke 已确认稳定”的表述需要降级为历史 CLI 证据，
    不能再当作当前有效基线；
  - `render-graph-clarity-refactor` 与 `render-architecture-refactor` 当前口径基本正确，无需再扩散新待办。

### 2026-08-03：FG-301 开始

- 代码事实：`DeferredRenderPipeline::executeDeferredMainGraph()` 仍在函数体内分别创建 frame/light/skinning/SSAO/skybox buffer handle、GBuffer/viewport/environment/shadow texture handle，并把多个 handle 通过局部 lambda 捕获。
- 代码事实：graph prepare 后，SSAO export 仍被转换为 `_currentSSAOOutput` 再写回 `LightStage`；本任务只收口 handle 的 frame-local 组织，不提前改变该 descriptor contract。
- 提交边界：新增 `DeferredFrameGraphResources` 值类型，集中保存本帧 RG buffer/texture/pass handles；不保存 resolved GPU pointer，不改变 pass 顺序、shader、descriptor binding 或 execution result 生命周期。

### 2026-08-03：FG-301 完成

- 实现：新增 `DeferredFrameGraphResources`（`Engine/Source/Runtime/Rendering/Deferred/DeferredFrameGraphResources.h`），
  集中保存本帧 Deferred 图的 buffer/texture/pass handles；可选资源（SSAO、environment、shadow depth、
  bloom、postprocess 输出、shadow/gbuffer/light/skybox/overlay pass）全部显式 `std::optional`；
  不保存 resolved GPU pointer。
- `DeferredRenderPipeline::executeDeferredMainGraph()` 改为以 `graphResources` 为 handle 单一出口：
  GBuffer / Light / Skybox / Scene Overlay / Viewport Overlay / bloom / postprocess 全部消费 `graphResources`
  中的 handle，不再散落局部 handle 命名。
- 新增单测 `DeferredFrameGraphResourcesTest.KeepsOptionalInputsExplicitAndHandlesFrameLocal`，
  验证 optional 默认值与 frame-local handle 的 index/generation 语义。
- 测试：`python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:ResourceStateTrackerTest.*:DeferredRenderPipelineTest.*:DeferredFrameResourceSetTest.*:SSAOStageTest.*'`
  通过（96 tests）；`xmake b ya-engine` 通过。
- 未做：不改变 `setSSAOTexture`/export 回灌契约；`frame import result` 作为 FG-302~FG-306 typed
  pass params 的前置类型，本任务只收口 handle 组织。
- 提交：`[runtime/deferred] add typed frame graph resources`（本提交）

### 2026-08-03：FG-302 完成

- 实现：新增 `DeferredGBufferPassParams`（`DeferredFrameGraphResources.h`），同一对象驱动 GBuffer
  graph pass 的 setup（uniformRead/storageRead/declareRaster）与 execute（resolve + binding）：
  - `GBufferStage` 删除 `_frameInputs`/`setFrameInputs`，`execute(ctx, FrameInputs)` 显式接收
    current-flight binding，draw 方法只消费传入的 `FrameInputs`；
  - `DeferredRenderPipeline::executeDeferredMainGraph()` 从 `frameBinding` + `graphResources` 构造
    `gbufferParams`，build/execute 两个 lambda 只捕获它；
  - GBuffer pass execute 对 declared 的 frame/light/skinning handle 调用 `rgCtx.resolveBuffer`
    （FG-103 resolve validation + imported retain）；
  - `updateStageFrameInputs()` 不再给 GBufferStage 预置 frame inputs（LightStage/OverlayStage 保留）。
- 新增单测 `DeferredGBufferPassParamsTest.DefaultsAreEmptyAndHandlesRemainFrameLocal`。
- 测试：`python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:ResourceStateTrackerTest.*:DeferredRenderPipelineTest.*:DeferredFrameResourceSetTest.*:SSAOStageTest.*'`
  通过（97 tests）；`xmake b ya-engine`/`xmake b ya-editor` 通过；
  `python3 Script/ya.py run-editor --project Example/HelloMaterial/HelloMaterial.yaproject -- --exit-after-frame=300 --log-level=warn --log-detail-level=error`
  exit 0，日志无 Validation Error/VUID/VK_ERROR。
- 未做：LightStage/SSAOStage/OverlayStage 仍用 stage 成员预置 frame inputs（FG-303/304）；
  descriptor binding 尚未走 `RGPassBindingContext`（P5/FG-501）。
- 提交：`[runtime/deferred] parameterize gbuffer graph pass`（本提交）

### 2026-08-03：FG-303 完成

- 实现：
  - 新增 `DeferredSSAOPassParams` / `DeferredLightPassParams`（`DeferredFrameGraphResources.h`），
    SSAO/Light 两个 graph pass 的 setup 与 execute 共用同一 params。
  - `LightStage`：删除 `_ssaoTextureOwner`/`setSSAOTexture()`/`_gBufferResources`，
    `setup(SharedInputs)` 不再接收 GBuffer 快照；`execute(ctx, frameAndLight, environmentLighting)`
    显式接收 current-flight binding；新增 `updateGBufferTextureDescriptors(...)` 从解析纹理更新 set 1。
  - `SSAOStage`：`appendGraphPass(graph, ctx, const DeferredSSAOPassParams&)`；pass execute 用
    `rgCtx.resolveTexture` 解析 albedo/normal/depth 并更新 `_inputDS`，不再从 `_gBufferResources` 读快照。
  - 主图：light pass 声明读取 `gBufferDepth` 并解析 GBuffer colors/depth/SSAO；
    删除 `setSSAOTexture`（initStages/refreshViewportStageState/syncFrameSettings/setSSAOEnabled/
    executeDeferredMainGraph）；`syncGraphAttachmentSnapshots` 保留 owner snapshot（debug）但不再回灌 stage。
- 关键决策：
  - 首次按 graph 解析 GBuffer depth 时触发 `assertTextureDeclared`（depth 未被 light pass 声明），
    已在 light pass build 增加 `passBuilder.read(gBufferDepth)` 修复。
  - `_currentSSAOOutput`/`_currentGBufferResources` owner snapshot 保留给 editor debug views，
    待 FG-403 改为 execution result 导出后移除双写。
- 测试：`python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:ResourceStateTrackerTest.*:DeferredRenderPipelineTest.*:DeferredFrameResourceSetTest.*:SSAOStageTest.*'`
  通过（96 tests）；新增 `DeferredPassParamsTest.SSAOAndLightDefaultsAreEmptyAndHandlesRemainFrameLocal`。
- smoke：默认（SSAO on）与 `ssao-disabled-smoke.automation.json`（SSAO off）各 300 帧 exit 0，
  日志无 Validation Error/VUID/VK_ERROR；`xmake b ya-engine`/`ya-editor` 通过。
- 提交：`[runtime/deferred] parameterize ssao and light graph passes`（本提交）
- 下一任务：FG-304（Skybox/Scene Overlay/Viewport Overlay 参数对象）

### 2026-08-03：FG-111 开始

- 代码事实：`IBuffer` 提供跨后端 `writeData`、`map`、`unmap` 和 `BufferHandle`；`IRenderResourceFactory::createBuffer` 是资源创建边界。
- 代码事实：`IRender::begin()` 在当前 flight fence 等待完成后才重置 fence；arena 的 `beginFlight()` 以此为调用前置条件。
- 设计边界：不新增 Vulkan-only alignment API，不硬编码后端常量；`allocate()` 要求调用方提供显式 alignment。
- 设计边界：每个 flight 独立 backing buffer 和 cursor；扩容时旧 backing 通过 `DeferredDeletionQueue` 延迟退休。
- 预计提交边界：新增 `FrameUploadArena` 资源类和对应 RenderGraphCore 单测，不迁移任何 stage。

### 2026-08-03：FG-111 完成

- 新增 `FrameUploadArena`：按 flight 隔离 host-visible `CpuToGpu` backing buffer，显式 alignment 的线性 slice 分配，slice 可写入并生成 `DescriptorBufferInfo`。
- backing 容量不足时按增长策略安全替换，旧 backing 进入 `DeferredDeletionQueue`；`beginFlight()` 只重置已由调用方 fence 等待完成的 flight cursor。
- 新增 2 个核心测试，覆盖共享 backing/non-overlap、descriptor offset/range、跨 flight 隔离、容量增长和 deferred retirement。
- 验证：`python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:ResourceStateTrackerTest.*'`（92/92 passed）；`xmake b ya-editor`（passed）；`python3 Script/ya.py build --project Example/HelloMaterial/HelloMaterial.yaproject`（passed）；`python3 Script/ya.py run-editor --project Example/HelloMaterial/HelloMaterial.yaproject -- --exit-after-frame=120 --log-level=warn --log-detail-level=error`（exit 0，日志无 Validation Error/VUID/[Error]/VK_ERROR）。
- `make test` 当前仅返回 “Nothing to be done”；`make b t=HelloMaterial` 无对应 make rule，已使用项目规定的 `python3 Script/ya.py` 等价入口验证。
- 下一可执行任务：FG-201。

### 2026-08-03：FG-201 前置审计

- 当前 Deferred 的 frame/light UBO 由 `GBufferStage` 创建、写入并持有，`LightStage` 只借用 descriptor set；主图再从 `GBufferStage` getter 导入整块 buffer。
- 迁移到同一 upload backing 的两个 descriptor slice 需要真实的 uniform-buffer offset alignment。仅使用 C++ `alignof` 会在 Vulkan 上违反 `minUniformBufferOffsetAlignment`，因此 `IRender` 需要提供后端无关的 alignment 查询，Deferred frame-resource owner 才能安全分配 slice。
- 该 RHI 查询是 FG-201 的前置代码依赖；实现范围限定为 `IRender`、Vulkan/OpenGL 后端和测试默认值，不改变 shader-facing 类型或 graph ownership。
- 进一步审计发现执行器当前按 `IBuffer*` 只保存一个状态，无法正确执行同一 upload backing 的非重叠 frame/light ranges；FG-201 必须同时收口为物理 buffer 的 range 状态集合，否则声明的 slice range 仍可能漏掉 HostWrite barrier。

### 2026-08-03：FG-201 实现进行中

- 状态：开始
- 提交边界：Deferred frame/light owner、RHI uniform alignment、共享 backing 的 graph range import/state tracking，以及对应 Deferred/Core 验证；保留用户已有的 `DescriptorVector.h` 删除，不纳入本任务。
- 失败路径审计：`DeferredFrameResourceSet::prepare()` 的两次 slice 分配和写入必须在新 binding 完整可用后再替换旧 binding，分配失败不能清空当前 descriptor。

### 2026-08-03：FG-201 完成

- 状态：完成
- 实现：
  - 新增 `DeferredFrameResourceSet`，集中持有 Deferred frame/light descriptor layout、pool、per-flight upload arena 和 shadow light payload；`GBufferStage` 不再创建或持有 frame/light UBO、descriptor set、shadow payload。
  - frame/light payload 使用生成的 Slang 类型写入同一 flight backing buffer，按 `IRender::getUniformBufferOffsetAlignment()` 计算真实 slice offset/range；两块数据一次连续预留，避免 arena 扩容时前一个 allocation 被旧 backing 留下。
  - `DeferredRenderPipeline` 在 frame boundary 准备 resource set，graph 以真实 slice range import frame/light backing；GBuffer 和 Deferred Light 两个 pass 都声明精确 range，executor 按物理 buffer 的非重叠 range 保存状态。
  - Vulkan backend 从 physical-device `minUniformBufferOffsetAlignment` 提供 alignment；RHI 默认值保持轻量 mock/OpenGL 实现兼容。
  - frame/light backing、imported range 和 descriptor replacement 均保留 completion-safe owner；分配失败保留上一份完整 binding，不清空 descriptor。
  - RuntimeTools panel 的 point-shadow 统计改从新的 frame resource owner 查询。
- 测试：
  - `python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:ResourceStateTrackerTest.*:DeferredRenderPipelineTest.*'`
  - 结果：94 tests passed。
  - `xmake b ya-editor`：build ok。
  - `python3 Script/ya.py run-editor --project Example/HelloMaterial/HelloMaterial.yaproject -- --exit-after-frame=300 --log-level=warn --log-detail-level=error`：exit code 0，退出稳定。
  - 最新日志：`Engine/Saved/Logs/YA-2026-08-03_17-07-11.log`，无 `Validation Error`、`VUID-`、`VK_ERROR` 或错误级别日志。
- 未做：skinning、SSAO、skybox、shadow per-flight owner 仍按后续 FG-202 至 FG-205 迁移；本任务不改变 shader-facing 结构。
- commit：`[runtime/deferred] centralize frame and light resources`（本提交）
- 下一任务：FG-202，迁移 Deferred skinning buffer owner。

### 2026-08-03：FG-202 实现进行中

- 状态：开始
- 代码事实：`GBufferStage` 当前同时拥有 skinning descriptor layout/pool、per-flight SSBO、capacity 和 CPU upload；扩容时会直接 reset 全部旧 owner，不满足其他 flight command buffer 仍可能引用旧 descriptor/buffer 的 completion-safe 生命周期。
- 提交边界：将 skinning resource owner、capacity replacement 和 CPU upload 迁到 `DeferredFrameResourceSet`；`GBufferStage` 只接收当前 skinning descriptor binding，`DeferredRenderPipeline` 从 resource set import owner-backed skinning buffer。保留用户已有的 `DescriptorVector.h` 删除。

### 2026-08-03：FG-202 完成

- 实现：`DeferredFrameResourceSet` 接管 skinning DSL、descriptor pool、per-flight SSBO、容量增长和 palette upload；扩容先完整创建新 pool/buffer/descriptor，再原子发布 binding，旧 pool/buffer 经 `DeferredDeletionQueue` 保活到 GPU completion。`GBufferStage` 删除 SSBO/pool/capacity/upload owner，仅接收当前 descriptor binding；`DeferredRenderPipeline` 从 frame resource binding import skinning buffer。
- 验收：新增容量策略测试，覆盖零 palette 的最小容量、倍增扩容及 32-bit buffer-size 溢出拒绝；无 palette 时仍具备有效 descriptor/buffer binding。
- 测试：`xmake b ya-editor` 通过；`python3 Script/ya.py test --target ya --filter 'DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:RenderGraphCoreTest.*:ResourceStateTrackerTest.*'` 通过（4 suites，95 tests）；`python3 Script/ya.py run-editor --project Example/HelloMaterial/HelloMaterial.yaproject -- --exit-after-frame=300 --log-level=warn --log-detail-level=error` 以 exit code 0 退出。
- 日志：`Engine/Saved/Logs/YA-2026-08-03_17-31-54.log` 未发现 `Validation Error`、`VUID-`、`VK_ERROR` 或 `[Error]`。
- 下一任务：FG-203，迁移 Deferred SSAO frame buffer owner。

### 2026-08-03：FG-203 实现进行中

- 状态：开始
- 代码事实：`SSAOStage` 当前持有 frame DSL、per-flight frame UBO 与 descriptor set，并在 `prepare()` 内自行上传。它的 `FrameData` 内容依赖 stage-owned SSAO settings，但 resource allocation/upload 不应由 pass module 持有。
- 提交边界：将 SSAO frame DSL、descriptor set 和 upload-arena slice 迁到 `DeferredFrameResourceSet`；`SSAOStage` 只构造 Slang 生成的 frame data、接收当前 binding 并继续管理自己的 sampled-input descriptors、noise texture 和 pipeline。

### 2026-08-03：FG-203 完成

- 实现：`DeferredFrameResourceSet` 新增 SSAO frame DSL、per-flight descriptor set 和 arena slice upload；`SSAOStage` 删除 per-flight UBO 与上传逻辑，只保留 pipeline-layout DSL 引用、输入 descriptor、noise texture 和 frame binding。SSAO graph pass 显式声明对应 UBO slice 的 `uniformRead`，独立入口也 import 同一 owner-backed slice。
- 验收：新增 `SSAOStageTest.BuildsFrameDataWithoutOwningGpuResources`，验证生成的 Slang frame payload、设置和投影矩阵；SSAO enable/disable 均通过编辑器冒烟。
- 测试：`xmake b ya-editor` 通过；`python3 Script/ya.py test --target ya --filter 'SSAOStageTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:RenderGraphCoreTest.*:ResourceStateTrackerTest.*'` 通过（5 suites，96 tests）；默认 SSAO `python3 Script/ya.py run-editor --project Example/HelloMaterial/HelloMaterial.yaproject -- --exit-after-frame=300 --log-level=warn --log-detail-level=error` 以 exit code 0 退出；关闭 SSAO 的同命令加 `--automation-config /tmp/ya-fg203-ssao-off.json` 以 exit code 0 退出。
- 日志：`Engine/Saved/Logs/YA-2026-08-03_19-11-51.log` 与 `Engine/Saved/Logs/YA-2026-08-03_19-12-12.log` 均未发现 `Validation Error`、`VUID-`、`VK_ERROR` 或 `[Error]`。
- 下一任务：FG-204，迁移 Deferred skybox frame buffer owner。

### 2026-08-03：FG-204 实现进行中

- 状态：开始
- 代码事实：`ViewportOverlayStage` 当前同时创建 skybox frame DSL/pool、per-flight UBO 和 descriptor set，并在 `prepare()` 内写入 `SkyboxFrameUBO`；skybox pass 的 graph declaration 也没有声明该 UBO range。
- 提交边界：使用生成的 `GLSL.Skybox.FrameUBO`，将 skybox frame DSL、descriptor set 和 arena slice 迁到 `DeferredFrameResourceSet`；overlay stage 仅保留 pipeline-layout DSL 和当前 frame descriptor，Deferred skybox pass 显式声明该 slice 的 uniform read。billboard frame UBO 不在本任务范围内。

### 2026-08-03：FG-204 完成

- 实现：`DeferredFrameResourceSet` 接管 skybox frame DSL、descriptor pool、per-flight arena slice 和 descriptor 更新；`ViewportOverlayStage` 删除 skybox UBO/pool/descriptor owner，仅保留 pipeline layout 与当前 frame descriptor。Deferred skybox pass 显式 import 并声明实际 UBO slice 的 `uniformRead`，frame payload 使用生成的 `GLSL.Skybox.FrameUBO`。
- 验收：新增 skybox frame payload 测试，验证相机平移被移除；Deferred pipeline 构建与 skybox 资源绑定沿用当前 flight owner。
- 测试：`xmake b ya-editor` 通过；定向渲染测试 97 tests 通过；`python3 Script/ya.py run-editor --project Example/HelloMaterial/HelloMaterial.yaproject -- --exit-after-frame=300 --log-level=warn --log-detail-level=error` 以 exit code 0 退出。
- 日志：`Engine/Saved/Logs/YA-2026-08-03_19-23-35.log` 未发现 `Validation Error`、`VUID-`、`VK_ERROR` 或 `[Error]`。
- 下一任务：FG-205，调查并迁移 shadow raster/cull/indirect per-flight buffer owner。

### 2026-08-03：FG-205 第一批次完成

- 实现：新增 `ShadowFrameResources`，统一持有 directional/point shadow 的 frame DSL、skinning DSL、descriptor pool、per-flight upload arena 与 capacity-managed skinning buffer。两个 raster pass 删除各自的 frame UBO、skinning SSBO、descriptor pool 和上传逻辑；graph 通过实际 arena `offset/size` 声明 UBO range，并在执行回调中消费 owner binding。
- 边界：point indirect renderer 的 instance buffer、draw command、visible instance、frustum/cull buffer 仍保留在原模块，下一批次再分类为 GPU-only scratch 或 host staging。
- 测试：`xmake b ya-editor` 通过；定向渲染/graph 测试 95 tests 通过；`python3 Script/ya.py run-editor --project Example/HelloMaterial/HelloMaterial.yaproject -- --exit-after-frame=300 --log-level=warn --log-detail-level=error` 以 exit code 0 退出。
- 日志：`Engine/Saved/Logs/YA-2026-08-03_19-42-24.log` 未发现 `Validation Error`、`VUID-`、`VK_ERROR` 或 `[Error]`。
- 下一步：继续 FG-205，迁移 point indirect/cull 的 per-flight instance 与 command resources，并保留 capacity growth 的延迟退休语义。

### 2026-08-03：FG-205 第二批次完成

- 实现：`PointShadowIndirectRenderer` 与 `PointShadowCullPass` 的容量从全局改为 per-flight；扩容只在当前已等待 fence 的 flight 上创建并发布新 instance/cull/command/visible/frustum buffer，descriptor 只更新当前 flight，旧 GPU buffer 经 `DeferredDeletionQueue` 退休。增加 32-bit buffer size 检查和失败返回，避免半完成状态继续进入 graph。
- 边界：这些资源仍是 imported per-flight buffers，尚未改成 graph transient slot；它们跨 compute cull 与 point raster pass，需要先完成跨 pass 参数对象后再决定 transient 化。
- 测试：`xmake b ya-editor` 通过；`python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:DeferredRenderPipelineTest.*'` 通过（85 tests）；默认 smoke 与开启 `pointLightUseIndirect/pointLightIndirectCullEnabled` 的 smoke 均 exit code 0，后者日志 `Engine/Saved/Logs/YA-2026-08-03_19-51-10.log` 未发现 `Validation Error`、`VUID-`、`VK_ERROR` 或 `[Error]`。
- 下一步：FG-205 收尾审计 Forward/Deferred shadow owner 共享边界，并为 cull/indirect imported resource 增加明确 range/descriptor snapshot 测试；确认后进入 FG-206 的真实 graph transient consumer。

### 2026-08-03：FG-205/FG-206 第三批次完成

- 实现：point shadow cull/indirect 的 draw command、visible instance、frustum 输出改为当前 graph 的 transient buffer；CPU 写入的 per-flight host buffer 通过显式 copy pass 上传，compute cull 和 point raster 通过同一组 graph handles 消费。descriptor 更新与 transient buffer resolve 均延迟到对应 pass callback，避免缓存 graph-owned 指针。
- 修复：NoCull 路径补齐 cull pass 的 active face/batch shape；此前 graph 会计算出零 bucket 并触发必需资源断言，现由 `prepareNoCull()` 与 compute shape 使用同一 cull owner 状态。
- 验收：`xmake b ya-editor` 通过；`python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:ResourceStateTrackerTest.*:DeferredRenderPipelineTest.*:SSAOStageTest.*:DeferredFrameResourceSetTest.*'` 通过（96 tests）；默认、Indirect+GPU Cull、Indirect+NoCull 各运行 300 帧并以 exit code 0 退出，日志 `Engine/Saved/Logs/YA-2026-08-03_20-03-15.log`、`Engine/Saved/Logs/YA-2026-08-03_20-10-02.log`、`Engine/Saved/Logs/YA-2026-08-03_20-08-36.log` 均无 `Validation Error`、`VUID-`、`VK_ERROR` 或错误级别输出。
- 复用证据：RenderGraph core 已有 transient slot 跨帧 pool hit 与 alias barrier 测试；本批次将真实 Deferred point-shadow cull consumer 接入该 registry。当前 cull 输出在同帧生命周期重叠，没有可安全 alias 的业务 buffer，按停止线以跨帧 pool reuse 作为 runtime 证明。
- commit：待提交，建议 `[runtime/shadow] route indirect buffers through render graph`
- 下一任务：FG-301，定义 DeferredFrameGraphResources 与 frame import result。

## 初始代码审计

### 已完成基础

- resource factory 已覆盖 Vulkan buffer/image/view/sampler 创建，buffer 静态 factory 已删除。
- ResourceStateTracker 已有 subresource state；RenderGraph compiler/registry/executor 已进入真实运行路径。
- Deferred 已有统一 `executeDeferredMainGraph()`，主图包含 shadow、GBuffer、SSAO、light、overlay 和 postprocess。
- Forward shadow group 已 graph-backed；presentation、screenshot/offscreen utility 已有独立 graph 使用案例。
- 固定 automation config、pipeline switch、resize、shadow、SSAO、postprocess、shutdown/readback 基线已存在于旧计划目录。

### 当前关键缺口

- Deferred 顶层图仍从 `DeferredFrameResourceSet` binding import frame/light/skinning buffer；
  handle 组织已由 FG-301 收口到 `DeferredFrameGraphResources`，下一阶段（FG-302~306）把它升级为
  typed pass parameters，消除回灌。
- Deferred frame/light/skinning/SSAO/skybox 与 shadow raster owner 已集中；Forward 及部分 postprocess/overlay 资源边界仍待后续阶段收口。
- SSAO、Postprocess、Bloom、Directional/Point/Cull shadow 等模块仍存在局部 executor/standalone graph 入口。
- graph prepare 后仍有 resolved resource 回灌、Stage prepare 和 descriptor cache 更新；setup 与实际 binding 不是同一参数真相源。
- world graph、presentation graph 和 graph 外 capture callback 构成三段 orchestration。
- persistent physical identity 已使用 stable key；imported resource 的 source/view identity 仍由各 consumer 显式声明。
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

### 2026-08-03：FG-109 完成

- 状态：完成
- 代码事实：
  - 同一 compiled physical slot 的多个 logical buffer 共享 `IBuffer*` 后，executor 只按 physical pointer 记录 state；如果前后 logical buffer 恰好使用相同 state，单纯比较 stage/access/range 会跳过必要同步。
  - alias 切换还必须覆盖整个 physical slot，不能只使用下一个 logical buffer 的局部 range，否则前一个 logical range 的写入可能泄漏到后一个 identity。
- 实现：
  - `RGCompiledGraph` 增加 `RGTransientBufferAliasBoundaryPlan`，记录 slot、previous/next logical handle 和 next pass。
  - compiler 按每个 slot 的 lifetime 顺序生成 boundary，并断言 slot 成员 lifetime 不重叠。
  - executor 在 boundary 的首个 pass 强制发出 buffer barrier；barrier 使用 physical buffer 全范围，之后再把 state 更新为 next logical access state。
  - 同一 pass 内若一个 logical buffer声明多个 usage，只对该 buffer的 alias boundary 强制一次，避免重复 boundary barrier。
  - debug dump 与 diagnostics 增加 alias boundary 数量和成员切换位置。
- 未做：
  - 还没有统一的 physical slot pool hit/miss/reuse ratio runtime diagnostics；这属于 `FG-110`。
  - 仍然只覆盖单 queue；没有扩展 queue ownership 或跨 queue alias synchronization。
- 测试：
  - `python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:ResourceStateTrackerTest.*'`
  - 结果：89 tests passed
  - 新增/覆盖：
    - `CompileAllocatesDeterministicTransientBufferSlots` 校验 alias boundary plan
    - `ExecutorForcesBarrierAtTransientAliasBoundary` 校验相同 access state 仍发出 physical 全范围 barrier
  - 额外烟测：`xmake b ya-editor`
  - 结果：build ok
- artifacts：
  - `RGTransientBufferAliasBoundaryPlan`
  - executor alias boundary barrier path
- commit：待提交
- 下一任务：
  - 进入 `FG-110`，增加 buffer reuse diagnostics 和完成门禁

### 2026-08-03：FG-110 完成

- 状态：完成
- 代码事实：
  - compiled graph 已有 logical/physical slot assignment，但此前没有明确的 reuse ratio，也没有 runtime registry 层的 pool hit/miss 可观测数据。
  - `sync(graph)` 兼容路径有意不启用 alias；需要一个可测试的对照，防止“保留兼容 API”被误解成运行时 alias 已经生效。
- 实现：
  - `RGTransientBufferDiagnostics` 增加 `reuseRatio`，并在 debug dump 输出 logical/physical bytes、slot、alias boundary 和 ratio。
  - `RenderGraphResourceRegistry` 增加只读 `RGTransientBufferPoolDiagnostics` snapshot，记录本次 sync 的 hit/miss、pool entry 数和累计计数。
  - 完成门禁要求测试图中 physical slot 数小于 logical used 数，连续 compiled frame 能观察到 pool hit，capacity/memory class 变化观察到 miss。
  - legacy `registry.sync(graph)` 对照测试确认未提供 compiled plan 时仍按 logical buffer 分配，不会偷偷改变旧 standalone 行为。
- 未做：
  - 还没有把 pool LRU/容量上限接入运行时；当前 pool 只在 registry clear/destructor 时统一回收。
  - 还没有 per-flight upload arena；CPU-written UBO 仍由后续 `FG-111` 处理。
- 测试：
  - `python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:ResourceStateTrackerTest.*'`
  - 结果：90 tests passed
  - 新增/覆盖：
    - `ResourceRegistryReusesTransientBufferPoolAcrossFramesAndGrowsSafely` 的 hit/miss 断言
    - `ResourceRegistryLegacySyncKeepsLogicalTransientBuffersSeparate`
    - compiled slot reuse ratio 断言
  - 额外烟测：`xmake b ya-editor`
  - 结果：build ok
- artifacts：
  - `RGTransientBufferPoolDiagnostics`
  - `RenderGraphResourceRegistry::getTransientBufferPoolDiagnostics()`
- commit：待提交
- 下一任务：
  - 进入 `FG-111`，实现 completion-safe per-flight FrameUploadArena

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
