# Gallery 手感一轮（17 项）

## 目的

Feature Gallery 走查暴露的是 **手感/契约** 缺口，不是「控件不存在」。
closure dump 测试继续当契约门禁；本线把 Gallery 能点的路径修到可日常使用。

## 硬边界

1. 不合并 `GUIWindowHost` 与 `ya::App`。
2. 不把 ColorEdit 改成 `UICompoundWidget`（仍是单 widget + popup picker）。
3. Widgets 不 include AssetManager；磁盘贴图只在 Host `IGuiTextureSource` 里加载。
4. 不停靠 tab 拖与浮窗标题拖抽成同一 helper。
5. 不为 1–2 个文件发明新目录。
6. Modal **不等于** 遮罩：Modal = 独占输入直到完成/Esc；是否 dim 由 overlay `_bDimBackground` 交给应用。

## 分层

| 层 | 项 |
|---|---|
| Demo 接线 | 1 SelectableRow 选中同步、6 Box spacing/Fill、8 Theme 进菜单、9 Menu 锚点、12 Drag 标签宽度、17 离页拆 Popup host |
| 框架交互 | 3 hoverable、5 ignore-case、10 modal≠dim、11 Dialog wrap、13 disabled 走 `isEnabledInTree`、14 Tree expand Layout dirty |
| 视觉 | 2 Check 用 Line、16 Dock chooser 始终画五块 |
| 能力 | 4 ColorEdit = swatch+RGBA 字段 + SV 分层渐变、7 Host 加载 file: 贴图、15 Table 列/行拖拽改尺寸 |

## 验证

```
xmake b GUIWorkbench ya-gui-closure-test
xmake r ya-gui-closure-test -- --gtest_filter='ToolControlsTest.*:WidgetTreeTest.Modal*:UIFrameSnapshotTest.TreeView*'
xmake r GUIWorkbench --headless --smoke-actions
# 按页 scenario：Inputs/Dialog/Enable/Tree/Table/Widgets/DragDrop
```
