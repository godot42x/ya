# Editor Do/Undo/Redo/Cancel 计划

> 状态：2026-08-07 建立。基于对编辑器变更入口与现有基础设施的调研，本计划定义
> 事务/命令框架、快照策略、副作用重放与 UI 接入，作为后续实现主线。

## 1. 目标

为编辑器引入统一的 **do / undo / redo / cancel** 能力：

```text
用户操作（属性编辑 / gizmo 拖动 / 实体与组件增删 / 节点重排）
  -> EditorCommand（do/undo/redo，或 beginTransaction/commit/cancel）
  -> EditorUndoService（undo/redo 栈 + 合并 + 事务边界）
  -> 快照（属性级 old/new diff 或 实体级 JSON 快照）
  -> 副作用重放（TransformSystem / onPostSerialize / onEdit / 资源 invalidate / 面板刷新）
```

最终要求：

1. 支持 Ctrl/Cmd+Z（undo）、Shift+Cmd+Z（redo）、Edit 菜单入口。
2. 一次 gizmo 拖动、一次属性拖拽、一次"添加/删除/复制实体"分别是一个原子事务。
3. **cancel 语义**：事务进行中按 Esc 放弃当前操作并还原到事务开始状态；空闲时 Esc 保持退出。
4. undo/redo 后渲染、层级、Inspector、选中集、资源（terrain/skybox/env）、物理 body 全部一致。
5. 只在 stopped 模式生效（与现有编辑器交互门禁一致）；PIE/场景切换清空栈。
6. 快照一律以 UUID 为键，不依赖 entt handle 稳定性。

## 2. 非目标与停止线

本计划不要求：

- v1 不把脚本/RPC（`component.set`、JS 反射字段写入）纳入 undo 栈；脚本操作不产生事务。
- 不做跨 PIE 边界的 undo（play 场景为 clone，undo 只作用于 authoring 场景）。
- 不做 UE 级"多级场景编辑会话"、非线性历史、分支/书签。
- 不为"消灭手写 UI"而重写 Inspector；只给现有渲染器补 old/new 捕获。
- 不迁移实体存储结构（`_entityMap` 按值存 Entity 的问题只做兼容处理，见风险 D）。

## 3. 当前代码事实

### 3.1 变更入口（需要纳入事务的操作）

| 操作 | 位置 | 现状 |
|---|---|---|
| Inspector 属性编辑 | `Editor/Inspector/TypeRenderer.cpp`、`DetailsView.cpp` | 渲染器原地直写组件字段；`RenderContext::addModification/pushModified` 已记录 propPath |
| Gizmo 拖动 | `Editor/Interaction/EditorLayer.Interaction.cpp` `renderGizmo` | 每帧 `TransformSystem::setWorldTransform`，无 begin/end 边界；多选按 delta 联动 |
| 实体增删/复制 | `Editor/Panels/SceneHierarchyPanel.cpp` | `flushPendingActions` 延迟批处理 duplicate/delete |
| 节点重排 | 同上 dragAndDrop | `moveNode(parent, childIndex)` |
| 组件增删 | `Editor/Inspector/Reflection/DetailsView.Reflection.cpp` | `ECSRegistry::add/removeComponent`，带 billboard/light 联动约束 |
| 资源副作用 | `DetailsView.Components.Basic.cpp` | `terrain->invalidate()`、`markTerrainDirty/markSkyboxDirty` 等 |

### 3.2 可直接复用的基础设施

- `SceneSerializer::serializeEntity/deserializeEntity`：UUID 身份 + 反射逐组件序列化 +
  custom serializer + `onPostSerialize`；`serializeNodeTree`（entityRef 用 UUID 引用）。
- `IDComponent::_id` + `Scene::createEntityWithUUID`；编辑器选中已用 `_selectedEntityUUID`。
- `RenderModificationRecord` 已预留 `oldValueJson/newValueJson` 字段（当前无人填值）。
- `ECSRegistry::cloneComponent` + `EClonePolicy`（CopyCtor/Reflection）。
- `PhysicsSystem::reconcileBodies`：按组件存在性 diff 重建 body，undo 恢复后自动同步。
- `ResourceResolveSystem` 的 `markTerrainDirty/markSkyboxDirty` 等统一失效入口。
- `EditorLayer.Layout.cpp` 已有 `BeginMenuBar`；`EditorInputNode.cpp` 有命令路由。

### 3.3 已知缺口

- 无命令/事务抽象，无 undo/redo 栈。
- 属性编辑无 old 值捕获；`onEdit()` 钩子存在但从未被调用。
- 无场景 dirty 追踪（没有"未保存修改"标记）。
- Esc 目前直接 `requestQuit()`，与 cancel 语义冲突。
- gizmo 拖动无开始/结束事件。

## 4. 设计

### 4.1 命令与事务模型

