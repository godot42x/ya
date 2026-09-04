# GUI Framework / Game Editor Readiness Plan

## 1. 目的

本计划用于把当前 retained-mode GUI 从“Workbench 可运行、EditorSurface 可展示”推进到“能够稳定承载完整游戏编辑器”。

审计范围：

- slot-first layout
- retained WidgetTree / ownership / snapshot
- invalidate / dirty / incremental paint cache
- typed native DSL
- Reactive / ReactiveList / Computed
- Style / Theme / resource readiness
- GameEditor chrome、编辑器控件和编辑器数据模型

最终目标不是恢复或兼容旧 GUI，而是形成一套清晰的第一版架构：

```text
Editor/Game model
    ↓
commands / transactions / selection / reactive adapters
    ↓
retained widgets + typed DSL + behaviors
    ↓
WidgetTree layout / input / focus / invalidate / snapshot
    ↓
Render2D compose / window or offscreen presentation
```

## 2. 硬边界

1. 不保留任何 legacy 兼容层、兼容别名、旧布局字段、旧 builder 路径或双写逻辑。
2. 不恢复 `ui::layout()`、`FUILayoutSpec`、旧 capability DSL、Description → apply 作为静态 DSL 主路径。
3. 静态 DSL 直接构造 live WidgetTree；动态集合由列表控件内部管理 keyed row 生命周期。
4. Layout 由 parent-owned layout + typed Slot 管理；UIElement 不恢复 authored geometry 转发接口。
5. `Reactive` 不上升为 Runtime Kernel 的唯一状态模型；kernel 同时支持 imperative、behavior、transient、reactive 和 adapter patch。
6. 每个 checkpoint 必须有一个可运行目标、代码、测试、计划映射和一次分类提交；不得用文档、空壳或碎片提交冒充进度。
7. 本计划执行时，plan/progress/feature matrix/session checklist 与对应代码和测试必须同一 checkpoint 提交。

## 3. 当前基线（2026-09-03）

### 已具备

- Slot-first Canvas / Box / Overlay / Split / Scroll / SizeBox 等布局路径。
- UIElement 持有当前 Slot 指针，parent 持有 child edge。
- WidgetTree attach / reparent / detach、focus、pointer capture、popup、tooltip、drag/drop。
- Immutable `UIFrameSnapshot`，Render2D compose 与 headless/offscreen/windowed 回归路径。
- Paint/Layout/Subtree invalidation 分类、dirty transition 统计和 debug validation frame。
- typed static builder，父节点通过 `SlotArgs` 做布局参数约束。
- UITheme、typed style、sparse authored patch、theme generation。
- GUIWorkbench、EditorSurface 和部分 retained editor chrome。
- TreeView、TableGrid、Dock、Menu、TextField、输入控件等 editor 需要的基础控件雏形。

### 已确认的主要缺口

- paint cache 以裸 `UIElement*` 为 key，detach/destroy 后没有明确的 subtree cache 清理或 identity 防护。
- G2 incremental/full repaint 对比没有覆盖 texture、UV、font、corner radius、text scale 等所有绘制字段。
- layout dirty 是 tree 级全量重排，没有 subtree dirty、measure cache、arrange cache。
- ReactiveList 只有全量通知，缺 insert/remove/move/update/key/diff/transaction。
- Computed 每次读取都执行 selector，没有真正的上游依赖图、dirty cache、循环检测。
- Reactive mutation 的线程约束、re-entrant 行为、batch/transaction 语义未冻结。
- style key/field 主要是字符串 + 运行时反射，layout-affecting 与 paint-only 字段缺统一 schema。
- DSL 对动态条件、局部结构变更、keyed list 和复杂 editor 页面仍需手工 attach/detach。
- GameEditor 仍保留大量 ImGui/ImGuizmo 路径。
- selection、command、undo/redo、multi-object property projection、resource/error state 尚未与 retained GUI 形成统一闭环。

## 4. 执行流程

每个阶段固定执行：

