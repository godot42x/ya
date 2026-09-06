# Editor dock layout as data + Window menu

Layout persist/restore already exists (`editor.dockLayout` + `FDockContext` JSON).
This line removes hardcoded workspace splits and adds a Window visibility menu.

## Checkpoints

- **C1**：factory `DefaultEditorDockLayout.json`；`applyLayoutDocument` spawn known keys / sanitize unknown / import / factory fallback；删除 `kDefaultWorkspaceTabs` 与 `applyDefaultLayout` C++ splits
- **C2**：全部 tab 可关闭；Window 菜单 checkbox + Reset Layout；`invokeTab` 落到 last focused leaf；去掉 Tools 的 tab 列表

## 明确不做

- Named user layouts（Save Layout As…）
- 记住已关闭 tab 的几何
- 拆 `EditorSurface::tick` / Surface 持 tab 指针 / `EditorPanel` / 总线
- 改 dock JSON schema（`version` 仍为 1）
- 把 Workbench 加回 GameEditor dock
