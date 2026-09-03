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

- [ ] fragment/group/helper。
- [ ] conditional/switcher/repeater。
- [ ] duplicate key diagnostics。
- [ ] all layout examples and compile-time rejection tests。

## Phase 5：Style / Theme

- [ ] style field impact metadata。
- [ ] style key/type catalog。
- [ ] resource-ready invalidation。
- [ ] visual state matrix。

## Phase 6：Editor data

- [ ] SelectionModel。
- [ ] Command/Action routing。
- [ ] Undo/Redo transaction。
- [ ] Property projection。
- [ ] multi-selection / validation / error state。

## Phase 7-9：Editor migration and release

- [ ] Inspector retained controls。
- [ ] Content Browser retained controls。
- [ ] Hierarchy and virtualized lists。
- [ ] Viewport/gizmo/drag-drop。
- [ ] Remove ImGui editor paths。
- [ ] Multi-window/docking persistence。
- [ ] Cross-platform and long-run gates。
