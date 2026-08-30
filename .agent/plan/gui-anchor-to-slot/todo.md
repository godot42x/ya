# GUI layout unified todo

## 架构结论（2026-08-31 修正）

- **不是把所有“布局相关代码/状态”机械搬到 slot。** 正确边界是：每个 child 的 authored placement/layout intent 唯一归属 parent-owned `UISlot`；anchor、offset、margin、alignment、size rule、preferred/fixed size、min/max、grid cell 以及 parent 内 z-order 都放在 slot。
- `UILayout` 仍是父节点的布局算法与策略唯一归属，负责 measure/arrange 以及容器级配置：方向、spacing、padding、split ratio、scroll offset、overlay policy、轨道/行列规则等不得下沉到 slot。slot 是 parent-child edge 的数据与约束，不是第二套 layout 算法。
- `UIElement/Widget` 只提供 intrinsic/content measurement、baseline、控件内部视觉状态；最终 `_layoutRect`、clip、snapshot 几何属于 runtime，不是 authored 字段。
- 不照搬 Slate 的 DSL，也不把 slot 暴露成必须显式书写的第二个节点。保留 YA 的 `ui::layout() >> child` / `parent[...]` 能力化语法：layout spec 在 child builder 上声明，attach 时由 parent 根据 capability 创建/消费 typed slot；DSL 表达的是 edge intent，所有权仍归 parent。builder 的 `.setSize/.setPosition` 只能作为过渡 sugar，最终必须转换为 parent edge intent，不能写入 widget geometry。
- standalone root 也必须拥有 host/layer-owned root slot；不得以 child `_anchor*` / `_position` / `_size` / `_bAutoSize` 作为长期 root 布局真值。
- SizeToContent 由 slot size mode 决定：Canvas 按轴 Auto、Box 的 Auto/Fill、SingleChild/Overlay 的 Fill 或 desired 对齐；widget 只报告 intrinsic/desired size。

### 该边界带来的 API/编译期约束

- child builder 可以携带 layout intent，但不能携带任意 parent 专属字段；`column[anchor(...) >> child]`、`canvas[weight(...)]` 等无能力匹配的组合必须在模板实例化期失败，LSP/编译器据此提示可用参数。
- 不同 parent 继续拥有不同 typed slot（Canvas/Box/Overlay/SingleChild/Table 等），但调用入口统一为 `parent[layoutSpec >> child]` / `.child(child, args)`；不要求用户手写 `canvasSlot()`，也不为每种 parent 维护一套平行 DSL。
- `UISlot` 不承担 child 的 intrinsic measurement，也不保存运行时 rect；`UILayout` 不反向写回 widget authored geometry。任何需要跨 parent 复用的字段必须先证明是 edge intent，否则留在对应 layout host 或 widget。
- 迁移完成的验收标准从“slot 字段数量覆盖率”改为职责不变量：serialized/authored child geometry 只能出现在 slot；layout 算法只能读取 slot + child intrinsic；widget geometry API 不再是生产布局输入；root/layer attach、reparent、snapshot 和 designer 都遵守同一 edge contract。

### 计划方向修正

- CP2 的最终验收不是“把所有布局字段搬到 slot”，而是“所有 child authored placement 在 slot、父级算法在 layout、内容测量在 widget”；不得以把容器策略塞进 slot 的方式达成表面覆盖。
- CP2 删除 UIElement authored geometry 前，必须先完成 root slot contract 与 builder pending edge intent；否则会误伤独立 root 的合法入口。
- `_bAutoSize`、`setSize()`、`setPosition()` 目前只能保留为过渡桥，禁止新增运行时依赖；删除前需完成所有消费者迁移与 root slot 化。
- CP7 增加 root-slot、builder-to-slot、slot size-mode 驱动 invalidation，以及 widget intrinsic 不携带 authored geometry 的断言。

