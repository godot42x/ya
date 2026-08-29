# GUI layout unified todo

- [x] CP1 冻结 UIConstraints + size mode + UILayoutIntent 协议
- [x] CP3 新增 UICanvasLayout + UICanvasSlot；UIPanel 改为 Canvas layout host
- [x] CP3 public DSL：`ui::layout()` 能力化 + `parent[spec >> widget]` + 全量调用点迁移
- [ ] CP2 从 UIElement 全量移除 authored geometry（`_anchorMin/_anchorMax/_position/_size/_min/_max/_bAutoSize`、`setPosition/setSize/getPosition/getSize`、`computeAnchorRect`、`reportStretchAnchorsIgnored`）
- [x] CP3 Canvas slot 补全：四边 `FMargin` offsets、alignment、width/height size mode（min/max 已有）
- [x] CP3 收尾：pivot、preferred size、`ui::canvas()` 宿主 DSL（CP3 全部完成）
- [x] CP3/§3.4 capability 编译期隔离（`column[anchor(...) >> w]` 已编译失败）
- [ ] CP4 所有 layout host 统一 typed slot arrange（Box/Overlay/SingleChild/Split/Scroll/Grid/Canvas）+ reparent/detach slot 重建
- [x] CP5 部分：`UIElement` layout-host 钩子（消除运行期 path-A/path-B 二分）+ panel 子节点与 designer 回归修复
- [ ] CP5 剩余：tree root/layers 改 canvas host（影响 217 处 `attachToLayer`，需整体迁移）、DockSpace `_previewOverlay`、Popup、测试直写
- [x] CP6 序列化侧旧锚点清理（`SceneWidgetEntry` 死条件、过期 `smoke.yaui`）；`serializeFields` 早已不输出锚点，无旧 schema 文件残留
- [ ] CP6 剩余：Designer inspector / 快照 dump 中若出现新 slot 字段需同步（当前无残留）
- [ ] 既有故障（非本计划引入，待你决定）：`UIDocument::instantiate` 传 null fields 给 `UIPanel::deserializeFields` 抛 `type_error.307`，致 5 个 `GameUIHostTest` 失败
- [ ] CP7 编译期断言 + 几何测试（intrinsic measure、constraints、Canvas 四边 offsets、anchor span、alignment、min/max、reparent）+ snapshot parity
- [ ] 清理过渡物：`ui::panelSlot()`/`FCanvasPanelSlotBuilder`、别名 `FCanvasPanelSlotArgs`/`UICanvasPanelSlot`、legacy `child(node)` 默认重载
