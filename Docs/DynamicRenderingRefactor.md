# 渲染框架重构设计文档

## 目标

同时支持传统 Subpass 渲染和 Vulkan Dynamic Rendering，提供统一的抽象接口。

## 架构设计

### 1. 渲染模式枚举

```cpp
namespace ERenderingMode {
enum T {
    Subpass,          // 传统 RenderPass + Subpass
    DynamicRendering, // Vulkan 1.3+ Dynamic Rendering
    Auto              // 自动选择（根据驱动支持）
};
}
```

### 2. 核心接口变化

#### IRenderPass 接口扩展

```cpp
struct IRenderPass {
    // 获取渲染模式
    virtual ERenderingMode::T getRenderingMode() const = 0;
    
    // Dynamic Rendering 专用 API
    virtual void beginDynamicRendering(
        ICommandBuffer* cmdBuffer,
        const DynamicRenderingInfo& info) = 0;
    
    virtual void endDynamicRendering(ICommandBuffer* cmdBuffer) = 0;
    
    // 传统 Subpass API（保持向后兼容）
    virtual void begin(...) = 0;  // 现有API
    virtual void end(...) = 0;
};
```

#### ICommandBuffer 接口扩展

```cpp
struct ICommandBuffer {
    // Dynamic Rendering 命令
    virtual void beginRendering(const DynamicRenderingInfo& info) = 0;
    virtual void endRendering() = 0;
    
    // 图像布局转换（Dynamic Rendering 需要）
    virtual void transitionImageLayout(
        void* image,
        EImageLayout oldLayout,
        EImageLayout newLayout,
        const ImageSubresourceRange& range) = 0;
};
```

### 3. 配置结构体

#### DynamicRenderingInfo

```cpp
struct RenderingAttachmentInfo {
    void* imageView;                    // VkImageView 或 OpenGL FBO texture
    EImageLayout::T imageLayout;        // 图像布局
    EAttachmentLoadOp::T loadOp;       // 加载操作
    EAttachmentStoreOp::T storeOp;     // 存储操作
    ClearValue clearValue;              // 清屏值
    
    // MSAA 解析（可选）
    void* resolveImageView;             
    EImageLayout::T resolveLayout;
    EResolveMode::T resolveMode;
};

struct DynamicRenderingInfo {
    Extent2D renderArea;                // 渲染区域
    uint32_t layerCount;                // 层数（通常为1）
    
    // 颜色附件（支持多个）
    std::vector<RenderingAttachmentInfo> colorAttachments;
    
    // 深度附件（可选）
    RenderingAttachmentInfo* depthAttachment;
    
    // 模板附件（可选）
    RenderingAttachmentInfo* stencilAttachment;
};
```

### 4. 渲染流程对比

#### 传统 Subpass 流程

```cpp
// 1. 创建 RenderPass
auto renderPass = IRenderPass::create(render);
renderPass->recreate(RenderPassCreateInfo{
    .attachments = {...},
    .subpasses = {...},
    .dependencies = {...}
});

// 2. 创建 Framebuffer
auto framebuffer = IFramebuffer::create(...);

// 3. 录制命令
cmdBuffer->begin();
renderPass->begin(cmdBuffer, framebuffer, extent, clearValues);
// 绘制命令...
renderPass->end(cmdBuffer);
cmdBuffer->end();
```

#### Dynamic Rendering 流程

```cpp
// 1. 创建 RenderPass（仅用于管线兼容性）
auto renderPass = IRenderPass::create(render);
renderPass->setRenderingMode(ERenderingMode::DynamicRendering);
renderPass->recreate(DynamicRenderPassCreateInfo{
    .colorFormats = {EFormat::R8G8B8A8_UNORM},
    .depthFormat = EFormat::D32_SFLOAT,
});

// 2. 无需 Framebuffer！

// 3. 录制命令
cmdBuffer->begin();
cmdBuffer->beginRendering(DynamicRenderingInfo{
    .renderArea = extent,
    .colorAttachments = {{
        .imageView = swapchainImageView,
        .imageLayout = EImageLayout::ColorAttachmentOptimal,
        .loadOp = EAttachmentLoadOp::Clear,
        .storeOp = EAttachmentStoreOp::Store,
        .clearValue = ClearValue(0.1f, 0.1f, 0.1f, 1.0f),
    }},
    .depthAttachment = &depthInfo,
});
// 绘制命令...
cmdBuffer->endRendering();
cmdBuffer->end();
```

### 5. 实现策略

#### Vulkan 后端

- 检测 VK_KHR_dynamic_rendering 或 Vulkan 1.3+ 支持
- Dynamic Rendering 模式：
  - 使用 vkCmdBeginRendering / vkCmdEndRendering
  - 管线创建时使用 VkPipelineRenderingCreateInfo
  - 无需 VkFramebuffer
- Subpass 模式：
  - 保持现有实现不变

#### OpenGL 后端

- 统一映射到 FBO + MRT
- Dynamic Rendering 模式：
  - 动态绑定 FBO 附件
  - 在 beginRendering 时执行 glFramebufferTexture
- Subpass 模式：
  - 映射为固定 FBO 配置

### 6. API 使用示例

