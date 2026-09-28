# Progress

## 2026-09-28 — P0 驱动点被 game-ui-script-framework F0 取代

`GameUIHost::update` 从 `buildGameRenderFrame` 移到 `GameRuntimeTickOrchestrator::tickUILogic`。
原先放渲染侧的理由是「暂停 gate 整段逻辑」；F0 起暂停只停玩法模拟系统与世界脚本，UI 逻辑
每帧都跑（`FUIFrameClock` 语义不变）。见 `game-ui-script-framework/progress.md`。

## 2026-09-25 — Phase 0 收敛 + Phase 2b 落地

**Phase 0（时间策略）**：上一版 `GameUIHost::tick(float)` 把「UI 时间策略」写死成一个
说不清语义的 dt，且隐式等于真实时间。现在：

- `FUIFrameClock{gameDelta, realDelta}` 在唯一知道暂停决定的地方（`buildGameRenderFrame`）
  一次算出；`EUIUpdateClock` 由 host 声明它读哪一个（`updateClock()` / `setUpdateClock()`）。
- 默认 RealTime，与上一版行为一致：暂停菜单继续动画。gameplay HUD 可切 GameTime 随游戏冻结。
- 门禁两条：`UpdateAdvancesMountedTreeBehaviors`、`ClockPolicyDecidesWhetherPausedFramesAdvanceTheTree`。
  后者第一版写错成「数访问次数」，实测发现暂停帧仍会拜访树（给 0 秒），改成累积秒数才对——
  策略决定的是「推进多少时间」，不是「是否访问」。

**Phase 2b（三棵树）**：

- `EditorUIDesignerSession` 明确为 Authoring：预览树私有、无 tick / dispatch 入口，头注释
  写清契约；新增门禁 `CanvasPickingSelectsAButtonWithoutRunningItsClickHandler`（画布拾取
  选中按钮，但不触发 onClick）。
- 场景 UI 预览改为持久宿主 `EditorGameUIPreview`：重建条件是 mount 输入（场景 / mount 列表 /
  文档 revision / extent），不再是每帧。
- `UIDocumentStore::revision(path)` 新增，供缓存派生结果的消费者判断文档是否被改过。
- 新增 `EditorGameUIPreviewTest` 四条门禁。

验证：

- `xmake b ya-game-runtime / ya-game-editor / ya-testing` 全绿。
- `xmake r ya-testing` 全量 1325 passed / 0 failed。
- 运行时冒烟 HelloMaterial --exit-after-frame=90，viewport 截图 md5
  `c775245ae636f15b41da8485319a2267`，与基线逐字节一致。
- 编辑器冒烟（`run_widgettree_editor_smoke.py`）**本身 flaky**：同一份代码连续跑会
  时而通过、时而报 `world view did not render: 0x0`。已用 `git stash` 在 HEAD 上对照，
  基线同样复现（连跑 2 次即 1 次失败），与本次改动无关。判定本轮编辑器路径是否安全，
  以 `ya-testing` 全量与运行时冒烟为准；编辑器冒烟的多跑一次通过不能当证据。

剩余 / 未完成：

- per-subtree 时钟（暂停菜单与 HUD 同帧不同速）——一个 host 一棵树，需要时再拆。
- Interactive Preview（designer 内试跑输入 / 动画）未做，按需显式增加。

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

## 2026-09-29 — Phase 6 立项：UI Designer 可用性

用户判断保留模式设计器基本不可用，要求先对照 `origin/main`（`91ded16e`）的 ImGui 版。核对结论写入
plan.md Phase 6 基线表：新建硬编码 `"panel"` 必失败、无 Open / Save As、几何迁到父侧 slot 后检查器
无任何布局字段（slot 编辑器从未实现）、层级无右键菜单、预览固定 800×600；两版都没有设计器撤销。

决定：撤销用文档快照；计划并入本计划作为 Phase 6；其余体验项延后。本轮只做设计器内部编辑闭环
（U2 → U3 → U4），U1 排在之后。

### U2 文档事务与撤销（落地）

- `EditorUIDesignerSession::commitEdit(label, mergeKey)` 取代 `syncPreviewToDocument`：比对快照，
  有变化才推一步、标 dirty、`UIDocumentStore::put`。Palette 添加 / 删除 / 层级拖放 / 画布拖拽（一次
  手势在 `endDrag` 提交一次）/ 检查器改属性（`FEditCommitSink`，拖动字段用 beginMerge/endMerge）全部
  走它。撤销中途有拖拽时 `cancelDrag`，不会反向提交把 redo 清掉。
- 重建预览后 `previewGeneration()` 自增，检查器指纹带上它，避免复用旧地址。
- 键盘：之前快捷键只派发给 Level root，UI 页按 Ctrl+Z 会撤销关卡。现在派发给当前页 root；
  UI root 注册 `edit.undo/redo`、`selection.delete`、`ui.save`；Edit 菜单同样取当前页。
  画布 tab 自己的 Delete 分支删除。
- 已知限制：两个设计器会话共享同一文档撤销栈时，一方推的步在它销毁后出栈为空操作。编辑器每层只有
  一个设计器会话，暂不处理。UI 页 Edit 菜单的 Duplicate 在 U4 注册前显示为禁用。
