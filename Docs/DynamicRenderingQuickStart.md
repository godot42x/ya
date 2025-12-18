# Dynamic Rendering 使用指南

## 快速开始

### 1. 检查支持并选择模式

```cpp
// 在 App::init() 中
RenderPassCreateInfo ci{
    .renderingMode = ERenderingMode::Auto, // 自动选择最佳模式
    .attachments = {...},
    .subpasses = {...},
};

_renderpass = IRenderPass::create(_render);
_renderpass->recreate(ci);

// 检查实际使用的模式
ERenderingMode::T actualMode = _renderpass->getRenderingMode();
YA_CORE_INFO("Using rendering mode: {}", 
    actualMode == ERenderingMode::DynamicRendering ? "Dynamic" : "Subpass");
```

### 2. 使用 Dynamic Rendering 绘制

```cpp
// 录制命令缓冲
cmdBuffer->begin();

// 配置颜色附件
RenderingAttachmentInfo colorAttachment{
    .imageView = swapchainImageViews[currentImage],
    .imageLayout = EImageLayout::ColorAttachmentOptimal,
    .loadOp = EAttachmentLoadOp::Clear,
    .storeOp = EAttachmentStoreOp::Store,
    .clearValue = ClearValue(0.1f, 0.1f, 0.1f, 1.0f),
};

// 配置深度附件
RenderingAttachmentInfo depthAttachment{
    .imageView = depthImageView,
    .imageLayout = EImageLayout::DepthStencilAttachmentOptimal,
    .loadOp = EAttachmentLoadOp::Clear,
    .storeOp = EAttachmentStoreOp::DontCare, // 深度不需要保存
    .clearValue = ClearValue(1.0f, 0),
};

// 开始动态渲染
cmdBuffer->beginRendering(DynamicRenderingInfo{
    .renderArea = {.width = width, .height = height},
    .layerCount = 1,
    .colorAttachments = {colorAttachment},
    .pDepthAttachment = &depthAttachment,
});

// 绘制命令（和传统方式一致）
cmdBuffer->bindPipeline(pipeline);
cmdBuffer->setViewport(0, 0, width, height);
cmdBuffer->setScissor(0, 0, width, height);
cmdBuffer->draw(3, 1, 0, 0);

// 结束动态渲染
cmdBuffer->endRendering();

// 转换布局用于显示
cmdBuffer->transitionImageLayout(
    swapchainImages[currentImage],
    EImageLayout::ColorAttachmentOptimal,
    EImageLayout::PresentSrcKHR,
    ImageSubresourceRange{.aspectMask = 1, .levelCount = 1, .layerCount = 1}
);

cmdBuffer->end();
```

### 3. 创建兼容的管线

```cpp
// Dynamic Rendering 模式的管线
GraphicsPipelineCreateInfo pipelineCI{
    .renderingMode = ERenderingMode::DynamicRendering,
    .colorAttachmentFormats = {EFormat::R8G8B8A8_UNORM}, // 指定格式而非 RenderPass
    .depthAttachmentFormat = EFormat::D32_SFLOAT,
    .shaderDesc = shaderDesc,
    // ... 其他配置
};

auto pipeline = IGraphicsPipeline::create(_render, _renderpass.get());
pipeline->recreate(pipelineCI);
```

## 进阶场景

### 多渲染目标（延迟渲染 G-Buffer）

