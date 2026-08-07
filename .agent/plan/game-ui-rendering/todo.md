# Game UI / 2D Rendering — TODO

> 对应 `plan.md`（2026-08-07：统一 scene tree 混合 2D/3D，Node2D 纯节点不接 ECS）。

## Phase 1 —— 最小闭环（统一树 + 游戏内 UI）

- [x] 1.1 `Render2D` 裁剪栈（pushClipRect/popClipRect + 批次 flush + scissor）
- [x] 1.2 Deferred 独立 UI 渲染段（`appendUI` 在 overlay 后、postprocess 前，
      bloom 后）；**Forward 未接（UI 仍随 overlay 进 bloom，待办）**
- [x] 1.3 `Node2D : Node` 基类 + `Scene::createUINode` + `_entityLessNodes` 所有权
- [x] 1.4 具体节点：`UICanvasNode`/`UIPanelNode`/`UITextNode`/`UIButtonNode`
      （反射字段，可序列化）
- [x] 1.5 树遍历渲染：`UISceneRenderer` 每帧收集 Node2D → Render2D 批次
      （zOrder 稳定排序）。**注：实现放在 Runtime 层（Core/UI 不引 Scene），
      UIManager 保持空壳，Phase 3 清理；update(dt) 未接（v1 无动画）**
- [x] 1.6 序列化：`nodeTree` 条目 nodeType + 反射字段（无 entityRef）；
      旧文件兼容；PIE clone Node2D 分支
- [x] 1.7 事件语义：`UISceneRenderer::handleEvent` 返回消费；fallback 仅在
      交互节点命中时吞事件（面板/canvas passive）；`UIButtonNode` hitTest +
      坐标（viewportRect 偏移，scale 换算待 FrameBufferScale 接入）
- [x] 1.8 编辑器：Hierarchy Node2D 行 + 节点级选择通道；Inspector 反射编辑；
      创建菜单反射自动收集（2D 子菜单 + 3D 预设注册表 NodeCreateRegistry）
- [x] 1.9b 编辑器 2D/3D 视口模式（Godot 风格开发期查看；运行时 3D+2D 叠加不变）：
      Mode2D = 网格画布 + Node2D screen-space 合成 + 轻量 pan/zoom 导航；
      CLI：viewport.set_mode / viewport.pan_zoom
- [x] 1.10b 场景树 CLI：node.types/list/get/create/set/move/destroy + scene.create_preset
      （ScriptApiRegistry → RPC/MCP/JS 自动暴露）
- [x] 1.9 测试：混合树 roundtrip、clone、hitTest/zOrder/visible；
      HelloMaterial 运行时 UI 演示（HUD 面板 + 文本 + 可点击按钮）
- [x] 1.10 提交：`6421b08d [scene]` + `361dc13b [render/ui]` +
      `[example]`（演示）

## Phase 2 —— 布局、文本与 2D 世界空间

- [ ] anchor/pivot、百分比尺寸、HBox/VBox/Stack、文本驱动尺寸
- [ ] 9-slice 封装、字形缓存上限、字体 fallback、焦点/键盘导航
- [ ] Node2D world-space 模式（Godot Sprite2D 语义）→ 2D 实体入同一棵树
- [ ] 编辑器 UI 树面板完善

## Phase 3 —— 独立 UI 后端

- [ ] UIRender 顶点批 + 图集 + 裁剪；Node2D 渲染切换依赖
- [ ] Render2D 与 UI 解耦；清理 `UIComponent` stub 与 DetailsView 引用
- [ ] （远期）editor 面板迁移试点
