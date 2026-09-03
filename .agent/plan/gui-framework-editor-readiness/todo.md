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

- [ ] 冻结 dirty taxonomy。
- [ ] dirty subtree。
- [ ] measure/arrange cache。
- [ ] child desired-size propagation。
- [ ] layout performance counters。

## Phase 3：Reactive

- [ ] UI-thread 与 reentrancy contract。
- [ ] transaction/batch。
- [ ] keyed ReactiveList diff。
- [ ] Computed dependency graph。
- [ ] detach/unbind safety。

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
