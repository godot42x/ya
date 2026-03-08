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

## Dynamic Rendering 的 Layout Transition 机制

> 实现文件：`Engine/Source/Platform/Render/Vulkan/VulkanCommandBuffer.cpp`

YA 使用 `VK_KHR_dynamic_rendering`（无 VkRenderPass），通过手动 pipeline barrier 模拟 RenderPass 的 layout 管理。

### 每帧流程

```
[begin frame]
  collectRenderTargetTransitions(rt, bInitial=true)   → 读 attachmentDesc.initialLayout
  VulkanImage::transitionLayouts(...)                 → 发出 barrier，图像进入 initialLayout
  beginDynamicRenderingFromRenderTarget(rt, ...)      → 开始 dynamic rendering
    └─ 硬编码 imageLayout = COLOR_ATTACHMENT_OPTIMAL
               depthstencil = DEPTH_STENCIL_ATTACHMENT_OPTIMAL
  [draw calls]
  vkCmdEndRenderingKHR
  collectRenderTargetTransitions(rt, bInitial=false)  → 读 attachmentDesc.finalLayout
  VulkanImage::transitionLayouts(...)                 → 发出 barrier，图像进入 finalLayout
[end frame]
```

### 关键约束

`beginDynamicRenderingFromRenderTarget` 硬编码告诉驱动当前图像在哪个 layout：

| 附件类型   | 硬编码 imageLayout                      |
|----------|----------------------------------------|
| color    | `COLOR_ATTACHMENT_OPTIMAL`             |
| depth    | `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`     |
| resolve  | `COLOR_ATTACHMENT_OPTIMAL`             |

**`AttachmentDescription::initialLayout` 必须与之一致**，否则 barrier 把图像转到错误 layout，驱动（尤其 Intel Arc）会拒绝写入 → 残留旧内容 → 马赛克。

### RenderTarget 描述约定

```cpp
// color / resolve 附件
.initialLayout = EImageLayout::ColorAttachmentOptimal,   // ← 必须
.finalLayout   = EImageLayout::ShaderReadOnlyOptimal,    // 供下帧采样

// depth 附件
.initialLayout = EImageLayout::DepthStencilAttachmentOptimal,
.finalLayout   = EImageLayout::DepthStencilAttachmentOptimal,
```

> ❌ 常见错误：`initialLayout = ShaderReadOnlyOptimal` 或 `Undefined`
> — barrier 目标错误，dynamic rendering 期望的 layout 不满足 → 驱动静默丢弃写入

## 退出条件

- 改动边界清晰：抽象层、平台层、业务层职责不混杂
