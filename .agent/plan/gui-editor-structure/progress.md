# Progress

## C0 当前 checkpoint（2026-09-06）

- 建立 `gui-editor-structure` 计划工件。
- 修正 `gui-framework` skill：模块地图改为 `Host/` + `ya-gui-host`；Draw2D/fonts 归 `Framework/Render`；写清 `AppKernel` 下 GUI-only 与 GameEditor 两条产品循环，以及 `EditorSurface::tick` 契约。
- editor-readiness `todo.md` 的 10E 其余 tab owner 交叉引用到本线 C2。
- 验证：文档对照；本 checkpoint 不改 C++。

### C0 保留项

- C1 leftover panel 删除未做。
- C2 tab owner 未做。
- 不合并 `GUIApp` 与 `ya::App`；不拆 WidgetTree / UILayout / GUIAppHost。
- 未宣称 retained editor ready。
