# Todo

## Phase 0

- [x] 更新过时的 `gui-framework` skill 描述。
- [x] 建立 GUI 能力覆盖矩阵。
- [x] 为每个高风险缺口列出当前测试和缺失测试。
- [ ] 确认 Phase 1 的 cache identity 方案。

## Phase 1：生命周期与缓存

- [x] 稳定 widget runtime identity / cache generation。
- [x] detach/destroy 清理 paint cache。
- [x] 完整 draw-item equality/debug hash。
- [x] stale cache、reparent、cross-tree 测试。
- [x] 对象重分配、popup/drag teardown 和 snapshot build mutation policy。

## Phase 2：Invalidate / Layout

- [x] 冻结 dirty taxonomy。
- [x] child desired-size propagation（UIElement measure dirty 向祖先传播）。
- [x] dirty subtree（assigned rect proof 下的局部 layout skip）。
- [ ] measure/arrange cache。
- [x] layout proof generation / attach-reparent invalidation boundary。
- [x] layout performance counters（tree-level scope 累计统计 + skipped widgets）。

## Phase 3：Reactive

- [x] UI-thread 与 reentrancy contract（foreign-thread reject + reentrant notify defer）。
- [x] transaction/batch（同步嵌套事务、pending 去重、依赖快照）。
- [x] keyed ReactiveList identity/diff baseline（revision + replaceKeyed + TreeView state prune）。
- [x] keyed ReactiveList mutation/update contract（insert/update/move、边界拒绝、clear/replace diff）。
- [x] Computed dependency graph（lazy cache + upstream dirty propagation + cycle diagnostics）。
- [x] detach/unbind safety（upstream destroy -> downstream computed unlink）。

## Phase 4：DSL

- [x] fragment/group/helper。
- [x] conditional construction helpers (when / unless / ifElse); runtime switcher/repeater remains pending。
- [x] duplicate key diagnostics。
- [x] compile-time rejection tests for canvas/box slots; broader layout examples remain pending。
- [x] keyed child reconciler + Content Browser / Scene Save list consumers。
- [x] Content Browser entry visible-range window + spacer scroll extent; TableGrid/TreeView virtualization remains pending。

## Phase 5：Style / Theme

- [x] style field impact metadata。
- [x] style key/type catalog。
- [x] resource-ready invalidation。
- [x] visual state matrix。
- [x] panel naked-color cleanup（paint 只读 fillColor；`_color` 仅 no-theme fallback）。
- [x] 无主题 / 资源缺失 snapshot 回归（headless host 同契约；windowed GPU/offscreen 仍属 Phase 9）。

## Phase 6：Editor data

- [x] SelectionModel（identity 单选/多选/primary/hover/active/focus；不持有 ECS 指针）。
- [x] Command/Action routing（ActionMap；菜单/快捷键/toolbar 共用 execute）。
- [x] Undo/Redo transaction（UndoStack；拖动 merge；Inspector 属性/重命名接入 ActionMap）。
- [x] Property projection（`PropertyGraph::project`；Transform setter 进 projection；Inspector 按 component 物化 AutoPropertySection）。
- [x] multi-selection mixed value（交集 component、DragFloat "—"、批量写回、按 instance undo）。
- [x] viewport 选择写入 SelectionModel（`EditorLayer::selectionGeneration` + `syncSelectionFromLayer`；`SelectionModel::replace`）。
- [x] validation / missing resource error state（`PropertyHandle::validationError` + manipulate spec；`UIDragFloat`/`UITextField`/`UIImage` error fill；viewport `setResourceMissing`）。

## Phase 7-9：Editor migration and release

- [x] Inspector retained controls（bool/float/vec3/string/enum/color/asset-ref via `EditorAutoPropertySection` + `PropertyHandle`）。
- [x] Content Browser retained controls（EditorSurface mount/entry lists、search、selection、navigate、visible-window；ImGui panel 仍平行）。
- [x] Hierarchy virtualization（UITreeView scroll-window paint + EditorSurface HierarchyScroll）。
- [x] Hierarchy filter + entity drag-drop reorder（`HierarchyFilter` + `bindFilter`；`setReorderable` + `EditorHierarchyOps`）。
- [ ] Viewport/gizmo/drag-drop。
- [ ] Remove ImGui editor paths。
- [ ] Multi-window/docking persistence。
- [ ] Cross-platform and long-run gates。