```cpp
// G-Buffer Pass：渲染到 3 个颜色附件
RenderingAttachmentInfo positionAttachment{
    .imageView = positionBufferView,
    .imageLayout = EImageLayout::ColorAttachmentOptimal,
    .loadOp = EAttachmentLoadOp::Clear,
    .storeOp = EAttachmentStoreOp::Store,
};

RenderingAttachmentInfo normalAttachment{
    .imageView = normalBufferView,
    .imageLayout = EImageLayout::ColorAttachmentOptimal,
    .loadOp = EAttachmentLoadOp::Clear,
    .storeOp = EAttachmentStoreOp::Store,
};

RenderingAttachmentInfo albedoAttachment{
    .imageView = albedoBufferView,
    .imageLayout = EImageLayout::ColorAttachmentOptimal,
    .loadOp = EAttachmentLoadOp::Clear,
    .storeOp = EAttachmentStoreOp::Store,
};

cmdBuffer->beginRendering(DynamicRenderingInfo{
    .renderArea = renderArea,
    .colorAttachments = {
        positionAttachment,
        normalAttachment,
        albedoAttachment,
    },
    .pDepthAttachment = &depthAttachment,
});

// 渲染几何...
cmdBuffer->bindPipeline(geometryPassPipeline);
// ...

cmdBuffer->endRendering();
```

### 后处理流水线（多次渲染）

```cpp
// Pass 1: 场景渲染到中间缓冲
cmdBuffer->beginRendering(DynamicRenderingInfo{
    .renderArea = renderArea,
    .colorAttachments = {{
        .imageView = intermediateColorView,
        .imageLayout = EImageLayout::ColorAttachmentOptimal,
        .loadOp = EAttachmentLoadOp::Clear,
        .storeOp = EAttachmentStoreOp::Store,
    }},
});
// 绘制场景...
cmdBuffer->endRendering();

// 转换中间缓冲为纹理可读
cmdBuffer->transitionImageLayout(
    intermediateColorImage,
    EImageLayout::ColorAttachmentOptimal,
    EImageLayout::ShaderReadOnlyOptimal,
    subresourceRange
);

// Pass 2: 后处理到交换链
cmdBuffer->beginRendering(DynamicRenderingInfo{
    .renderArea = renderArea,
    .colorAttachments = {{
        .imageView = swapchainImageView,
        .imageLayout = EImageLayout::ColorAttachmentOptimal,
        .loadOp = EAttachmentLoadOp::DontCare, // 不需要加载
        .storeOp = EAttachmentStoreOp::Store,
    }},
});
// 绑定中间缓冲为纹理，执行后处理...
cmdBuffer->bindPipeline(postProcessPipeline);
cmdBuffer->bindDescriptorSets(/* 包含 intermediateColorImage 的纹理 */);
cmdBuffer->draw(3, 1, 0, 0); // 全屏三角形
cmdBuffer->endRendering();
```

### MSAA 抗锯齿

```cpp
// 创建 MSAA 颜色附件（4x采样）
RenderingAttachmentInfo msaaColorAttachment{
    .imageView = msaaColorView, // 4x MSAA image
    .imageLayout = EImageLayout::ColorAttachmentOptimal,
    .loadOp = EAttachmentLoadOp::Clear,
    .storeOp = EAttachmentStoreOp::DontCare, // MSAA 结果不需要保存
    .resolveImageView = swapchainImageView, // 解析到这里
    .resolveLayout = EImageLayout::ColorAttachmentOptimal,
    .resolveMode = EResolveMode::Average, // 平均采样
};

cmdBuffer->beginRendering(DynamicRenderingInfo{
    .renderArea = renderArea,
    .colorAttachments = {msaaColorAttachment},
    .pDepthAttachment = &msaaDepthAttachment, // 深度也需要 MSAA
});

// 绘制（自动解析到 swapchainImageView）...

cmdBuffer->endRendering();
```

## 传统 Subpass 模式对比

### 传统方式

```cpp
// 需要创建 Framebuffer
auto framebuffer = createFramebuffer(renderPass, attachments);

// 开始 RenderPass
renderPass->begin(cmdBuffer, framebuffer, extent, clearValues);

// 绘制...

// 结束 RenderPass
renderPass->end(cmdBuffer);
```

### Dynamic Rendering