- [x] CP1 冻结 UIConstraints + size mode + UILayoutIntent 协议
- [x] CP3 新增 UICanvasLayout + UICanvasSlot；UIPanel 改为 Canvas layout host
- [x] CP3 public DSL：`ui::layout()` 能力化 + `parent[spec >> widget]` + 全量调用点迁移
- [x] CP2 前置分层：新增 `computeIntrinsicSize()`，显式区分 widget 自身固有尺寸与 layout 聚合 desired size
- [x] CP2 intrinsic 迁移：补齐 `UITextField::computeIntrinsicSize()`，文本/字号变化改为触发布局失效
- [x] CP2 intrinsic 迁移：`UITreeView` / `UITableGrid` 非 AutoSize 路径改由显式 intrinsic contract 提供 authored fallback
- [x] CP2 intrinsic 迁移：`UIText` wrapping 无显式宽度时不再借用 child `_size.x`，改由 intrinsic 单行宽度与 assigned slot rect 分工
- [x] CP2 纠偏：layout 从 slot 读 authored size，不再把 child `_size` 当布局输入；`resolveCanvasRect` 仍是 canvas 锚点入口（传入 slot authored size / Auto）；`computeDesiredSize` 只报告内容。`_size`/`setSize` 字段仍在，待全量删除
- [ ] CP2 从 UIElement 全量移除 authored geometry（`_anchorMin/_anchorMax/_position/_size/_bAutoSize`、`setPosition/setSize/getPosition/getSize`、`computeAnchorRect`、`reportStretchAnchorsIgnored`）；`min/max` 约束只保留在 typed slot
- [x] CP2 运行时入口纠偏：GameUIHost/DefaultGameUIController 删除从 widget 几何反推 layer slot 的死辅助；scene entry 只使用显式 rootSlot
- [x] CP2 前置缺口：SceneWidgetEntry 顶层 entry→root 已改为独立的 parent-owned root slot；UIElement 根节点 geometry 删除仍待 CP2 全量清理
- [x] CP2 前置收口：SceneWidgetEntry 根节点使用 parent-owned `rootSlot`；旧 widget serialized files 不迁移，直接删除；新 schema 缺少 rootSlot/childSlots 时严格拒绝
- [x] CP2 前置：UIDocument 持久化 parent-owned child slot intent（canvas/box/overlay/single-child/table），实例化时按 parent typed slot 恢复；无 child geometry 兼容字段
- [x] CP6 schema guard：UIDocument 严格校验 `children` 与 `childSlots` 一一对应，禁止 slot edge 数据错位或静默丢失
- [x] CP6 slot coverage：UIDocument round-trip 覆盖 Overlay / SingleChild / Table typed slot intent
- [x] CP6 纠偏：SceneWidgetEntry nested document reparent/reorder 原子搬运 childSlots，避免 parent-edge intent 与 child 文档错位
- [x] CP2 parent-owned arrange 收口：`UILayout::assignChildRect()` 不再读取 child anchors，删除 `reportStretchAnchorsIgnored()` / `hasStretchAnchors()` 死桥
- [x] CP3 Canvas slot 补全：四边 `FMargin` offsets、alignment、width/height size mode（min/max 已有）
- [x] CP3 收尾纠偏：删除过渡 public API（`ui::panelSlot()` / `FCanvasPanelSlotBuilder` / 旧 canvas-panel 命名）
- [x] CP3/§3.4 capability 编译期隔离（`column[anchor(...) >> w]` 已编译失败）
- [ ] CP4 所有 layout host 统一 typed slot arrange（Box/Overlay/SingleChild/Split/Scroll/Grid/Canvas）+ reparent/detach slot 重建（ScrollViewport 的 fake-unified single-child slot 已纠偏；SelectableRow 已收成 single-child host）
- [x] CP4 纠偏：same-parent `reparentBefore/After/reparent(parent, child)` 改为移动原 edge，保留 slot 状态
- [x] CP4 计划纠偏：`Grid/Table` capability 暂收窄为 `cell-only`；待 `UITableSlot` 具备更多 runtime contract 后再开放
- [x] CP3/CP4 收口：Declarative edge attach 改为 inline slot init，`[]` 保持 child 语法糖，slot ownership 仍归 parent
- [x] CP5 部分：`UIElement` layout-host 钩子 + panel 子节点与 designer 回归修复
- [x] CP4/CP5 纠偏：删除 `applyLayoutSpecToSlot(box)` 对 child `setSize()` 的回写，避免继续依赖 child-authored geometry
- [x] CP4 纠偏：补齐并验证 unified `ui::layout()` 对 `UISingleChildSlot` / `UIOverlaySlot` 的运行时消费
- [x] CP5 纠偏：TreeRoot 改为 canvas host，system layer fill 迁到 root->layer `UICanvasSlot`
- [x] CP5 设计前置：layer typed host/edge 契约已落地，`attachToLayer(layer, widget, FCanvasSlotArgs)` 可显式表达 edge intent
- [x] CP5 纠偏：system layer 升为 canvas host，`attachToLayer()` 自动桥接 child canvas geometry -> `UICanvasSlot`，`setPosition()` 桥接到 slot offset
- [x] CP5 纠偏：`attachToLayer()` / `setSize()` / Designer drag 补齐 canvas edge `fixedSize` 桥接，layer child 不再只迁移 position
- [x] CP5 收口：`PopupOverlay` 作为独立 full-screen host，content edge 改为 popup-owned `UICanvasSlot`；Menu/Dialog 不再手写 `layoutAssigned()`
- [x] CP5 纠偏：`UIDesigner` 连续拖拽从 canvas slot 读取起始 `offset/fixedSize/anchor`，不再混用陈旧 child 字段
- [x] CP5 纠偏：Menu 行 / MenuBar 项 / Dialog 面板尺寸改由 typed slot 持有（box preferredSize / popup canvas preferredSize）
- [x] CP5 纠偏：Workbench/Editor chrome 固定高度与 preview highlight 几何改由 canvas slot 持有
- [x] CP4/CP5 纠偏：`UISelectableRow` 收成 single-child host；box `setSize` 桥 `preferredSize`；popup `_contentExtent` 提到 overlay；`ui::build(spec >> widget)` 在 attach 时写 edge
- [x] CP4/CP5 纠偏：`UICompoundWidget` / `UICheckBox` 收成 single-child host；`FBoxSlotArgs::preferredSize` 补齐；inspector 行尺寸写 box edge
- [x] CP4/CP5 纠偏：attach 把 authored child `setSize`/`setPosition` 种到 box preferredSize / canvas fixedSize+offset；layout spec 覆盖种子
- [x] CP4/CP5 纠偏：`installLayout` host 的 `createSlotForChild` 走 layout 工厂；`UIDockSpace` 收成 single-child host；`UIDockFloatingHost` 收成 canvas host，window rect 落到 host-owned canvas slot
- [x] CP4/CP5 纠偏：`UIPopupOverlay` 安装 canvas layout 并删掉手写 content rect；`UIDockFloatingWindow` 收成 overlay host，resize handle 走 overlay slot
- [x] CP5 纠偏：Dock preview overlay 删除 dead child-authored anchors/position/zero-size
- [x] CP5 审计纠偏：测试直写多数保留为合法夹具/absolute/layer-child 语义；tree/layer 路径中已失效的 child-owned zero-size 写入已清理
- [x] CP5 收口：`attachToLayer(layer, widget, FCanvasSlotArgs)` 直接写 parent-owned canvas slot；explicit path 不再回写 child 几何，Designer 读取 slot 作为真值
- [x] CP5 纠偏：EditorSurface / UIDesignerPanel / GameUIHost / DefaultGameUIController 的挂载入口改为显式 canvas args，不再依赖 no-arg legacy attach 作为运行时主路径
- [x] CP5 运行时入口收口：GUIFrameworkSmoke 与 DockSpace drag-preview 改用显式 parent-owned canvas args，生产代码不再新增 no-arg layer attach 依赖
- [x] CP2/CP5 纠偏：subtree attach 不再把 child authored geometry 种到 slot；相关 snapshot / routing / popup 测试改成显式 slot 初始化
- [x] CP2/CP5 纠偏：已知 canvas host 的 Workbench highlight / drag ghost 删除 child-geometry fallback；ToolControls 测试夹具统一改为显式 slot intent
- [x] CP6 序列化侧旧锚点清理（`SceneWidgetEntry` 死条件、过期 `smoke.yaui`）；`serializeFields` 早已不输出锚点，无旧 schema 文件残留
- [ ] CP6 剩余：Designer inspector / 快照 dump 中若出现新 slot 字段需同步（当前无残留）
- [x] CP6 Designer entry inspector：Position/Size 直接编辑 `SceneWidgetEntry::rootSlot`，不再写 `_position/_size` instance overrides
- [x] CP6 Designer direct manipulation：非 Canvas parent 不再回退写 UIElement 几何，改为显式拒绝并要求 typed slot 编辑
- [x] CP2 序列化边界：新生成 UIDocument v2 fields 剔除 UIElement `_position/_size/_bAutoSize`；旧 geometry fixture 直接删除并改用 root/child slot
- [x] CP2 内部控件迁移：Dialog / TabBar 不再通过 UIElement `_bAutoSize` 表达 parent-owned 内容尺寸，改由 typed slot desired-size contract 承接
- [x] CP2 specialized intrinsic 迁移：TreeView / TableGrid 在已挂载 parent slot 时始终报告内容尺寸， authored fallback 仅保留给 detached root
- [x] CP2 SizeToContent 语义收口：新增 \`UIElement::isAutoSizeActive()\`，UIText 的 invalidation/paint 按 parent slot size mode 判定，detached root 才回退 \`_bAutoSize\`
- [x] CP2 authoring 边界收口：UIElement 反射/authoring schema 不再暴露 \`_position/_size/_bAutoSize\`；运行时过渡字段仅待 root-slot 化后删除
- [x] 既有故障（非本计划引入）：`UIDocument::instantiate` 传 null fields 时 `deserializeFields` 需容错（`type_error.307`）；已修复并恢复 `GameUIHostTest` 5 项用例
- [ ] CP7 编译期断言 + 几何测试（intrinsic measure、constraints、Canvas 四边 offsets、anchor span、alignment、min/max、reparent）+ snapshot parity
- [ ] 清理过渡物：legacy `child(node)` 默认重载
- [ ] 计划状态纠偏：CP3 改为“主体完成但仍有过渡物”；CP5 改为“引入 layout-host hook，但 legacy self-positioned fallback 仍在”
- [x] 计划政策纠偏：旧 widget serialized file 不纳入迁移范围；示例中的未定版 `widgetEntries` 已删除，后续只生成新 schema 文件
