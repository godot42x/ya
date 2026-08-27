# GUI Framework / Editor / Game UI 重构 Todo

## 当前优先级（按顺序推进）

- [x] G3.1 引入 `UIBehavior` 最小模型：先定义 attach / detach / tick / input hook / invalidate bridge，作为 drag/drop、tooltip、shortcut、editor interaction 的统一横切能力入口。
- [x] G3.2 用 `UIBehavior` 重构 Workbench drag/drop demo：移除 `FDemoDragItem` / `FDemoDropZone` 作为长期模型，改为普通 retained widget / 静态 DSL + behavior。
- [x] G3.3 收敛 drag/drop runtime 边界：`WidgetTree` 继续拥有 gesture / session / routing；widget 只暴露 capability hook；behavior 承载可复用交互逻辑。
- [x] G4.1 将 `Runtime/Widgets/Reactive.h` 拆到中性的 `Runtime/Binding/` 或 `Runtime/Dataflow/`，明确 Reactive 是绑定层能力，不是 widget kernel 本体。
- [x] G4.2 抽薄 invalidation / binding 稳定契约：底层必须并存 imperative setter、widget transient state、behavior state、reactive binding、future adapter patch。
- [x] G4.3 补齐 `UICompoundWidget` 迁移桥接契约：明确 kernel / behavior / compound / adapter 四者边界，并为现有 editor/game compound controls 建立迁移判定表。
- [x] G4.3a 基于现状盘点第一批对象：`FDemoDragItem` / `FDemoDropZone` / `UISelectableRow` / `UITreeView` / `UITableGrid` / `UIMenuBar` / `UIDockSpace`，逐个判定 builder helper / specialized control / `UICompoundWidget` / `UIBehavior` 归宿。
- [x] G4.3b 形成第一批逐文件实施顺序：先 `WorkbenchDemoPages.cpp`（drag/drop demo 行为化），再 `SelectableRow`，Tree/Table/Menu/Dock 仅补边界与 seam，不做 compound 化改写。
- [x] G5.1 拆分 `Declarative/Construct.h`，至少收敛为 `BuilderBase` / `ControlBuilders` / `LayoutBuilders` / `CompoundBuilder` / `Build` 一类边界，停止继续扩张 god file。
- [x] G5.2 明确 native retained DSL 与 future adapter 的关系：native DSL 只是原生 authoring API，不是 React-like / HTML-CSS-JS / script UI 的唯一底座。
- [ ] G6.1 在 behavior / binding / declarative 分层稳定后，再继续推进 GameEditor 剩余 ImGui feature migration，避免 editor 功能反向锁死底层。
- [x] G6.2 预留 adapter seam：定义最小 document/component adapter host 边界，保证未来接 React-like、HTML-CSS-JS、脚本 UI 时，只新增 adapter，不推翻 `UIElement` / `WidgetTree` / invalidate / layout / input kernel。

## 当前明确不做

- [ ] 不继续给 `UIButton`、`UIText` 等基础控件直接混入 drag highlight、drop target、tooltip、editor inspector 等专用状态。
- [ ] 不把 `UICompoundWidget` 视为唯一 component / declarative model；它只是 native retained composition primitive。
- [ ] 不把 document/component adapter、behavior registry 或 editor host 反向塞进 `UICompoundWidget`，把它膨胀成“框架之框架”。
- [ ] 不在 behavior / binding / `Construct` 分层稳定前，大面积铺开 Editor feature migration。
- [ ] 不恢复 `UIDescription -> reconciler -> controller` 这条已删除的声明式中间层。

## 每轮执行检查

- [ ] 每个切片先补或更新 widgets / declarative contract 测试，再做实现。
- [ ] 有实际代码改动时，plan 工件与对应代码一并分类提交。
- [ ] 避免误提交工作区内与当前切片无关的脏改动。