```cpp
// 无需 Framebuffer！

// 开始 Dynamic Rendering
cmdBuffer->beginRendering(dynamicRenderingInfo);

// 绘制...

// 结束 Dynamic Rendering
cmdBuffer->endRendering();
```

## 注意事项

### 1. 图像布局管理

Dynamic Rendering 需要手动管理图像布局：

```cpp
// ❌ 错误：忘记转换布局
cmdBuffer->beginRendering(...); // 图像布局可能不正确

// ✅ 正确：确保正确的布局
cmdBuffer->transitionImageLayout(
    image,
    EImageLayout::Undefined, // 或上一次使用的布局
    EImageLayout::ColorAttachmentOptimal,
    subresourceRange
);
cmdBuffer->beginRendering(...);
```

### 2. LoadOp 和 StoreOp 优化

```cpp
// ✅ 优化：丢弃不需要的附件
RenderingAttachmentInfo tempAttachment{
    .loadOp = EAttachmentLoadOp::DontCare, // 不需要加载旧内容
    .storeOp = EAttachmentStoreOp::DontCare, // 结果不需要保存
};

// ✅ 优化：深度附件通常不需要保存
RenderingAttachmentInfo depthAttachment{
    .storeOp = EAttachmentStoreOp::DontCare, // 节省带宽
};
```

### 3. 管线兼容性

```cpp
// Dynamic Rendering 管线必须指定格式
GraphicsPipelineCreateInfo ci{
    .renderingMode = ERenderingMode::DynamicRendering,
    .colorAttachmentFormats = {EFormat::R8G8B8A8_UNORM},
    .depthAttachmentFormat = EFormat::D32_SFLOAT,
};

// ❌ 错误：在 Dynamic Rendering 模式下仍使用 subPassRef
// ci.subPassRef = 0; // 这个字段会被忽略
```

### 4. 兼容性检查

```cpp
// 检查设备是否支持 Dynamic Rendering
if (_renderpass->getRenderingMode() == ERenderingMode::Subpass) {
    YA_CORE_WARN("Dynamic Rendering not supported, falling back to Subpass");
    // 使用传统 API
    renderPass->begin(...);
} else {
    // 使用 Dynamic Rendering API
    cmdBuffer->beginRendering(...);
}
```

## 性能优势

### 减少对象创建开销

- **Subpass**: 需要为每个附件组合创建 Framebuffer
- **Dynamic Rendering**: 无需 Framebuffer，直接指定附件

### 灵活的渲染目标切换

- **Subpass**: 切换渲染目标需要新的 RenderPass + Framebuffer
- **Dynamic Rendering**: 直接修改 DynamicRenderingInfo，无需重建对象

### 更好的驱动优化

现代驱动（NVIDIA, AMD）针对 Dynamic Rendering 有专门优化：
- 更少的验证开销
- 更好的内存管理
- 自动的布局优化

## 调试技巧

### 启用验证层

```cpp
// Vulkan Validation Layers 会检查：
// - 图像布局错误
// - 格式不匹配
// - 无效的 LoadOp/StoreOp 组合
```

### RenderDoc 调试

```cpp
// RenderDoc 1.18+ 支持 Dynamic Rendering
// 可以查看每次 beginRendering 的配置
// 验证附件格式、布局、清屏值
```

### 日志输出

```cpp
YA_CORE_INFO("Begin dynamic rendering:");
YA_CORE_INFO("  Color attachments: {}", info.colorAttachments.size());
YA_CORE_INFO("  Render area: {}x{}", info.renderArea.width, info.renderArea.height);
```

## 总结

Dynamic Rendering 提供了：
- ✅ 更简洁的 API（无需 Framebuffer）
- ✅ 更灵活的渲染目标管理
- ✅ 更好的性能（驱动优化）
- ✅ 更清晰的资源依赖关系

但需要注意：
- ⚠️ 手动管理图像布局
- ⚠️ 管线创建方式不同
- ⚠️ 需要 Vulkan 1.3+ 或扩展支持