1. 复述阶段目标、范围和明确不做项。
2. 先补契约测试或失败用例。
3. 只实现该阶段闭环所需的最小架构改动。
4. 接入至少一个真实 consumer（Workbench 或 GameEditor）。
5. 跑 closure、headless、windowed/offscreen、必要时 GPU parity。
6. 做 diff、测试、plan 映射和工作树检查。
7. 以 `[gui] ...` 提交一个完整 checkpoint。
8. 在 `progress.md` 记录保留项、未完成项、偏离项和下一阶段入口。

如果阶段验证发现方向错误，立即暂停编码，修订本计划和 feature matrix，不通过新提交掩盖偏离。

## 5. 阶段路线

### Phase 0：文档与契约收口

目标：让实现、skill、plan、测试名称表达同一套架构。

步骤：

1. 更新 `gui-framework` skill 中已过时的 `ui::layout()`、旧 path-B、`setSize/setPosition`、compatibility alias 等描述。
2. 将 slot-first、无 legacy、静态 DSL 直接物化、Reactive 分层、Editor 双栈迁移策略写成稳定规则。
3. 建立能力矩阵：layout、dirty、style、reactive、editor control、editor data、ImGui migration。
4. 清点现有 GUI 测试，标注“已覆盖 / 仅 demo / 缺失”。

验收：

- 新文档不再指导开发者使用已删除 API。
- 每个后续阶段都有唯一验收目标和测试入口。
- 不修改实现代码。

### Phase 1：Retained 生命周期与缓存正确性

目标：证明 attach/reparent/detach/destroy 后不会出现 stale snapshot 或 stale transient state。

步骤：

1. 为 WidgetTree 引入稳定 runtime identity 或等价 cache generation。
2. detach subtree 时清理两个 paint cache 中属于该 subtree 的段，或通过 generation 使其不可复用。
3. attach/reparent 后强制新 edge/subtree 重新建立必要的 paint/layout 状态。
4. 扩展 G2 validation，比较 `UIFrameDrawItem` 的全部渲染相关字段。
5. 覆盖 reparent、cross-tree move、same-parent reorder、destroy/reallocate、popup/tooltip/drag ghost detach。
6. 明确 snapshot build 期间的 mutation 禁止或 deferred policy。

验收测试：

- detach → destroy → 地址复用 → attach 不复用旧绘制段。
- reparent 后布局、clip、z-order、focus/capture/hover 正确。
- incremental snapshot 与 full repaint 完全一致。
- tree teardown 不留下 widget、slot、reactive dependent、GPU resource 残留。

### Phase 2：Invalidate taxonomy 与增量布局

目标：把当前“整棵树 layout dirty”升级为可扩展的 dirty graph。

步骤：

1. 明确 dirty 类型：structure、measure、arrange、paint、paint-context、resource、input-state。
2. 将 Slot setter、child desired-size 变化、父尺寸变化、visibility/style/font/resource 变化映射到明确 dirty 类型。
3. 在 WidgetTree 或 layout host 中记录 dirty subtree，而不是只记录 bool。
4. 为 measure 和 arrange 增加缓存失效条件与缓存统计。
5. 处理 detached subtree、reparent、layout host 替换、clip host 的传播。
6. 增加“只改 paint 不触发 layout”“只改 child intrinsic size 会触发 parent measure”的契约测试。
7. 在大树、大列表、Dock workspace 上建立性能基线。

验收：

- 非布局属性更新不再触发不必要的全树 layout。
- child desired size 变化一定能到达必要的 parent。
- clip/visibility/context 变化不会复用错误 cache。
- perf stats 能报告 dirty subtree、measure count、arrange count。

### Phase 3：Reactive 基础重构

目标：让 Reactive 能支撑编辑器数据流，而不是只做 paint-time getter。

步骤：

1. 冻结 UI thread、同步通知、重入和跨线程写入规则。
2. 增加 batch/transaction API，保证一个业务事务只触发一次有效更新。
3. 将 `ReactiveList` 升级为 keyed collection signal，支持 insert/remove/move/update/replace。
4. 明确 list row 的 key、复用、销毁和 transient state 保留规则。
5. 重写 `Computed`：首次求值收集依赖、上游变化标记 dirty、惰性重算、same-value 抑制、循环检测。
6. 统一 bind/unbind 生命周期，确保 detach/reparent/destroy 后不会回调失效 widget。
7. 为 selection、filter、split ratio、style generation、resource readiness 增加统一 adapter。

