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

- 2026-09-16：Forward/Deferred viewport（含 GBuffer/SSAO）persistent key 按 ViewId 分名；同一 executor 上 View A/B 不再共用一张 GBuffer/color。ViewPersistentResourceKey 4/4，专项与 snapshot/binding/submission 回归 47/47，ya-game-runtime 构建通过。
- 2026-09-16：每个 View 拥有独立 output/extent/format 句柄；发布 B 不改写 A。RenderViewOutputTable 5/5，snapshot/binding/submission 回归 38/38，ya-game-runtime 构建通过。
- 2026-09-16：RenderRuntime 按 flight 持有 live submission；overlay 与 graph-exported image 保活到该 flight 下次 fence-safe 复用。RenderSubmissionTable 5/5，snapshot/binding/deferred/arena 回归 32/32，ya-game-runtime 构建通过。
- 2026-09-16：Shadow FrameResourceSet 提供 beginSubmission/beginView；cascade/face descriptor 与 upload slice 按 View 隔离。RenderViewBindingTable 8/8，snapshot/deferred/arena 回归 27/27，ya-game-runtime 构建通过。
- 2026-09-16：Deferred FrameResourceSet 提供 beginSubmission/beginView；SSAO/skybox 写入同一 View slot。抽出 ViewDescriptorSetAllocator 供 Forward/Deferred 共用。RenderViewBindingTable 7/7，snapshot/draw-candidate/deferred 回归通过，ya-game-runtime 构建通过。
- 2026-09-16：Forward FrameResourceSet 提供 beginSubmission/beginView；同一 submission 的 View 拥有独立 frame descriptor/slice。RenderViewBindingTable 6/6，snapshot/draw-candidate 回归通过，ya-game-runtime 构建通过。
- 2026-09-16：View-owned draw bucket 已完成 source pointer + order indices 迁移；14 个 RenderRuntime/DrawCandidateView 测试通过，ya-game-runtime 构建通过。
- 2026-09-16：Forward/Deferred 已移除跨 View 的 `_lastTickCtx` / `_lastFrameInput`，graph build 使用调用栈内 View-local context；渲染测试 14/14，ya-game-runtime 构建通过。
- 2026-09-16：FrameUploadArena 同 `(flightIndex, frameToken)` 的 begin 改为幂等追加语义；同 submission 的后续 allocation 不 rewind cursor，FrameUploadArena 专项测试通过。
- 保留未完成项：多 View command recording、PointShadow indirect per-flight 缓冲、双 View/双 Surface GPU 验收。
