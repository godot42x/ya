# GUI 内核体验 + ImGui 工作流

## 目的

C0–C3 已收口目录与 `EditorSurface` 编排。本线补 **内核交互手感** 和 **编辑器工作流接线**：closure dump 测试通过不等于用起来像以前的 ImGui 编辑器。

## 硬边界

1. 不合并 `GUIWindowHost` 与 `ya::App`。
2. 不按行数拆 `WidgetTree` / `UILayout` / `GUIAppHost`。
3. 不接 Phase 10F PropertyHandle、Windows/MSVC、OpenGL presentation。
4. 不把 ColorEdit 改成 `UICompoundWidget`。
5. 不为 1–2 个文件发明新目录。
6. 不停靠 tab 拖与浮窗标题拖抽成同一 helper（手势不同）。
7. 不宣称 retained editor ready。

## 口径

| 层 | 含义 | 本线验收 |
|---|---|---|
| 路径存在 | retained 有一条能完成核心工作流的 UI | 不够 |
| 手感等价 | 选区 / 选色 / dock 关 tab / 树右键 接近 ImGui 日常操作 | 必须 |

`ya-gui-closure-test` 断言 dirty / route / snapshot dump。Gallery scenario 与 editor 手测才是 UX 门禁。

## Checkpoints

- **K0**：冻结缺口（文档 + skill + parity 表）
- **K1**：typed drag 唯一 payload（代码已在 `1c66af41`；本线记录证据）
- **K2**：`UITextField` 选区；DragFloat/SpinBox 编辑态复用；I-beam
- **K3**：ColorEdit SV/hue/hex 选色器
- **K4**：Dock 叶内 tab close + 同 leaf 重排
- **E1**：Hierarchy 树右键 CRUD（现有 ActionMap）
- **E2**：Toolbar / Content Browser 图标
- **E3**：UI Designer 树 DnD；删 TypeRenderer / ImGui FilePicker 死路径

每个 checkpoint 一次 `[gui] ...` 提交；禁止「只过 dump 单测」写成完成。