#### 自动模式（推荐）

```cpp
RenderPassCreateInfo ci{
    .renderingMode = ERenderingMode::Auto,  // 自动选择
    .attachments = {...},
};

if (renderPass->getRenderingMode() == ERenderingMode::DynamicRendering) {
    // 使用 Dynamic Rendering API
} else {
    // 使用传统 Subpass API
}
```

#### 混合场景（延迟渲染）

```cpp
// G-Buffer Pass - 使用 Dynamic Rendering
cmdBuffer->beginRendering(DynamicRenderingInfo{
    .colorAttachments = {
        {.imageView = positionBuffer, ...},
        {.imageView = normalBuffer, ...},
        {.imageView = albedoBuffer, ...},
    },
    .depthAttachment = &depthInfo,
});
// 几何Pass绘制...
cmdBuffer->endRendering();

// Lighting Pass - 使用 Dynamic Rendering
cmdBuffer->beginRendering(DynamicRenderingInfo{
    .colorAttachments = {{.imageView = finalColorBuffer, ...}},
});
// 光照计算...
cmdBuffer->endRendering();

// Post-Processing - 使用 Dynamic Rendering
cmdBuffer->beginRendering(DynamicRenderingInfo{
    .colorAttachments = {{.imageView = swapchainImage, ...}},
});
// 后处理...
cmdBuffer->endRendering();
```

### 7. 管线兼容性

#### Vulkan

```cpp
// Dynamic Rendering 模式管线
VkPipelineRenderingCreateInfo renderingInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
    .colorAttachmentCount = 1,
    .pColorAttachmentFormats = &format,
    .depthAttachmentFormat = depthFormat,
};

VkGraphicsPipelineCreateInfo pipelineInfo{
    .pNext = &renderingInfo,  // 关键：绑定 Dynamic Rendering 信息
    // 无需 .renderPass 和 .subpass 字段
};
```

#### 抽象层

```cpp
struct GraphicsPipelineCreateInfo {
    ERenderingMode::T renderingMode;
    
    // Subpass 模式字段
    uint32_t subPassRef;
    
    // Dynamic Rendering 模式字段
    std::vector<EFormat::T> colorAttachmentFormats;
    EFormat::T depthAttachmentFormat;
    EFormat::T stencilAttachmentFormat;
};
```

### 8. 迁移路径

#### 阶段 1：接口扩展（不破坏现有代码）
- 添加新的 Dynamic Rendering 接口
- 保持传统 Subpass API 不变
- 现有代码继续工作

#### 阶段 2：实现 Vulkan Dynamic Rendering
- VulkanRenderPass 添加 Dynamic Rendering 支持
- VulkanCommandBuffer 实现 beginRendering/endRendering
- VulkanPipeline 支持两种模式的创建

#### 阶段 3：实现 OpenGL 映射
- OpenGLRenderPass 映射 Dynamic Rendering 到 FBO
- OpenGLCommandBuffer 实现动态附件绑定

#### 阶段 4：应用层适配
- App::init 中支持选择渲染模式
- 材质系统适配两种模式
- 示例项目添加 Dynamic Rendering 演示

### 9. 性能优化

#### Dynamic Rendering 优势
- **无 Framebuffer 开销**：减少对象创建/销毁
- **灵活的渲染目标切换**：后处理流水线更高效
- **更好的驱动优化**：现代驱动针对 Dynamic Rendering 优化

#### 最佳实践
- 使用 `STORE_OP_DONT_CARE` 丢弃不需要的中间结果
- 合理设置图像布局，减少不必要的转换
- MSAA 解析使用 `resolveAttachment` 而非额外的 Blit

### 10. 调试与验证

#### 验证点
- [ ] Vulkan 1.3+ 设备支持检测
- [ ] VK_KHR_dynamic_rendering 扩展启用
- [ ] 管线兼容性验证（RenderPass vs Dynamic Rendering）
- [ ] 图像布局正确性检查
- [ ] 多颜色附件支持（G-Buffer）
- [ ] MSAA 解析正确性

#### 调试工具
- RenderDoc 支持 Dynamic Rendering（需 1.18+ 版本）
- Vulkan Validation Layers 会检查布局/格式错误
- 添加日志输出当前渲染模式

### 11. 限制与注意事项

#### Vulkan
- 需要 Vulkan 1.3 或 VK_KHR_dynamic_rendering 扩展
- 管线必须使用 VkPipelineRenderingCreateInfo 创建
- 图像布局需手动管理（驱动不会自动转换）

#### OpenGL
- Dynamic Rendering 映射到 FBO，无本质性能差异
- 仍需 FBO 对象，只是延迟绑定附件

#### 通用
- 现有的 RenderPass 对象仍需创建（用于管线兼容性）
- 两种模式不能在同一个管线中混用
- 需要明确指定图像布局（不能依赖自动转换）

## 总结

这个重构提供了：
1. **向后兼容**：现有 Subpass 代码无需修改
2. **现代化支持**：完整的 Dynamic Rendering 实现
3. **统一抽象**：跨后端的一致API
4. **灵活性**：运行时选择渲染模式
5. **性能优化**：充分利用 Dynamic Rendering 优势
