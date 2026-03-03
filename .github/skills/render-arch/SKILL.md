---
name: ya-render-arch
description: YA Engine 渲染架构与模块边界
---

## 适用场景

- 用户修改渲染管线、材质、后端实现
- 关键词涉及：`App.RenderPipeline`、`IRender`、`VulkanRender`、`OpenGLRender`
- 需要判断改动应放在抽象层还是后端层

## 架构要点

- 多后端：`IRender` 抽象 + Vulkan / OpenGL 实现
- `App` 直接持有 `IRender`
- `IRenderTarget` 使用共享所有权（`shared_ptr`）
- 兼容路径可含 `IRenderPass`，但优先遵循现有管线组织

## 目录锚点

- `Engine/Source/Core/App/`
- `Engine/Source/Render/`
- `Engine/Source/Platform/Render/Vulkan/`
- `Engine/Source/Platform/Render/OpenGL/`

## 决策原则

1. 公共能力先落抽象层，再按后端补实现。
2. 后端差异仅在平台层扩散，不反向污染上层接口。
3. 管线问题先查初始化顺序与资源生命周期，再查 shader / 状态配置。

## 退出条件

- 改动边界清晰：抽象层、平台层、业务层职责不混杂
