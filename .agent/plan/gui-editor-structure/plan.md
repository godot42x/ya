# GUI + Editor 结构收口（克制版）

## 目的

让 GUI Framework + GameEditor 能顺着读完整 orchestration：`EditorSurface` 只做编排，tab 各自有 owner，目录不再说谎。不合并 `GUIApp` 与 `ya::App`。

## 硬边界

1. 不合并 `GUIWindowHost` 与 `ya::App` 的 present / 输入栈。两条产品线有意分叉：Workbench 保持 GUI closure；Editor 吃 3D viewport + swapchain。
2. 不按行数拆 `WidgetTree` / `UILayout` / `GUIAppHost`。它们的价值是顺着读完整时序。
3. 不接 Phase 10F PropertyHandle、Windows/MSVC、OpenGL presentation。
4. 不把 `UIDesignerPanel` 预览文档模型塞进 widget。
5. 不为 1–2 个文件发明新目录。
6. 不把 `UICompoundWidget` 当成 specialized control 的归宿。
7. 不把无关工作区改动（尤其 PropertyHandle）卷进本线提交。

## 产品循环（当前事实）

- 唯一 while-loop：`AppKernel`
- GUI-only：`GUIApp` + `GUIWindowHost`（`Framework/GUI/Host/`，target `ya-gui-host`）
- 游戏/编辑器：`ya::App` + `GameRuntimeFrameOrchestrator`；编辑器是 module，经 `onPresentation` 挂 `EditorSurface`

```text
AppKernel
  ├─ GUIWindowHost  → FWorkbenchSurface → WidgetTree::buildSnapshot → compose/present
  └─ ya::App → GameRuntimeFrameOrchestrator → EditorModule::onPresentation
       → EditorSurface::tick → WidgetTree::buildSnapshot → replayUIFrameSnapshot
```

## EditorSurface 目标契约

```text
tick: rebuild-if-needed -> window metrics -> sync tabs/chrome -> buildSnapshot -> viewport host
rebuild: new tree/theme/dock -> shell chrome -> tab.build() 挂进 FDockContext
```

Hierarchy / Viewport / Menu / Dock persist / dialogs 留在 Surface（chrome 编排）。Content / Asset / UI Designer / Runtime Tools 必须是独立 tab owner。

结构面 C0–C3 已完成。内核手感与 ImGui 工作流接线见 `.agent/plan/gui-kernel-ux-parity/`。

## Checkpoints

- **C0**：口径与编排图（skill + 本计划工件，不改 C++）
- **C1**：删除说谎 leftover panel；搬迁 `migrateLegacyRuntimeSettings`
- **C2**：抽出 tab owner；去掉 `EditorTabRegistry` callback 袋
- **C3**：拆 `InputExtras`；Dock drag 仅在真重复时抽 helper

每个 checkpoint 必须有代码或文档闭环、验证和一次 `[gui] ...` 提交。

结构面 C0–C3 已完成。内核手感与 ImGui 工作流接线见 `.agent/plan/gui-kernel-ux-parity/`。
