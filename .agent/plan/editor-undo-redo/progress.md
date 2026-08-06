# Editor Do/Undo/Redo/Cancel 进度

## 当前状态

- 计划建立日期：2026-08-07
- 当前阶段：调研完成，计划已建立；尚未开始实现
- 当前执行任务：无（P0 待领取，建议从 ED-001/ED-002 开始）

## 调研结论（2026-08-07）

- 变更入口盘点：Inspector 属性编辑（TypeRenderer 原地直写）、gizmo 拖动（无事务边界）、
  层级面板实体增删/复制/重排（延迟批处理）、组件增删（ECSRegistry 路径）、资源失效副作用。
- 已有基础设施：`SceneSerializer::serializeEntity/deserializeEntity`（UUID 身份 +
  反射序列化 + onPostSerialize）、`serializeNodeTree`（UUID 引用）、
  `RenderModificationRecord` 已预留 old/new 字段（未填值）、`cloneComponent`、
  `PhysicsSystem::reconcileBodies` 自愈、ResourceResolveSystem 统一失效入口。
- 关键缺口：无命令/事务框架；无场景 dirty 追踪；`onEdit()` 未被调用；
  Esc 直接 quit 与 cancel 语义冲突；gizmo 无 begin/end 事件。
- 风险：entt handle 复用（快照键必须用 UUID）；`_entityMap` 按值存储导致的
  `_owner` 悬垂隐患（恢复后必须修复 owner）；脚本/RPC 写入不进栈（v1 隔离）。

## 执行记录

（尚无实现提交。）