验收：

- 列表单行更新不重建整页。
- same-value 和同一事务内重复写入不会产生多余通知。
- computed 多层依赖能正确传播，循环依赖有诊断。
- 跨线程 mutation 被拒绝或安全转发到 UI thread。

### Phase 4：DSL 与动态结构能力

目标：保持 UI/layout 分离，同时让复杂 editor 页面可读、可维护、可局部更新。

步骤：

1. 保持控件专属 builder 和 typed Slot，不引入统一 `ui::layout()`。
2. 增加匿名节点、显式 key、display name 的一致规则和重复 key 诊断。
3. 为复杂页面提供 fragment/group/helper 组合方式，减少深层 `.child()` 噪音。
4. 增加条件节点、switcher 和 keyed repeater，但把 diff 生命周期封装在容器/列表控件内部。
5. 统一 `child(widget)` 与 `child(widget, slotArgs)` 的默认布局语义。
6. 将 builder 的构造期副作用限制在 live attach 前，失败时不得留下半挂载节点。
7. 对每种 layout 补充可读案例：Canvas、Box、Overlay、Split、Scroll、SizeBox、Grid/Table。

验收：

- DSL 能表达 editor shell、inspector、asset list、dock layout。
- 父节点可在编译期拒绝不属于自身的 Slot 参数。
- 动态列表只更新受影响 rows，key 保持 focus/selection/edit state。
- 不恢复 Description/Reconciler 作为静态页面主路径。

### Phase 5：Style / Theme / Resource 状态闭环

目标：让主题、局部覆盖、异步资源和布局影响具备稳定契约。

步骤：

1. 为 style 字段增加 layout/paint/resource metadata。
2. 让 theme key + style type 通过 catalog/schema 校验，减少字符串错误。
3. 明确 sparse patch、full freeze、clear field 的 API 和序列化语义。
4. 验证 theme switch、style key 变化、父主题变化、font atlas ready、brush texture ready 的 invalidation。
5. 清理仍绕过 theme 的裸颜色/字号字段；保留行为真正需要的几何/交互状态。
6. 统一 visual states：normal、hovered、pressed、focused、disabled、selected、error、drop-target。
7. 建立无主题 fallback、主题切换、资源缺失/延迟加载的 snapshot 与 GPU 回归。

验收：

- 主题切换不会遗漏 paint 或 layout 更新。
- 字体/纹理异步就绪后下一帧显示正确内容。
- style schema 错误可在编译期或文档加载期明确诊断。
- EditorTheme 与 WorkbenchTheme 不形成第二套机制。

### Phase 6：编辑器数据契约

目标：建立 GUI 与 Scene/ECS/Resource 的可撤销、可选择、可同步数据层。

步骤：

1. 建立 `SelectionModel`：单选、多选、primary、hover、active、focus。
2. 建立 command/action routing：菜单、快捷键、context menu、command palette 共用。
3. 建立 undo/redo transaction，支持拖动连续修改合并提交。
4. 建立 property projection：反射字段 → editor field model → retained editor control。
5. 支持 multi-object editing、mixed value、validation error、readonly、missing resource。
6. 将 Scene hierarchy、asset browser、inspector、viewport selection 接入同一 selection/command 模型。
7. 明确 editor model 与 UI Reactive 的单向/双向边界，禁止 widget 直接持有不安全的 ECS 裸指针。

验收：

- 修改 inspector 属性可 undo/redo。
- 多选对象可显示 mixed value 并批量提交。
- hierarchy、viewport、inspector 的 selection 一致。
- 资源加载失败能在 UI 中显示并可恢复。

### Phase 7：Editor primitives 与 retained 迁移

目标：用 retained GUI 覆盖编辑器核心工作流。

迁移顺序：

