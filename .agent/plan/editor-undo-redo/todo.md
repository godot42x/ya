# Editor Do/Undo/Redo/Cancel TODO

## 使用规则

- worker 开始前先读 `plan.md`，然后领取第一个依赖已满足的任务。
- `[-]` 表示正在执行；同一时间只允许一个架构任务处于 `[-]`。
- 每个任务默认对应一个可 review 的提交批次；标明"调查"的任务可以只更新计划。
- 实现提交必须执行 `xmake b ya-testing`、完整 `ya-testing` 运行和任务指定冒烟。
- 不修改或提交与任务无关的现有工作区文件。

状态：`[ ]` 未开始，`[-]` 进行中，`[x]` 完成，`[~]` 延后，`[-x]` 停止。

## P0 前置基础设施

- [ ] `ED-001` 场景 dirty 追踪
  - 依赖：无
  - 修改：`EditorLayer`/`EditorModule` 增加 `bSceneDirty` 与标记入口；所有编辑入口（属性、gizmo、层级、组件）统一调用
  - 验收：任何编辑后 dirty=true；保存/加载后 dirty=false；标题栏或窗口标题体现未保存状态
  - 提交：`[editor] track scene dirty state`

- [ ] `ED-002` 实体/节点快照与恢复工具
  - 依赖：无
  - 修改：新增 `Editor/Undo/SceneSnapshot.h/.cpp`：
    - `snapshotEntity(entity)` -> `{uuid, entityJson, parentUuid, childIndex, nodeName}`
    - `restoreEntity(scene, snapshot)`：按 UUID 重建实体+组件（复用 `SceneSerializer` 路径）+ 重建节点挂回 parent/childIndex + 修复 `_owner`
    - `removeEntityWithSnapshot(entity)`：先快照再 `destroyEntity`
  - 验收：对场景内任意实体（含子层级）执行 snapshot->remove->restore 后，UUID、组件字段、节点层级一致；`_owner` 有效；entt handle 变化不影响结果
  - 提交：`[editor/undo] add entity snapshot and restore helpers`

- [ ] `ED-003` RenderContext 属性 old/new 捕获
  - 依赖：无
  - 修改：`renderReflectedType` 外层对实例做渲染前/后序列化 diff，填充 `RenderModificationRecord.oldValueJson/newValueJson`；保持 `isModified` 等现有查询语义不变
  - 验收：单属性编辑、多选编辑、嵌套容器属性均能拿到准确 old/new JSON；现有 Inspector 行为无回归
  - 提交：`[editor/inspector] capture property old/new values for undo`

## P1 事务框架

- [ ] `ED-101` EditorCommand 抽象与 EditorUndoService
  - 依赖：无
  - 修改：新增 `Editor/Undo/EditorCommand.h`、`EditorUndoService.h/.cpp`：
    - do/undo/redo/postApply 生命周期；undo/redo 栈（上限 100）；`clear()`
    - `beginTransaction/commitTransaction/cancelTransaction`；同标签 + 合并窗口合并
    - 挂在 `EditorLayer`，提供 `canUndo/canRedo/currentLabel`
  - 验收：单测覆盖 push/undo/redo/合并/cancel/清栈（可用纯命令桩，不依赖渲染）
  - 提交：`[editor/undo] add command transaction framework`

- [ ] `ED-102` 菜单、快捷键与 Esc-cancel 语义
  - 依赖：ED-101
  - 修改：`EditorLayer.Layout.cpp` Edit 菜单（Undo/Redo/Cancel Operation + 快捷键）；
    `EditorInputNode.cpp` Esc 路由改为"有活动事务先 cancel，否则 quit"
  - 验收：菜单项与快捷键可用；gizmo 拖动中 Esc 取消拖动并还原；空闲 Esc 仍退出
  - 提交：`[editor/undo] wire edit menu, shortcuts and cancel semantics`

## P2 属性编辑接入

- [ ] `ED-201` PropertyEditCommand 与 Inspector 接入
  - 依赖：ED-002、ED-003、ED-101
  - 修改：由 `RenderContext` 修改记录生成 `PropertyEditCommand`（路径 + old/new），
    Inspector 单/多选编辑包成事务；`undo` 用 old 反写并走 `deserializeProperty`
  - 验收：Inspector 每项编辑可 undo/redo；多选编辑整体为一次 undo；拖拽连续修改合并为一条
  - 提交：`[editor/undo] undo inspector property edits`

- [ ] `ED-202` 编辑副作用接线（onEdit + 资源失效重放）
  - 依赖：ED-201
  - 修改：属性编辑后调用组件 `onEdit()`；`postApply` 统一重放
    `terrain->invalidate()`/`markTerrainDirty`/`markSkyboxDirty`/`markEnvironmentDirty`
  - 验收：undo/redo 修改 terrain/skybox 参数后资源重新 resolve；与手工编辑行为一致
  - 提交：`[editor/undo] replay edit side effects on undo/redo`

## P3 结构操作接入

- [ ] `ED-301` 实体增删/复制/移动 undo
  - 依赖：ED-002、ED-101
  - 修改：`SceneHierarchyPanel` 的 create/duplicate/delete/move 入口包成事务；
    删除用 `removeEntityWithSnapshot`；恢复走 `restoreEntity`
  - 验收：新增/复制/删除/拖拽重排均可 undo/redo；子层级整体恢复；选中集按 UUID 恢复或清空
  - 提交：`[editor/undo] undo entity create/duplicate/delete/move`

- [ ] `ED-302` 组件增删 undo
  - 依赖：ED-002、ED-101
  - 修改：`DetailsView` 的 add/remove component 包成事务，恢复/移除走组件快照 +
    `onPostSerialize`；billboard/light 联动约束在 undo/redo 时同样执行 `canAdd/canRemove` 判定
  - 验收：添加/移除组件可 undo/redo；受联动约束的组件恢复后链路（billboard<->light）一致
  - 提交：`[editor/undo] undo component add/remove`

- [ ] `ED-303` Gizmo 拖动事务
  - 依赖：ED-101
  - 修改：`renderGizmo` 用 `ImGuizmo::IsUsing()` 边沿 begin/commit 事务；
    `GizmoTransformCommand` 保存主实体与所有选中实体的 begin/end world 变换
  - 验收：一次拖动 = 一条 undo；undo 精确还原全部选中实体；Esc 可取消拖动
  - 提交：`[editor/undo] transactional gizmo drag`

## P4 收尾

- [ ] `ED-401` PIE / 场景切换清栈与隔离
  - 依赖：ED-101
  - 修改：进入 PIE、切换/加载场景、关闭场景时 `undoService->clear()`；
    authoring 与 play 场景快照互不串用
  - 验收：PIE 进出后 undo 栈为空；切换场景不残留旧场景命令
  - 提交：`[editor/undo] clear history across PIE and scene switches`

- [ ] `ED-402` 集成测试与文档
  - 依赖：ED-201~ED-303
  - 修改：新增 `EditorUndoTest.cpp`（命令桩 + 属性/实体/组件快照恢复单测）；
    `progress.md` 收尾记录
  - 验收：`xmake b ya-testing` + 全量测试通过；覆盖属性、实体、组件、gizmo 四类命令
  - 提交：`[test/undo] cover command framework and snapshot restore`
