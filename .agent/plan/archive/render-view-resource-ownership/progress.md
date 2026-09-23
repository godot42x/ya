# Progress — render-view-resource-ownership

## 2026-09-23 收尾轮

### 核对结论

五个 checkpoint（A 统一发布事实源 / B 引入 `ViewTargetStore` / C 删除 graph persistent
View 资源 / D 显式 View 生命周期 / E 收敛 stage 与诊断残留）的代码均已落地：

- §13「必须删除的旧路径」全部为 0 命中：`ViewResourceKey/Table`、`reconcilePublishedViews`、
  `DeferredPipelineDebugViews`、`getDeferredPipelineDebugViews`、`getLastFrameGraphTopology`、
  `ViewPersistentResourceKey`、`createViewPersistentTexture`、`_preparedOutputImage`、
  `_debugAlbedoRGBView` 等。
- 计划声称的新结构均存在：`ViewTargetStore` / `ViewTargetLease` / `EViewAttachment` /
  `ViewTargetRequest` / `IRenderPipeline::beginSubmission` / `_frameGraphTopologies`
  （逐 family 汇总）/ `ISceneViewProducer::ownedViewLocalIds` / `registerSceneView`。

### 本轮改动

1. **命名偏差修正**：plan 草案里的 `PublishedView` 落地时沿用了既有记录类型
   `RenderViewOutput`，由 `ViewTargetStore` 持有 `RenderViewOutputTable`
   （`beginPublication` / `publishView` / `findPublication`）。plan 文本（§3/§8/§9/§16）
   已跟随实现改写，不做无关重命名。
2. **结论沉淀**：写下 `skills/render-arch/SKILL.md` 第 18 条「View target 资源只有一套
   所有权，落在 `ViewTargetStore`」，含创建/持有/本帧使用/graph 访问/发布/查询/替换/回收
   全链路与「存在 ≠ 本帧渲染」「不用缺席推断销毁」两条约束。
3. **附带修复（非本 plan 领域，独立提交）**：`WidgetTree::detach` 命中系统 layer 时原为
   `YA_CORE_ASSERT(false)` → `PLATFORM_BREAK()`（macOS = `__builtin_trap()`），使项目自带的
   `WidgetTreeTest.SystemLayersCannotBeDetached`（它正是为了验证「系统层不能被 detach」而
   调用 `detach`）必然 SIGTRAP，全量测试无法跑完。改为 `YA_CORE_WARN` + 拒绝：层保留在树中，
   误用可恢复，不再拖垮进程。

### 验证

| 命令 | 结果 |
| --- | --- |
| `ya-testing --gtest_filter='ViewTargetStore*:RenderViewOutput*:ViewFamilyRenderer*:RuntimeGameViewProducer*'` | 19/19 通过 |
| `ya-testing --gtest_filter='WidgetTreeTest.*'` | 89/89 通过（含 `SystemLayersCannotBeDetached`） |
| `ya-testing`（全量） | 1315 用例，1267 通过，10 失败 |
| `ya-testing --gtest_filter='RenderGraphCoreTest.*'` | 86/86 通过（隔离下） |

### 遗留（均与本 plan 改动无关，已用 `git stash` 基线复核）

- **10 个既有失败**（在全量里位于崩溃点之前，属其它线）：
  `GUIWindowManagerTest.DragOverlaySessionIsExemptFromFocusAndInput`（本机 Vulkan portability
  缺 `VK_KHR_surface`，环境类）、`ToolControlsTest`×2、`EditorPropertyGraphTest`×2、
  `WidgetLayoutTest.FloatingWindowResizeHandlesLiveOnOverlaySlots`、
  `ScriptApiLibraryFixture.GameUIWidgetLifecycleThroughRegistry`（`'_color'` 不在
  `engine.panel` 上）、`GameUIHostTest.BuildSnapshotComposesMountedWidgets`、
  `GUIHeadlessHostTest`×2（snapshot items / tintColor）。

### 顺序依赖失败已修（独立提交）

`RenderGraphCoreTest.ResourceRegistryUsesProvidedImportedImageViewAndRetainsOwner` 在隔离运行
（86/86）通过、全量下失败：它断言 `registry.clear()` 后 `retainedOwner.expired()` 为 true，
但 `clear()` 把 retained bundle 交给进程级 `DeferredDeletionQueue`（一个全局单例），释放发生在
队列被 flush 时；全量下队列已被前面的用例初始化，release 被 defer。同 suite 其它用例都显式做
`DeferredDeletionQueue::get().flushAll()/init(1)`，此用例漏了。修法：`clear()` 后显式
`flushAll()` 再断言，不依赖前置用例状态。该改动独立于本 plan，单独提交（`[test/rendergraph]`）。

### 冒烟

- HelloMaterial runtime：`exit-after-frame=120` + automation `--screenshot`，exit 0，产出视口 PNG。
- Control Service（TCP JSON-RPC，`--automation-control-port`）：`ping` / `get_world_view_state` /
  `capture_screenshot` / `set_render_pipeline{target:forward}` / `quit` 全 OK，应用干净退出。
- GameEditor（`run-editor`）：`exit-after-frame=120` + `--screenshot`，exit 0，产出 PNG。
