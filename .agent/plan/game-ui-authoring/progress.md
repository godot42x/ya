# Progress

## 2026-09-20 — Phase 1 (landed)

目标：Scene 存文档引用，不存内联文档。

已完成：

- 新增 UIDocumentStore（ya-gui-widgets）：按路径唯一持有 live UIDocument，
  resolve（按需读盘）/ put（发布编辑，不写盘）/ find（只查不读盘）/ save（写盘）。
  磁盘形态就是 UIDocument::toJson 的裸 JSON，扩展名 .yaui。
- SceneWidgetEntry::inlineDocument -> documentPath，序列化键 "inline" -> "document"。
  Scene/Core 的头不再需要 UIDocument；overrides 仍由 UIInstanceOverrideSet 承担
  （它 applyTo(UIElement) 需要完整 UIElement，cpp 里单独 include）。
- App 持有 UIDocumentStore，构造时交给 GameUIHost；mountSceneAutoMountEntries
  增加 documents 参数，按路径解析，空 store / 解析失败走 onError。
- 两个挂载点都接了 store：DefaultGameUIController（运行时）、
  EditorViewportCompositor::composeCanvasPreview（编辑器 canvas 预览）。
- 编辑器：EditorLayer 成为两张表的唯一持有者（bindDocumentServices:
  EditorDocumentRegistry + UIDocumentStore），designer session 只向 owner 要；
  删除 designer 上的 bindDocuments / _documents / _entryScene / _entryId 平行路径。
  openDocument 改为按路径打开资产，新增 openUntitled 供 newDocument 使用；
  openSceneEntry 只接受 entry 引用。saveDocument 先 put（层级/挂载立即可见）
  再写盘；syncPreviewToDocument 只 put，不写盘。
- Inspector 的 Game UI Entry 行显示文档路径（列名 Type -> Document），
  打开按钮按路径非空启用。
- scripts: 删除 ui.add_to_scene（唯一的内联文档生产者；仓库内零调用方），
  换成 ui.mount_document({path, name?, zOrder?})，条目只存路径，
  entryId 默认取文档文件名 stem 并在场景内去重。
- 测试：新增 UIDocumentStoreTest（存盘往返 / put+save 覆盖 / 缺失与损坏路径 /
  未知文档 save 失败）、ScriptApiLibraryFixture.MountDocumentCreatesAReferenceEntry；
  改写 SceneWidgetEntryTest / SceneSerializerTest / GameUIHostTest /
  EditorUIDesignerSessionTest 以文档引用为准（GameUIHostTest 用 store.put 发布
  文档，不落盘）。

验证：

- xmake b ya-gui-widgets / ya-scene-core / ya-game-runtime / ya-game-editor / ya-testing 全绿
- xmake r ya-testing（渲染/编辑器/GUI/Scene/UIDocument/ScriptApi 过滤器）：
  650 tests, 645 passed, 5 failed —— 5 个都是既有基线，用 git stash 去掉本轮改动后
  同样失败，或者已在 plan 里记录：
  - EditorPropertyGraphTest.AutoPropertySectionAssetPathCommitBrowseAndUndo
  - EditorPropertyGraphTest.TextureAssetRowShowsRetainedPreview
  - WidgetLayoutTest.FloatingWindowResizeHandlesLiveOnOverlaySlots
  - ScriptApiLibraryFixture.GameUIWidgetLifecycleThroughRegistry（用例对一个没有
    _color 字段的 engine.panel 调 ui.set，属用例本身过期）
  - GameUIHostTest.BuildSnapshotComposesMountedWidgets
- 运行时冒烟：Example/HelloMaterial，--exit-after-frame=90，viewport 截图
  md5 c775245ae636f15b41da8485319a2267，与基线逐字节一致。
- 编辑器冒烟：--exit-after-frame=120，exit 0，无 error/assert。

剩余 / 未完成：

- 阶段四的 UI 资产浏览器未做：新建文档没有落盘入口，场景 entry 目前只能由
  ui.mount_document 或手写场景文件产生。
- Scene/Core -> ya-gui-widgets 这条依赖还在（UIInstanceOverrideSet::applyTo 需要
  UIElement）。要真正切掉得先把 overrides 的表现层/实例层边界定下来（阶段四）。

## 2026-09-25 — Phase 0 (landed)

目标：运行时 Game UI 有明确的 tick 驱动。

核对结论：`GameUIHost::buildSnapshot()` 只做 layout + paint，不推进 tick；全仓库
对 `WidgetTree::tick` 的调用只有 `EditorSurface::tick` 一处（编辑器 chrome 树）。
运行时挂载的 UI 树因此从不 tick，behavior / tween 不跑。

已完成：

- `GameUIHost::tick(dt)`，转发 `WidgetTree::tick`。
- `RuntimeRenderContext::buildGameRenderFrame` 内，与 `setPresentation` / `buildSnapshot`
  同一分支、同一顺序：先 tick 再 snapshot。选渲染侧而非 `tickLogic`，因为暂停只 gate
  逻辑；暂停帧仍然提交，暂停菜单的输入反馈与动画必须继续跑。
- 门禁 `GameUIHostTest.TickAdvancesMountedTreeBehaviors`：断言挂载不 tick、
  buildSnapshot 不 tick、只有 `host.tick()` 推进计数。

验证：

- xmake b ya-game-runtime / ya-testing 全绿。
- ya-testing 相关过滤器 282 tests 全过（GameUIHost/Scene/ScriptApi/Widget/
  EditorUIDesigner/RenderRuntime/RenderView）。
- 运行时冒烟 HelloMaterial --exit-after-frame=90，viewport 截图 md5
  c775245ae636f15b41da8485319a2267，与基线逐字节一致（默认场景没有挂 UI 文档，
  所以输出不变是预期的）。

剩余 / 未完成：

- editor designer preview tree 同样从不 tick（`EditorUIDesignerSession` 只
  buildPreviewSnapshot）。同一类缺陷，归属 Phase 3 的预览渲染一起修。