```cpp
struct EditorCommand {
    virtual void execute() = 0;   // 首次执行（do）
    virtual void undo() = 0;      // 撤销
    virtual void redo() = 0;      // 重做（默认 = execute）
    virtual void postApply() {}   // 副作用重放（见 4.3）
    std::string label;
};

class EditorUndoService {
    void beginTransaction(std::string label);   // 收集组内命令
    void commitTransaction();                    // 压入 undo 栈（可合并）
    void cancelTransaction();                    // 逆序 undo 组内命令并丢弃
    void push(std::unique_ptr<EditorCommand>);
    void undo();  void redo();  void clear();
    void setCoalesceWindow(...);                 // 属性拖拽/同标签连续命令合并
};
```

实现位置：`Editor/Undo/`（`EditorCommand.h`、`EditorUndoService.h/.cpp`、命令子类）。
服务实例挂在 `EditorLayer`，输入路由与菜单直接调用。

### 4.2 快照策略

- **属性级**（Inspector 编辑）：在 `renderReflectedType` 外层对已修改属性做
  before/after 序列化，填 `RenderModificationRecord.oldValueJson/newValueJson`；
  每个修改路径生成 `PropertyEditCommand`。多选编辑（`applyModificationsToInstances`）
  同一事务内为每个实例各生成一条。
- **实体级**（增删/复制）：`serializeEntity` 快照 + 节点父子关系
  （parent UUID / childIndex / nodeName）快照；undo 删除 = 按 UUID 重建实体与节点；
  undo 新增 = `destroyEntity`；undo 复制 = 删除副本；undo 移动 = 恢复 parent/childIndex。
- **组件级**（增删）：组件 JSON 快照 + `onPostSerialize` 重放；恢复走
  `ECSRegistry` 现有 add/remove。
- **Gizmo**：`ImGuizmo::IsUsing()` 上升沿 begin 事务、下降沿 commit；事务内记录
  主实体与全部选中实体的 world 变换（begin 快照），commit 时生成
  `GizmoTransformCommand`（undo 用 begin 值，redo 用 end 值）。

### 4.3 副作用重放（postApply，undo 与 redo 后统一执行）

1. `TransformSystem` 重算/标记 dirty（保证 gizmo 与渲染立即一致）。
2. 组件 `onPostSerialize()`；属性编辑流接入 `onEdit()`。
3. 资源失效重放：`terrain->invalidate()` + `markTerrainDirty`、`markSkyboxDirty`、
   `markEnvironmentDirty`（与 Inspector 现有路径一致）。
4. 场景 dirty 标记（补前置后）。
5. 面板与选中集刷新：hierarchy / Inspector / gizmo；选中按 `_selectedEntityUUID`
   重新解析，失效则清空。
6. 物理不需要额外操作（`reconcileBodies` 自愈），但 undo 后若处于 stopped 模式
   且需要即时调试体同步，可在 postApply 触发一次 reconcile。

### 4.4 UI / 输入

- `EditorLayer.Layout.cpp` 菜单栏增加 Edit 菜单：Undo / Redo / Cancel Operation。
- 快捷键：Cmd/Ctrl+Z、Shift+Cmd+Z；gizmo 拖动中 Esc = cancel 当前事务。
- `EditorInputNode.cpp` 的 Esc 路由改为：有活动事务时先 cancel，否则维持 quit。

## 5. 关键决策与风险

- **A. 快照键**：一律 UUID（`IDComponent._id`）；entt handle 会复用，禁止作为快照键。
- **B. 全量 vs 增量**：属性用 diff；结构操作用实体级全量 JSON 快照（v1 简单可靠）。
- **C. 栈上限与内存**：undo 栈上限建议 100；实体快照按需序列化，不在栈上保留整场景。
- **D. `_entityMap` 按值存储 Entity 的既有隐患**：`IComponent::_owner` 是指向 map
  元素的裸指针，map 重哈希/擦除会悬垂；且 `ECSRegistry::addComponent` 路径不设
  owner。undo 恢复实体后必须修复 owner（或统一走 `Entity::addComponent` 设置路径）。
  该问题在 undo 之前就存在，本计划只保证"恢复后 owner 有效"，不重构存储。
- **E. 合并策略**：同一事务标签且间隔 < 合并窗口（如 1s）的连续命令合并为一个
  历史条目；gizmo 拖动天然是一个事务，不再额外合并。
- **F. 脚本隔离**：RPC/JS 写入不进栈，避免异步事务边界被打穿；文档注明。

## 6. 里程碑

- P0 前置：场景 dirty 追踪 + 快照/恢复工具 + RenderContext old/new 捕获。
- P1 框架：EditorCommand / EditorUndoService / 事务与 cancel + 菜单快捷键。
- P2 属性编辑接入：PropertyEditCommand + onEdit 接线 + 资源失效重放。
- P3 结构操作接入：实体/组件增删复制移动 + gizmo 拖动事务。
- P4 收尾：PIE/场景切换清栈、选中集刷新、测试与文档。

详细任务清单见 `todo.md`，执行记录见 `progress.md`。
