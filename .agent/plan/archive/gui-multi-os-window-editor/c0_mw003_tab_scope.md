# MW-003 旧 Editor tab 登记与 scope

> 2026-09-08。只读登记，不移动文件。来源：[`EditorTabSpawnerRegistry.cpp`](../../../Engine/Source/Applications/GameEditor/UI/EditorTabSpawnerRegistry.cpp)、[`DefaultEditorDockLayout.json`](../../../Engine/Source/Applications/GameEditor/UI/DefaultEditorDockLayout.json)。

## 结论

今天 **没有** Level/Material/Script 三个 `WindowRootEditor` tab。整窗 `EditorSurface` + 扁平 `FDockContext` 就是隐式 Level Editor；`ui-designer` 已是 document 编辑器，却和 Content/Stats 塞在同一 leaf。迁移时必须先长出 Level Editor 作为不可关闭的 window-root，再把 owned tool 放进它的 nested dock。

所有 builtin tab 都是 `UICompoundWidget`。`WidgetTree` 负责 attach/detach/tick；部分 tab 另有 `tick()` 轮询 Layer，不应再持有 `EditorSurface*`。`FEditorTabSpawnContext` 仍绑定单窗 tree/selection/undo/viewport。

JSON identity 即当前 `tabId` / dock `stableKey`。旧 `editor.dockLayout` 只恢复 main window 扁平 panel。

## 已注册 tab

| tabId | 类型 | 目标 scope | owner | document | viewport | selection | undo | App/RHI | singleton | 自 tick | 可独立 OS 窗 | 关闭 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `viewport` | `EditorViewportTab` | EditorOwnedTool | LevelEditor | 当前 scene | 是（`IEditorViewportHost`） | 否 | 否 | sink→Surface | 每 Level 一份 | 否 | 否（C5 第一阶段跟 Level 锁定） | 不可关 |
| `hierarchy` | `EditorHierarchyTab` | EditorOwnedTool | LevelEditor | scene 实体 | 否 | 是（Layer+SelectionModel） | 否（ActionMap） | 否 | 每 Level 一份 | 否 | 可 tear-off，保留 owner | 可关，可回 nested |
| `inspector` | `EditorInspectorTab` | EditorOwnedTool | LevelEditor | 实体/component | 否 | 经 Layer | 是 | 否 | 每 Level 一份 | 是 | 可 tear-off，保留 owner | 可关 |
| `content-browser` | `EditorContentBrowserTab` | WindowTool | 无 | 无 | 否 | 否 | 否 | Layer（reveal/picker） | 每 window policy | 是 | 可按 policy | 可关 |
| `runtime-tools` | `EditorRuntimeToolsTab` | WindowTool | 无 | 无 | 否 | 否 | 否 | 设置段读 `App::get()`/`getSwapchain` | 每 window | 是 | 可 | 可关 |
| `ui-designer` | `EditorUIDesignerTab` | WindowRootEditor | 自身 | UI document | 预览 offscreen | Designer 自己的选区 | 是 | Layer | 每 `uiKey` | 是 | 可（root tear-off） | 走 document close |
| `asset-inspector` | `EditorAssetInspectorTab` | WindowTool | 无 | 当前 inspect path | 缩略图 | 否 | 否 | Layer | 每 window | 是 | 可 | 可关 |
| `debug-images` | `EditorDebugImagesTab` | WindowTool | 无 | 无 | debug RT | 否 | 否 | Layer | 每 window | 是 | 可 | 可关 |
| `frame-stats` | `EditorStatsTab` | WindowTool | 无 | 无 | 否 | 否 | 否 | Layer | 每 window | 是 | 可 | 可关 |

## 尚未注册、计划需要的 root

| 身份 | scope | 现状 | 迁移 |
| --- | --- | --- | --- |
| Level Editor | WindowRootEditor | 无 tabId；`EditorSurface` 即整窗 chrome | 新增非关闭、不可 tear-off（C5）的 root；nested：viewport/hierarchy/inspector |
| Material Editor | WindowRootEditor | 无 spawner | C6；owned Preview/Parameters |
| Script Editor | WindowRootEditor | 无 spawner | C6 |

## 当前布局事实

`DefaultEditorDockLayout.json` 把 `hierarchy` / `viewport` / `inspector` 与 `content-browser`、`ui-designer`、stats 全部放进 **同一个** window-root dock。`ui-designer` 与 Content 同 leaf 堆叠，不是独立 root。`floating: []`。

`EditorDockWorkspace` 按 `tabId` materialize；未知 key sanitize；persist 写 `editor.dockLayout`。

## 依赖与违规点

- `EditorInspectorTab` / `EditorUIDesignerTab` 拿 `UndoStack*`；Surface 还 `dynamic_cast<EditorInspectorTab*>` 做 IME。
- `EditorRuntimeToolsTab` 的 render settings 经 `App::get()` 改 **唯一** swapchain vsync（main-facade）。
- 多个 tab `tick()` 跟 Layer；生命周期仍应归 WidgetTree，tab 不得缓存 Surface、不得自建 loop。
- spawn 全部 `std::make_shared<...>` 进 dock panel widget；destroy = dock close + tree detach。无独立 native teardown。

## 本 checkpoint 边界

- 保留：现有 tabId 与 factory JSON 作为兼容 identity。
- 未完成：真正改 spawn context / nested dock（C4/C5/C6）。
- 偏离：无。未移动文件。
