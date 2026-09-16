# Session Checklist — model-instance-authoring

## 开工

1. `git status` 确认哪些改动是本线的（其它 agent 常在 Render3D / GUI 并行）。
2. 确认 `ModelInstantiationSystem` / `ManagedChildComponent` / `EditorHierarchyOps` 仍是当前接缝（`rg -n 'ManagedChildComponent' Engine/Source`）。
3. 复述本轮目标与边界，说明保留 / 未完成 / 偏离项。

## 硬规则

1. 实例边界来自**结构**（实例根 node 下的 `ManagedChildComponent` 子树），不来自组件上的原始指针清册。
2. 销毁路径必须级联；任何「删父留子」的新代码都要先想清楚孤儿会怎样继续渲染。
3. 结构操作的拒绝判据写在命令实现里；`canExecute` 只负责菜单置灰，不做唯一判据。
4. 实例子节点的编辑语义是「本次会话有效」，文案必须讲清楚，不能静默丢失。
5. 测试里 `Entity` 按值存在 `Scene::_entityMap`，`Node` 由 `_nodeMap` 持有：销毁后**不得**再解引用任何裸 `Entity*` / `Node*`，改用 `entt::entity` handle 回查。

## 收尾

1. `xmake b ya-game-editor ya-render-ecs-adapters ya-scene-core ya-testing`。
2. `ya-testing --gtest_filter='SceneNodeLifecycleTest.*:EditorHierarchyOpsTest.*'`。
3. 编辑器手测：点实例整体移动、Alt 点进入 mesh、删实例根、删子 mesh 被拒。
4. 更新 `progress.md`，把命令与实测结果写清楚。

## 轮次记录

### 2026-09-17（首轮）

- Phase 1/2/3 全部落地：实例根解析 + 拾取策略、销毁级联、cleanup 改结构扫描并删除 `ModelComponent::_childNodes`、结构操作封锁、`canExecute` 置灰、Inspector 提示。
- 新增 `SceneNodeLifecycleTest`（3 例）与 `EditorHierarchyOpsTest` 新增 6 例。
- 未做 / 偏离：见 `plan.md` §1「不做」与 `feature_matrix.json` 的 `out-of-scope` 项。