1. Inspector property editors：bool、enum、flags、number、vector、color、asset reference；nested/composite 字段必须走统一 property-path 展开，不把通用 typed read/write/copy/compare 逻辑长期留在 editor 的 `PropertyHandle`。
2. Content Browser / File Explorer：目录树、列表/图标视图、搜索、选择、双击打开、错误态。
3. Hierarchy / Tree：大树、过滤、展开状态、重排、拖放。
4. Virtualized list/table：可见行复用、稳定 key、局部 dirty。
5. Menu、context menu、command palette、shortcut/keymap。
6. Viewport host：纹理显示、输入 capture、camera navigation、selection overlay。
7. Gizmo/transform overlay：先定义 retained overlay contract，再接入 ImGuizmo 等实现或替代实现。
8. Drag/drop asset workflow：资源拖到 viewport、inspector、hierarchy 的统一 payload 和 preview。

验收：

- EditorSurface 能独立完成打开项目、选择实体、修改属性、保存、撤销、资源选择和 viewport 基本操作。
- 大型 hierarchy/content list 不因全量重建而失去可用性。
- 所有新控件有 headless interaction test 和至少一个 windowed smoke。

### Phase 8：移除 ImGui 双栈并完成多窗口能力

目标：让 retained WidgetTree 成为编辑器 chrome 的唯一正式路径。

步骤：

1. 按功能删除 ImGui TypeRenderer、Content Browser、FilePicker、ImGuizmo bridge、Runtime Tools、UI Designer、debug images。
2. 保留期间不新增旧路径功能，不做双写。
3. 完成 docking persistence、floating window、tab close/reorder、focus restoration、modal/popup scope。
4. 支持多 WidgetTree / 多 surface / 多 viewport 的生命周期和资源隔离。
5. 移除 `imgui-local` 及不再需要的 editor ImGui target/include。
6. 统一 editor startup、input、presentation、shutdown 路径。

验收：

- editor chrome 在 `widgettree` 模式下覆盖完整核心工作流。
- 进程内不再同时录制 ImGui 和 WidgetTree chrome。
- 多窗口/浮动面板关闭、重开、恢复布局后状态正确。
- Editor teardown 在 VMA/render/font 清理前释放所有 GUI 资源。

### Phase 9：性能、稳定性与发布门禁

目标：达到长期运行和跨平台发布质量。

步骤：

1. 建立大场景、大层级、大资源目录、大 inspector 的性能基线。
2. 增加长时间运行、频繁 attach/detach、热重载、资源异步完成、theme switch 压测。
3. 完成 macOS/Clang、Windows/MSVC、Vulkan/OpenGL 组合回归。
4. 完成 DPI、CJK fallback、键盘布局、输入法、剪贴板和文本编辑测试。
5. 增加 snapshot digest、GPU/offscreen parity、automation route trace 门禁。
6. 形成 editor release checklist，未通过项阻止宣称“retained editor ready”。

## 6. 优先级与依赖

```text
P0  Phase 1  生命周期/缓存正确性
P0  Phase 2  invalidate taxonomy + 增量布局基础
P1  Phase 3  ReactiveList / Computed / transaction
P1  Phase 5  Style/resource invalidation
P1  Phase 6  selection/command/undo/property projection
P1  Phase 7  Inspector + Content Browser + Hierarchy + Viewport
P2  Phase 4  DSL ergonomics / dynamic structure
P2  Phase 8  全量 ImGui 移除、多窗口收口
P2  Phase 9  性能、跨平台、发布门禁
```

依赖关系：

- Phase 1 是 Phase 2、7、8 的正确性前提。
- Phase 2 是 Phase 3、7 的性能前提。
- Phase 3 和 Phase 6 共同决定动态 editor 控件能否局部更新。
- Phase 5 是 editor visual states、font/resource error state 的前提。
- Phase 6 完成前，不迁移大规模 Inspector 和 multi-selection。
- Phase 7 核心工作流完成前，不删除 ImGui 路径。

## 7. 明确不做

- 不恢复任何 legacy API 或旧 serialized widget file 迁移。
- 不把所有接口塞进 `ui::layout()`。
- 不把 specialized TreeView/TableGrid/Dock 强行改造成 UICompoundWidget。
- 不把 React/VDOM/Reconciler 作为静态 native DSL 的基础。
- 不为了“看起来有进度”拆分无关提交。

