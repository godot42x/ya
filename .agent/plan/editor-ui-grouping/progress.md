# 进度

## 2026-09-18 — G1 落地

### 做了什么

把 `Engine/Source/Applications/GameEditor/UI/` 与
`.../include/GameEditor/UI/` 从平铺改为 7 个关切组，公开路径变为
`GameEditor/UI/<Group>/<Name>.h`。

- 搬迁：**91 rename + 1 create**（48 头 + 41 源 + 2 个 dock layout JSON；
  新建私有头 `UI/Dock/EditorDockSupport.h`）。
- include 重写：**90 个文件、201 行**（源 + 公开头 + 测试）。
- 顺手改名：`EditorViewportOverlayHost.cpp` → `Viewport/EditorViewportHost.cpp`
  （它的头一直叫 `EditorViewportHost.h`，源文件名与头不一致）。
- `xmake.lua` 补 `add_headerfiles("**.h")`：该 target 此前没有任何私有头，
  缺了这条私有头不会进 IDE 与安装清单。

搬迁形态：目标路径全部是新路径，所以**不需要** S1 那种"先删镜像、再搬真身"
的两次提交；`git diff -M --summary` 直接报 rename，blame 自然延续。

### 顺带修掉的真问题：unity build 重复 TU 符号

分组重排了 `add_files("**.cpp")` 的 unity 批次，`unity_7` 里同时落进了
`EditorNativeTearOff.cpp` 与 `EditorWindowLayout.cpp`，两者各自私有定义了
`FEmptyGuiDelegate` 与 `dockHasPanels` → 重复定义。

平铺布局下这两个文件恰好在不同批次，冲突被掩盖。这不是搬家的副作用，
是**被平铺掩盖的既有隐患**。

处理：抽私有头 `UI/Dock/EditorDockSupport.h`（`FEmptyGuiDelegate` +
inline `dockHasPanels` / `sessionHasDockPanels`），两个 .cpp 同目录裸文件名
include。`EditorNativeTearOff.cpp` 里的同义函数 `editorWindowHasPanels`
改名为 `sessionHasDockPanels`，避免"同义不同名"。

未采用加命名空间的方案：同文件里 `hostDockOnTree` 等仍按全仓库风格
非限定调用，单独给两个符号加限定会让该文件风格分裂。

### 测试/守卫修复

两轮脚本化改写 `Engine/Test` 里的源码守卫（这些守卫 `readEngineSource(...)`
后 grep 路径）：

1. 因本轮搬迁而失效的 4 个文件（EditorDocumentSessionTest、
   EditorWindowLayoutTest、EditorWindowSessionTest 26 处、GUIWindowChromeTest 8 处）。
   脚本**故意跳过 `.agent/plan/*`**：计划文档允许保留历史为真的路径。
2. **S1 遗留**：3 个文件、7 处路径是上一轮（`source-layout-subtraction` S1）
   打坏的，当时我的滤镜没跑到这些测试，所以没有暴露。按 basename 解析回真实位置：
   - `Source/Framework/GUI/Host/Window/GUIWindowChrome.h` →
     `.../GUI/Host/include/GUI/Host/GUIWindowChrome.h`
   - `Source/Framework/GUI/Runtime/Widgets/Controls/DockSpace/DockContext.h` →
     `.../Widgets/include/GUI/Widgets/Controls/DockSpace/DockContext.h`
   - `Source/Applications/GameEditor/Input/EditorInputNode.h` →
     `.../include/GameEditor/Input/EditorInputNode.h`

### 验证

| 检查 | 结果 |
| --- | --- |
| `xmake b ya-game-editor` / `ya-testing` / `ya-game-runtime` / `ya-engine` | 全部 ok |
| `GameEditor/*` include 解析 | 291 处全部解析，0 旧路径 |
| 平铺路径残留（全仓，排除 `build/` 与 `.agent/plan/`） | 0 |
| 搬迁 blame | `git diff --cached -M --summary` = 91 rename + 1 create |
| editor + 守卫滤镜 | 202 passed / 3 failed（均为基线既有） |
| 全量比对 HEAD 基线（`git stash` 探针） | HEAD 1230 passed / 14 failed → 现在 1234 passed / 10 failed；**0 新增回归** |

本轮**修好**的 4 条此前失败守卫：

- `EditorWindowSessionTest.DockContextDoesNotKnowEditorRoots`
- `EditorWindowSessionTest.InputRoutesByWindowId`
- `GUIRenderSurfaceTest.ComposeTargetDoesNotAcquirePresentOrReadLiveTree`
- `GUIWindowChromeTest.DockAndEditorDoNotIncludePlatformNonClientApis`

与本轮无关的既有失败（HEAD 上同样失败，逐个确认）：
`GUIWindowManagerTest.DragOverlaySessionIsExemptFromFocusAndInput`、
`ToolControlsTest.SplitPaneDividerDragChangesRatioAndEndsSession`、
`ToolControlsTest.ScrollViewportNestedInsideSplitKeepsCustomLayout`、
`EditorPropertyGraphTest.AutoPropertySectionAssetPathCommitBrowseAndUndo`、
`EditorPropertyGraphTest.TextureAssetRowShowsRetainedPreview`、
`ScriptApiLibraryFixture.GameUIWidgetLifecycleThroughRegistry`、
`RenderGraphCoreTest.ResourceRegistryUsesProvidedImportedImageViewAndRetainsOwner`、
`GameUIHostTest.BuildSnapshotComposesMountedWidgets`、
`GUIHeadlessHostTest.ReusesAppKernelAndBuildsSnapshotsWithoutWindowOrRhi`、
`GUIHeadlessHostTest.UnthemedFallbackThenThemeSwitchRepaintsSnapshot`，
外加两个 SIGTRAP：`WidgetLayoutTest.DockTabBarStripDoubleClickFiresHostCallback`
（`WidgetTree::beginPointerDispatch` 断言，自 `e1c93a0e`）、
`WidgetTreeTest.SystemLayersCannotBeDetached`（`WidgetTree::detach` 断言，自 `923527789`）。

另：`ya-gui-widgets-test` 编译失败与本轮无关——target 依赖缺 `ya-gui-compose`，
而 `GuiFrameInspectorTest.cpp` include 了 `GUI/Compose/GuiFrameInspectorOverlay.h`。

### 未完成 / 下一步

- G2：把分组契约写进 skill（`code-reorganize` 的目录规则 + `gui-framework` 的 UI 布局约定）。
- G3：`Engine/Test/Source`（114 文件）、`Render3D/Common`（33）、
  `GUI/Runtime/Widgets/Controls`（32/31）、`RHI/Core`（26）同标准评估，
  每条先说明"解决哪个读者问题"。
