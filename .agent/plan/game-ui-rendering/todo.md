# Game UI / 2D Rendering — TODO

> 对应 `plan.md`（2026-08-07：统一 scene tree 混合 2D/3D，Node2D 纯节点不接 ECS）。

## Phase 1 —— 最小闭环（统一树 + 游戏内 UI）

- [ ] 1.1 `Render2D` 裁剪栈（pushClipRect/popClipRect + 批次 flush + scissor）
- [ ] 1.2 Deferred/Forward 独立 UI 渲染段，`UIManager::render()` 从 overlay
      recorder 移出；验证 bloom 依赖与 UI 不进 bloom
- [ ] 1.3 `Node2D : Node` 基类（position/size/visible/zOrder/hitTest/屏幕变换链）
      + `Scene::createNode2D` + `_entityLessNodes` 所有权
- [ ] 1.4 具体节点：`UICanvasNode`/`UIPanelNode`/`UITextNode`/`UIButtonNode`
      （反射字段，可序列化）
- [ ] 1.5 树遍历渲染：UIManager 每帧收集 active scene 树 Node2D → Render2D 批次
      （zOrder 分组）；`UIManager::update(dt)` 接入 tickLogic
- [ ] 1.6 序列化：`nodeTree` 条目 nodeType + 反射字段（无 entityRef）；
      旧文件兼容；PIE clone Node2D 分支
- [ ] 1.7 事件语义：`UIManager::onEvent` 返回消费；fallback 仅在命中时吞事件；
      `UIButtonNode` 走 hitTest + 坐标缩放
- [ ] 1.8 编辑器：Hierarchy Node2D 行 + 节点级选择通道；Inspector 反射编辑；
      创建菜单 "2D" 子菜单
- [ ] 1.9 测试：混合树 roundtrip、clone、hitTest、裁剪栈；HelloMaterial 手动
      验收（混合树/点击不穿透/PIE/bloom/无残留）
- [ ] 1.10 提交分类（render / scene / serialization / editor / test）

## Phase 2 —— 布局、文本与 2D 世界空间

- [ ] anchor/pivot、百分比尺寸、HBox/VBox/Stack、文本驱动尺寸
- [ ] 9-slice 封装、字形缓存上限、字体 fallback、焦点/键盘导航
- [ ] Node2D world-space 模式（Godot Sprite2D 语义）→ 2D 实体入同一棵树
- [ ] 编辑器 UI 树面板完善

## Phase 3 —— 独立 UI 后端

- [ ] UIRender 顶点批 + 图集 + 裁剪；Node2D 渲染切换依赖
- [ ] Render2D 与 UI 解耦；清理 `UIComponent` stub 与 DetailsView 引用
- [ ] （远期）editor 面板迁移试点
