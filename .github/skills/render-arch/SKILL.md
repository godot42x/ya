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

## Shader 处理流程（配置 → Shader → C++ 头 → C++ 使用）

目标：让 shader 常量与布局定义能被 C++ 侧稳定复用，避免“运行时值 / 头文件值 / shader 值”三份漂移。

```
Engine/Config/Engine.json
  └─ shader.defines（例如 MAX_POINT_LIGHTS）
  ↓
Engine/Shader/Shader.xmake.lua (Step 0)
  └─ 生成 Engine/Shader/GLSL/Common/Limits.glsl
  └─ 生成 Engine/Shader/Slang/Common/Limits.slang
  ↓
GLSL/Slang 源文件通过 #include 引入 Limits
  ↓
glsl_gen_header.py / slang_gen_header.py 生成 C++ 头
  └─ Engine/Shader/GLSL/Generated/*.h
  └─ Engine/Shader/Slang/Generated/*.h
  ↓
C++（如 RenderDefines.h）include 生成头并 using 常量/结构体
```

推荐做法：
- C++ 仅引用生成头里的常量（例如 `Common.Limits.glsl.h`），不要再手写重复常量。
- shader 编译参数避免重复注入同名宏（例如 MAX_POINT_LIGHTS），防止覆盖统一源。

## Shader.xmake.lua 设计思路

`target("shader")` 的职责不是“编译业务代码”，而是“生成与同步 shader 相关中间产物”。

分层设计：
1. **Step 0（配置落盘）**：从 `Engine.json` 生成 `Common/Limits.*`，做统一常量出口。
2. **Step 1（Slang 头生成）**：批量调用 `slang_gen_header.py` 产出反射头。
3. **Step 2（GLSL 头生成）**：批量调用 `glsl_gen_header.py` 产出 std140 对齐头。

实现原则：
- **批处理优先**：一次 Python 进程处理多文件，减少启动开销。
- **增量优先**：只在内容变化时写文件，减少无意义 mtime 变化。
- **职责单一**：xmake 负责 orchestrate（编排），脚本负责 parse/generate（解析与生成）。

## 统一数据源思路（Single Source of Truth）

核心：同一个配置值只能有一个权威来源，其余路径只“消费”不“重定义”。

以 `MAX_POINT_LIGHTS` 为例：
- 权威来源：`Engine/Config/Engine.json`
- 派生文件：`Common/Limits.glsl` / `Common/Limits.slang`
- C++ 来源：`Common.Limits.glsl.h` 中的 `constexpr`

落地规则：
1. 不在各 shader 文件中硬编码重复值。
2. 不在 C++ shader desc 里重复注入同名宏覆盖配置值。
3. 当值变更时，只改 `Engine.json`，其余由生成链自动收敛。

## Image 与 RenderTarget 的 Layout 完整生命周期

> 实现文件：`VulkanImage.cpp`、`VulkanRenderTarget.cpp`、`VulkanCommandBuffer.cpp`

### 1. Image 创建（`VulkanImage::allocate`）

```
vkCreateImage → initialLayout 强制为 VK_IMAGE_LAYOUT_UNDEFINED
VulkanImage::_layout = UNDEFINED

if (ci.initialLayout != Undefined):
    beginIsolateCommands("ImageInitialLayout")   ← 独立 cmdBuf，立即 submit + waitIdle
    transitionImageLayout(this, Undefined → ci.initialLayout)
        ← subresourceRange 使用 VK_REMAINING_ARRAY_LAYERS + VK_REMAINING_MIP_LEVELS
        ← 覆盖所有 layer（含多 layer shadow map）
        ← image._layout = ci.initialLayout
    endIsolateCommands
```

Shadow Map depth image 配置：`initialLayout = DepthStencilAttachmentOptimal`  
→ 创建后所有 layer 立即处于 `DepthStencilAttachmentOptimal`

### 2. RenderTarget 创建（`VulkanRenderTarget::recreateImagesAndFrameBuffer`）

```
对每个 framebuffer:
    color / depth / resolve image → VulkanImage::create(ci) → allocate()
    IFrameBuffer::create(images)  ← 仅打包引用，不做 layout 操作
```

### 3. `beginRendering`（Dynamic Rendering 路径）

```
renderTarget->beginFrame(cmdBuf)       ← 若 bDirty 则 recreate RT
collectRenderTargetTransitions(rt, bInitial=true)
    ← color:   initialLayout（默认 ColorAttachmentOptimal）
    ← depth:   initialLayout（DepthStencilAttachmentOptimal）
    ← 若 old == new → 跳过，不产生 barrier
VulkanImage::transitionLayouts(barriers)
    ← VK_REMAINING_ARRAY_LAYERS 覆盖全部 layer
    ← image._layout = newLayout
vkCmdBeginRendering(layerCount=1)      ← 硬编码 imageLayout（见上表）
```

### 4. `endRendering`（Dynamic Rendering 路径）

```
vkCmdEndRendering()
collectRenderTargetTransitions(rt, bInitial=false)
    ← depth:  finalLayout → ShaderReadOnlyOptimal
    ← 若 finalLayout == Undefined → 跳过
VulkanImage::transitionLayouts(barriers)
    ← VK_REMAINING_ARRAY_LAYERS 覆盖全部 layer
    ← image._layout = ShaderReadOnlyOptimal
renderTarget->endFrame(cmdBuf)         ← 更新 _currentFrameIndex
```

### 5. Shadow Map 全帧时序

```
App 初始化：
  allocate shadow image → isolateCmd: UNDEFINED → DepthStencilAttachmentOptimal [all layers]

每帧：
  beginRendering(shadow)
    → DepthStencilAttachmentOptimal → DepthStencilAttachmentOptimal (skip)
    → vkCmdBeginRendering

  shadow draw calls → 写深度

  endRendering(shadow)
    → DepthStencilAttachmentOptimal → ShaderReadOnlyOptimal [all layers]

  phong pass → 采样 shadow map (ShaderReadOnlyOptimal ✓)

  下帧 beginRendering(shadow)
    → ShaderReadOnlyOptimal → DepthStencilAttachmentOptimal [all layers]
```

### 6. 关键陷阱：多 layer image 的 barrier 覆盖

`transitionLayout`（无显式 range）和 `transitionLayouts`（`useRange=false`）  
**必须**使用 `VK_REMAINING_ARRAY_LAYERS` / `VK_REMAINING_MIP_LEVELS`，否则只过渡 layer 0，  
其余 layer 停在 UNDEFINED，shader 读时触发 `VUID-vkCmdDraw-None-09600`。

```cpp
// 正确写法
barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
```

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
