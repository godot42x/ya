# Progress

- G1 SelectableRow：Gallery presenter 在 `_onSelect` 里同步所有行的 `setSelected`。
- G2 CheckBox：勾选标记改为两条 `addLine`，不再用小方块 stamp。
- G3 DragFloat/SpinBox：`isHoverable` + hover fill；SpinBox +/- hover 对比加强。
- G4 ColorEdit：左侧 swatch + RGBA 拖拽字段；picker SV 用分层 sprite 近似渐变。
- G5 SearchCombo / TreeView filter 默认 ignore-case，可改 Sensitive。
- G6 HBox 子格 Fill，spacing 不再撑破窗口；VBox 绑定同一 slider。
- G7 Host `IGuiTextureSource` 用 stbi 加载磁盘/`file:` 路径；Brush 页加 Load 示例。
- G8 Theme 切换进 View 菜单；Theme 页改为 sub-style 样例。
- G17 离页 `FPage::leave` 拆掉 Dock Popup 层 floating host。
- G9 Open popup menu 锚在按钮下沿，不再写死 (300, 220)。
- G10 Modal 只独占输入；`_bDimBackground` 可选；modal 外点不关；Dialog 画 outline。
- G11 UIDialog 内容默认 wrap，窗口加宽以免 DescText 被 clip。
- G12 DragDrop tile 按标签测宽，不再把 asset.texture.diffuse 裁进 160px。
- G13 Button/SelectableRow 禁用态走 `isEnabledInTree()`（CheckBox 已随 G2）。
- G14 TreeView expand/collapse 同时 `markLayoutDirty`+`markPaintDirty`；`isExpanded` 始终创建 Layout Reactive（随 G5 TreeView.cpp 落地，本项补测试）。
- G15 TableGrid 列/行分隔条 ±3px 命中，拖拽改宽/高（stretch 列首次拖时物化）。
- G16 Dock drop chooser 五块始终画暗，hover/active 只加亮；不把 tab 拖与浮窗标题拖合成同一 helper。
