# ModelComponent 作为 prefab 实例：拾取、编辑边界与生命周期

> 建立日期：2026-09-17
> 状态：已实现（本轮）
> 目标：把 `ModelComponent` 根 + `ManagedChildComponent` 子树的「prefab 实例子场景」语义在编辑器里做实，不引入资产系统。

## 0. 结论摘要

实例子场景的**数据形状已经存在**：根实体只序列化 `_modelRef`，子 mesh 由 `ModelInstantiationSystem` 运行时重建，`_cachedMaterials` 按 material index 共享 runtime material（等价 Unity `sharedMaterial`）。

缺的只有三件事，本轮补齐：

1. 视口拾取默认落到**实例根**，而不是落到某个生成出来的 mesh。
2. 实例子树上的**结构性操作**（删除 / 复制 / 重排 / 拖入）会破坏记账或静默丢失，必须挡住。
3. 删父实体时子实体变成**孤儿**（不在任何 node 子树里却仍在 registry 中继续渲染）——这是真实缺陷，必须修。

**明确不改 `TransformSystem`**：world matrix 从根下发、`setWorldTransform` 做 `parentWorld⁻¹` 再分解，两者本来就正确。之前「抓到子 mesh 后只有部分移动」的根因是选区落在子 mesh 上，不是变换系统。

## 1. 边界

### 做

- 视口点击 → 解析到第一个非托管祖先（实例根）；`Alt`+点击保留像素命中的叶子 mesh。
- `Scene::destroyEntity` 级联销毁整棵子树，消灭孤儿实体。
- `ModelInstantiationSystem::cleanupChildEntities` 改为扫描 scene 结构，删除 `ModelComponent::_childNodes` 原始指针清册。
- Hierarchy 删除 / 复制 / 拖拽、`ActionCatalog` 的 `canExecute`、Inspector 提示文案。

### 不做

- 不引入新的资产类型 / 资产系统 / prefab 变体或 override 存档。
- 不做子节点序列化、diff / revert / apply。
- 不做实例独占材质（共享材质语义保留）。
- 不改 `TransformSystem`、不改 entity-id pass 与 CPU raycast 的命中语义。
- 不做 Hierarchy 行图标（`UITreeView::FNode::icon` 需要真实图片资源，仓库没有编辑器图标集）。

## 2. 关键决策

| 决策 | 选择 | 理由 |
|---|---|---|
| 拾取落点 | 根 + `Alt` 进入子 mesh | 默认行为不再误选生成的 mesh；同时保留视口内直接定位具体 mesh 的能力 |
| 子节点编辑持久化 | 仅本次会话 | v1 不引入 override 存储；重建入口收敛成单一 seam，之后加 override 不用改结构 |
| 结构操作 | 阻止并说明原因 | 删除 / 复制 / 重排当前是坏的（悬垂指针、孤儿 mesh、重命名会被重建覆盖） |
| 实例边界来源 | 结构（`ManagedChildComponent` 子树） | 原始指针清册在作者删掉子 mesh 后必然失效，是 use-after-free 的根因 |
| 共享材质 | 保持不变 | 等价 Unity `sharedMaterial`；只在 Inspector 文案里说明，不静默改成独占 |

## 3. 阶段

### Phase 1 — 实例根解析与拾取策略

- `editorIsInstanceChild` / `editorResolveInstanceRoot`（`EditorHierarchyOps`，headless 可测）。
- `EditorLayer::pickEntity` 在两条拾取通路合流后统一解析实例根；`Alt` 跳过解析。

验收：`Suzanne` 整体可拖拽；`Alt`+点击选中具体 mesh。

### Phase 2 — 生命周期卫生

- `Scene::destroyEntity` 逆前序级联销毁（后代先于祖先，避免通过已释放的父 Entity 取子）。
- `cleanupChildEntities` 扫描实例根 node 下最顶层的托管子节点并销毁，不再依赖 `_childNodes`。

验收：删实例根后整棵实例消失且不再渲染；删单个托管子 mesh 后再重建不崩。

### Phase 3 — 结构操作封锁与提示

- `SceneHierarchyPanel::deleteSelection` / `EditorLayer::cmdDuplicateSelection` / `moveEditorHierarchyEntity` 以 `editorIsInstanceChild` 为判据拒绝。
- `moveEditorHierarchyEntity` 额外拒绝 `dropMode == 1` 且目标是托管子节点。
- `ActionCatalog` 的 `selection.delete` / `selection.duplicate` `canExecute` 在选区全为托管子节点时返回 false。
- Inspector 增加 instance 提示列（选中托管子节点 / 实例根两种文案）。

验收：对托管子节点按 Delete / Cmd+D 菜单为灰，Inspector 能看到原因。

## 4. 验收命令

```bash
xmake b ya-game-editor ya-render-ecs-adapters ya-scene-core ya-testing
./build/macosx/arm64/debug/ya-testing --gtest_filter='SceneNodeLifecycleTest.*:EditorHierarchyOpsTest.*'
./build/macosx/arm64/debug/ya-runtime --ya-project=Example/HelloMaterial/HelloMaterial.yaproject --editor --width=1470 --height=836 --screenshot=/tmp/inst.png --screenshot-target=presentation --screenshot-frame=90 --exit-after-frame=120
```
