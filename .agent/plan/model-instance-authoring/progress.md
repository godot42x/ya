# Progress — model-instance-authoring

## 2026-09-17（首轮：Phase 1–3 全部落地）

### 完成内容

**Phase 1 — 实例根解析与拾取**

- `EditorHierarchyOps` 新增 `editorIsInstanceChild` / `editorResolveInstanceRoot`（沿 node 父链上溯到第一个非托管祖先，带 64 层环保护；非托管实体与孤立托管子树各自有明确回退）。
- `EditorLayer::pickEntity` 在 entity-id pass 与 CPU raycast 合流后统一解析；`EKeyMod::Alt` 按下时跳过解析。命中叶子与选中根不同时日志同时打印两者。

**Phase 2 — 生命周期卫生**

- `Scene::destroyEntity` 级联销毁整棵子树：先按前序收集后代，再**逆序**销毁，保证不会通过已释放的父 `Entity` 取子（`Entity` 按值存在 `_entityMap`）。
- `ModelInstantiationSystem::cleanupChildEntities` 改为扫描实例根 node 下「最顶层的 `ManagedChildComponent`」并逐个 `destroyEntity`；删除 `ModelComponent::_childNodes` 字段与全部读写点。

**Phase 3 — 结构操作封锁与提示**

- `SceneHierarchyPanel::deleteSelection` 跳过托管子节点并 warn 指向实例根。
- `EditorLayer::cmdDuplicateSelection` 同样跳过并 warn。
- `moveEditorHierarchyEntity` 拒绝拖动托管子节点，以及 `dropMode == 1` 目标是托管子节点的情况。
- `EditorActionCatalog` 的 `selection.delete` / `selection.duplicate` `canExecute` 增加 `!editorSelectionIsAllInstanceChildren(layer)`，菜单项置灰；命令实现仍是唯一判据。
- `EditorInspectorTab` 新增 instance 提示列（`_instanceHost` / `_instanceBodyText`）：选中托管子节点说明「改动仅本次会话有效 + 结构操作已禁用」，选中实例根给出「N meshes / M materials」摘要。配套新增 `editorCountInstanceChildren`。

### 验证

本机 `ya-render-3d` 当时正被另一条并行改动线（RenderSubmission / CameraGizmo 重构）占用且处于不可编译状态，`ya-game-editor` / `ya-testing` 因此无法链接。为避免空等，验证走「真实引擎符号 + 私有 harness」：

```text
# 1) 改动的 8 个 TU 逐文件用目标真实 flags 语法校验
clang++ <target flags> -fsyntax-only -I/tmp/ya-stub <file>
  EditorHierarchyOps.cpp            -> 0
  EditorInspectorTab.cpp            -> 0
  SceneHierarchyPanel.cpp           -> 0
  EditorActionCatalog.cpp           -> 0
  EditorLayer.Interaction.cpp       -> 0
  EditorLayer.ViewportAuthoring.cpp -> 0
  ModelInstantiationSystem.cpp      -> 0
  Scene.cpp                         -> 0

# 2) Scene.cpp 真实链接进 libya-scene-core（xmake b ya-scene-core -> build ok）
# 3) 新增用例在私有 harness 里链接并执行（只额外提供缺失的 include，不改仓库）
SceneNodeLifecycleTest            3/3 PASSED
EditorHierarchyOpsTest            8/8 PASSED   （新增 6 例 + 原有 2 例）
```

**用例敏感性已反证**：把 `Scene::destroyEntity` 临时改回「只 clearChildren」的旧实现并重链 `ya-scene-core` 后，两个级联用例如预期失败（`entityCount` 只减 1、孤儿实体仍在 registry / `_nodeMap` 里），恢复后 3/3 重新通过。证明用例确实锁住了本次修复，而不是恒真。

**仍待补跑**（阻塞在并行改动线，与本轮改动无关）：

```bash
xmake b ya-game-editor ya-testing
./build/macosx/arm64/debug/ya-testing --gtest_filter='SceneNodeLifecycleTest.*:EditorHierarchyOpsTest.*'
# 编辑器手测：点 Suzanne 整体移动 / Alt 点进 mesh / 删实例根 / 删子 mesh 被拒
```

### 已知限制 / 未完成

1. **子节点编辑不持久**：改子 mesh 材质后保存再加载会被重建覆盖。v1 明确不承诺，已在 Inspector 文案中说明；重建入口已收敛为「`_bResolved` + cleanup 扫描」单一 seam，后续加 override 不需要改结构。
2. **共享材质**：同 material index 的 mesh 共享 runtime material，改一个会影响同级。这是 Unity `sharedMaterial` 等价物，本轮只说明不修改。
3. **Hierarchy 无实例标记**：`UITreeView::FNode::icon` 需要真实图片资源，仓库没有编辑器图标集；改用 Inspector 文案承担说明职责。
4. **嵌套实例未覆盖**：当前 `ModelInstantiationSystem` 只产生一层托管子节点；`editorResolveInstanceRoot` 已按多层实现，但若将来出现嵌套实例，规则应收紧为「上溯到最近的 `ModelComponent` 祖先」。

### 顺带修掉的真实缺陷（本轮范围外但同源）

- 删父实体留下的孤儿 mesh 会继续渲染：由 Phase 2 的级联销毁修复。
- 在 Hierarchy 删掉子 mesh 后再让 `ModelComponent` 失效会在 cleanup 里 use-after-free：由删除 `_childNodes` 清册修复。

### 后续修复：选中任意非模型实体崩溃（本计划引入的回归）

`updateInstanceNotice`（本计划 Phase 2 新增）在 Phase 2 里用的是
`primary->getComponent<ModelComponent>()`，而 `Entity::getComponent` 在组件缺失时
**assert**。于是选中任何不带 `ModelComponent` 的实体（相机、灯光、空节点）都会让编辑器
abort——用户点 Hierarchy 里的相机即可复现。

修复分两层：

1. `Entity::tryGetComponent<T>()`（ecs-core）：安全访问器成为一次调用即可用的惯用法，
   替代「先 `hasComponent` 再 `getComponent`」这种容易漏写的两步式。
2. 判断逻辑从 Inspector tab 提取为 `editorInstanceNotice(Scene&, Entity*)`，与其它 instance
   助手同处 `EditorHierarchyOps`，并由 `EditorHierarchyOpsTest` 覆盖（非模型实体返回空串、
   模型根与 mesh 子节点各自的文案）。

教训已写入 `.agent/skills/scene-object-boundary/SKILL.md` 的反面清单。