- 验证：`ya-testing` 1398 通过 / 1 跳过；`ya-gui-closure-test` 612 通过；HelloMaterial、GreedySnake
  编辑器 120 帧 exit 0。
- 手测（待用户）：UI 页 Palette 加控件 → Ctrl+Z 消失 / Ctrl+Shift+Z 回来；画布拖一个控件松手后
  Ctrl+Z 一步回原位；检查器拖数值字段一次松手撤销一步；UI 页 Ctrl+Z 不影响关卡；Level 页 Ctrl+Z
  不影响 UI 文档。

### U3 Slot 检查器（落地）

- 选中控件的检查器下方加「<类型> Slot」分组。反射对象是 slot 的 authoring args（`FCanvasSlotArgs` /
  `FBoxSlotArgs` / `FOverlaySlotArgs` / `FContentSlotArgs` / `FTableSlotArgs`，在 `UILayout.cpp` 外部
  反射，连同 `FMargin` 与 `EWidgetSizeMode` / `EUIOverlayAlignment` / `EUIBoxSlotSizeRule` /
  `EUIBoxSlotCrossAlignment` 枚举），不是 slot 私有字段：私有字段直写会绕过 setter 与
  `promoteStretchedAutoAxes`。`EditorUISlotEdit` 持有 args 副本，每次从 child 重新找 slot（reparent 会
  换 slot 对象），change hook 用新增的 `UISlot::assign` 写回；检查器每 tick `pull` 跟随画布拖拽和撤销。
- 为什么新增 `toArgs()` / `assign()` 而不改 `apply`：`apply` 把零尺寸当「未设置」，dock 浮窗、popup、
  设计器拖拽都在运行时对活 slot 做部分 `apply`，改语义会波及它们。`assign(toArgs())` 是精确往返。
- 锚点预设 12 个（九点 + 横向 / 纵向拉伸 + 填充），`withCanvasAnchorPreset` 放在 GUI Layout：点锚
  需要 pivot = 锚点且对齐 Left/Top（点轴上对齐作用于整个父尺寸），拉伸轴 pivot 必须为 0——这是 canvas
  布局语义，不该由编辑器知道。预设保留当前布局尺寸、清空 offset / insets。
- 文档根的父边是设计器宿主的 canvas slot，不属于文档：`EditorUIDesignerSession::editSlot` /
  `applyCanvasAnchorPreset` 对根返回空。
- 反射注册后这些枚举在反射序列化里写名字；读取仍接受整数，旧文档可读。slot 本身的 JSON 仍是手写
  `serialize`，不受影响。
- 发现（未做）：容器类控件的布局属性不反射也不进文档，见 plan.md U3 发现。
- 验证：`ya-testing` 1403 通过 / 1 跳过；`ya-gui-closure-test` 614 通过；HelloMaterial、GreedySnake
  编辑器 120 帧 exit 0。
- 手测（待用户）：选中画布子控件看到「Canvas Slot」分组与锚点按钮；点 BR 贴右下、Fill 铺满，
  Ctrl+Z 一步回退；拖 offset 数值一次松手一步撤销；选 column 里的子控件看到「Box Slot」，
  Size Rule 是下拉框；画布拖动控件时 Slot 分组的 offset 跟着变。

### U4 画布与层级补齐基线（落地）

- 设计分辨率：`EditorUIDesignerSession::setDesignResolution` 设预览树 logical extent（默认 1280×720），
  是设计器设置，不进文档、不产生撤销步。画布 tab 顶部工具条：预设下拉（含 Custom）+ 宽 / 高数值框 +
  Fit；会话在别处改了分辨率时工具条跟随。画布上画设计框（1px 灰边）。
- 适配：打开 / 新建文档与改分辨率时置 `EditorUICanvasView::bFitPending`，画布 tab 在拿到自身尺寸
  后 `fitTo`（打开时 tab 可能还没布局）；Fit 按钮手动触发。
- 层级右键：先选中该节点，菜单项取 UI root 的 `selection.duplicate` / `selection.delete`（与
  Ctrl+D / Delete 同一 action；UI 页 Edit 菜单的 Duplicate 也因此可用）。`duplicateWidget` 复制子树与
  父 slot 状态，插在原控件之后并选中副本，名字加 `_copy`。
- Palette：选中控件没有 layout（叶子）时插到它之后；文档根没有 layout 时拒绝并告警。
- 验证：`ya-testing` 1407 通过 / 1 跳过；`ya-gui-closure-test` 614 通过；HelloMaterial、GreedySnake
  编辑器 120 帧 exit 0（smoke 不进 UI 页，工具条 / 右键菜单 / 设计框需手测）。
- 手测（待用户）：打开 `.yaui` 后画布自动缩放到设计框居中；切 1920×1080 / 竖屏后锚点布局按新尺寸
  重排并重新适配；改宽高数值框下拉变 Custom；层级右键 Duplicate / Delete 各一步撤销；选中 Text
  点 Palette 的 Button，Button 出现在 Text 之后而不是里面。
