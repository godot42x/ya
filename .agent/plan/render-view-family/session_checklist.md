# Session Checklist

## 开工前

- [ ] 阅读根 AGENTS.md、.agent/plan/AGENTS.md。
- [ ] 按任务读取 ya-build、render-arch；改 GUI compose 时再读取 gui-framework。
- [ ] 查看 git status、最近相关提交和 progress.md。
- [ ] 复述本轮唯一 checkpoint、边界、保留项和非目标。
- [ ] 用 rg 核对当前符号和调用方，不以旧计划路径替代源码事实。
- [ ] 确认不会重复实现 gui-multi-os-window-editor 已有的 surface/present 能力。

## 实施中

- [ ] 只修改当前 checkpoint 所需的抽象、调用点和测试。
- [ ] 不把 Forward/Deferred 策略差异抽成万能基类。
- [ ] graph execute 不查询 ECS、Scene、live WidgetTree 或 ResourceResolveSystem。
- [ ] 不在 command recording 中途重建 GPU 资源，检查 deferred deletion/lifetime。

## 收尾前

- [ ] 运行受影响的 XMake build/test，target 名称以 xmake l targets 为准。
- [ ] 运行对应 golden/trace；R2 后增加双 View/双 Surface。
- [ ] 检查 git diff --check、staged diff 和生成文件。
- [ ] 更新 progress.md、feature_matrix.json、todo.md。
- [ ] 明确记录保留项、未完成项和偏离项。
- [ ] 代码、测试、plan/progress 使用同一 checkpoint 提交。

## 最近一次 checkpoint

- 2026-09-17：修正 C/D/E 计划状态为部分完成；目标收成公开 `Renderer`。不改引擎代码。下一刀是合并 DeviceState+Coordinator。
- 2026-09-17：host 提交 live Scene 列表：`HostSceneViewSubmit` 按 Scene* 去重 extract；recording derivedScene 从列表查找。HostSceneRenderSubmitTest 3/3，专项与回归 76/76。产品双 viewport 排在 4.0.3 之后。
- 2026-09-17：family-scoped derived Scene：删除 `RenderFramePlan::derivedScene`；recording 携带 host Scene；同 family 共享、跨 SceneId 隔离。Host 仍只提交一个 live Scene。专项与回归 76/76。下一刀是产品双 Scene 录制 / 双 Surface GPU。
- 2026-09-17：4.0.2 E：拆除 `RenderRuntime` 为 `RenderDeviceState` + `RenderFrameCoordinator`；删除 `ViewportStateService` 与 `getActiveScene` locator。Host 拥有 world-enable 与 viewport rect。空 `sceneRender` 是 UI-only。未引入空 ViewHistoryStore。专项与回归 72/72。下一刀是产品双 Scene / 双 Surface 验收。
- 2026-09-17：4.0.2 C：typed `ViewResources` 持有 SSAO/Light/EntityId/Overlay/debug/post DS/UBO；Bloom/BasicPost viewId map 与 Stage singleton CIS 删除；PointShadow packet 在 Shadow View Binding。ViewPassResourcesTest 5/5，专项与回归 66/66。下一刀曾是 Checkpoint D（family renderer），不再扩大 mega Binding。
- 2026-09-17：4.0.2 B：`SceneViewFamilyPlan` 在 seal 时物化；`SceneFamilyResources` 由 submission 持有 skinning SSBO。同 Scene 双 View owner 相同，双 Scene owner/buffer 不同。SceneFamilyResourcesTest 4/4，专项与回归 57/57。下一刀曾是 Checkpoint C（typed pass resources），不再扩大 mega Binding。
- 2026-09-17：4.0.2 A：`RenderSubmission` / `RenderSubmissionPool` 拥有 cmd/upload/transient DS/keepalive/finish；删除 `RenderSubmissionContext` / `RenderSubmissionTable` 与 resource-set `beginSubmission`。RenderSubmissionTest 7/7，专项与回归 53/53。下一刀曾是 Checkpoint B（SceneFamily），不再扩大 mega Binding。
- 2026-09-16：重审 4.0.2：补齐 SceneFamily 生命周期，ViewFamily 改为 graph 编译单位；执行顺序为 Submission owner → SceneFamily owner → typed pass resources → family renderer → 拆 RenderRuntime。下一刀曾是 Checkpoint A，不再扩大 mega Binding。
- 2026-09-16：postprocess/bloom 输出与 CombinedImageSampler set 按 View 持有；同一帧两个 View 不再共用 display GPU 资源。ViewPersistentResourceKeyTest 含 PostprocessAndBloomOutputsStayViewKeyed，RenderGraphCoreTest.ViewKeyedPostprocessTexturesStayIndependentAcrossSequentialGraphs，专项回归 56/56，ya-game-runtime / ya-game-editor 构建通过。Processor viewId map 仍是迁移期 hack。
- 2026-09-16：overlay View 的 composeRect 与 host viewport identity 分离；overlay 不得 resize host RT spec；camera frustum 为 compact gizmo。CameraFrustumOverlayTest 1/1，RenderRuntimeSnapshotTest 14/14，专项回归 52/52，ya-game-runtime / ya-game-editor 构建通过。
- 2026-09-16：产品路径为选中的 world Camera submit 第二个 SceneRenderRequest，ViewCompose PiP 到主 viewport 右下角，world Camera 用 overlay 3D 线画锥体。CameraFrustumOverlayTest 2/2，RenderRuntimeSnapshotTest 14/14，专项回归 53/53，ya-game-runtime / ya-game-editor 构建通过。
- 2026-09-16：RenderRuntime 按 SceneViewportTask 循环 tick/publish；同一 Scene 两个 View 共享 snapshot。RenderRuntimeSnapshotTest 11/11，专项与回归 48/48，ya-game-runtime 构建通过。
- 2026-09-16：Forward/Deferred viewport（含 GBuffer/SSAO）persistent key 按 ViewId 分名；同一 executor 上 View A/B 不再共用一张 GBuffer/color。ViewPersistentResourceKey 4/4，专项与 snapshot/binding/submission 回归 47/47，ya-game-runtime 构建通过。
- 2026-09-16：每个 View 拥有独立 output/extent/format 句柄；发布 B 不改写 A。RenderViewOutputTable 5/5，snapshot/binding/submission 回归 38/38，ya-game-runtime 构建通过。
- 2026-09-16：RenderRuntime 按 flight 持有 live submission；overlay 与 graph-exported image 保活到该 flight 下次 fence-safe 复用。RenderSubmissionTable 5/5，snapshot/binding/deferred/arena 回归 32/32，ya-game-runtime 构建通过。
- 2026-09-16：Shadow FrameResourceSet 提供 beginSubmission/beginView；cascade/face descriptor 与 upload slice 按 View 隔离。RenderViewBindingTable 8/8，snapshot/deferred/arena 回归 27/27，ya-game-runtime 构建通过。
- 2026-09-16：Deferred FrameResourceSet 提供 beginSubmission/beginView；SSAO/skybox 写入同一 View slot。抽出 ViewDescriptorSetAllocator 供 Forward/Deferred 共用。RenderViewBindingTable 7/7，snapshot/draw-candidate/deferred 回归通过，ya-game-runtime 构建通过。
- 2026-09-16：Forward FrameResourceSet 提供 beginSubmission/beginView；同一 submission 的 View 拥有独立 frame descriptor/slice。RenderViewBindingTable 6/6，snapshot/draw-candidate 回归通过，ya-game-runtime 构建通过。
- 2026-09-16：View-owned draw bucket 已完成 source pointer + order indices 迁移；14 个 RenderRuntime/DrawCandidateView 测试通过，ya-game-runtime 构建通过。
- 2026-09-16：Forward/Deferred 已移除跨 View 的 `_lastTickCtx` / `_lastFrameInput`，graph build 使用调用栈内 View-local context；渲染测试 14/14，ya-game-runtime 构建通过。
- 2026-09-16：FrameUploadArena 同 `(flightIndex, frameToken)` 的 begin 改为幂等追加语义；同 submission 的后续 allocation 不 rewind cursor，FrameUploadArena 专项测试通过。
- 保留未完成项：双 Scene/双 ViewFamily 产品录制、双 Surface GPU 验收、viewport click picking 仍需 mesh/billboard 写 entityId。
