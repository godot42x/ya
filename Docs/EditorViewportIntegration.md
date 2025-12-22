# Editor Viewport Integration Guide

## 概述

在Ya引擎中集成ImGui Docking编辑器的Viewport窗口，需要以下步骤：

## 架构说明

```
App (HelloMaterial)
  ├─> 创建离屏Framebuffer (Editor Viewport)
  ├─> 渲染场景到Framebuffer
  ├─> 获取Framebuffer的颜色附件纹理
  ├─> 注册纹理到ImGui (VkDescriptorSet)
  └─> 传递给EditorLayer显示
  
EditorLayer
  ├─> viewportWindow() 中使用 ImGui::Image 显示纹理
  ├─> 跟踪 viewport 大小变化
  └─> 通知App resize framebuffer
```

## 实现步骤

### 1. 创建离屏Framebuffer

当前Ya引擎已经有 `IRenderTarget` 用于渲染到swapchain。对于Editor Viewport，我们需要：

#### 选项A: 修改现有RenderTarget（简单）
直接使用App现有的 `_rt` (RenderTarget) 渲染到swapchain，然后在Viewport窗口中**不显示任何纹理**，而是将Viewport窗口设为透明，让底层渲染穿透显示。

**优点**：无需额外framebuffer，实现简单  
**缺点**：Viewport无法自由调整大小，必须全屏

#### 选项B: 创建专用Editor Framebuffer（标准）
创建一个独立的离屏framebuffer，App渲染到这个framebuffer而不是swapchain。

**步骤**：

```cpp
// 在HelloMaterial.h中
std::shared_ptr<ya::ITexture> _viewportColorTexture;
std::shared_ptr<ya::ITexture> _viewportDepthTexture;
void* _viewportTextureDescriptor = nullptr; // VkDescriptorSet

void createViewportFramebuffer(uint32_t width, uint32_t height)
{
    auto* render = getRender();
    
    // 创建颜色附件纹理
    TextureCreateInfo colorTexCI{
        .width = width,
        .height = height,
        .format = ETextureFormat::RGBA8,
        .usage = ETextureUsage::ColorAttachment | ETextureUsage::Sampled,
        .name = "EditorViewportColor"
    };
    _viewportColorTexture = ITexture::create(render, colorTexCI);
    
    // 创建深度附件纹理
    TextureCreateInfo depthTexCI{
        .width = width,
        .height = height,
        .format = ETextureFormat::Depth24Stencil8,
        .usage = ETextureUsage::DepthStencilAttachment,
        .name = "EditorViewportDepth"
    };
    _viewportDepthTexture = ITexture::create(render, depthTexCI);
    
    // 创建RenderPass
    // TODO: 使用TextureAttachment创建RenderPass
    
    // 创建Framebuffer
    // TODO: 使用上面的纹理创建Framebuffer
    
    // 注册到ImGui
    auto* vkRender = getRender<VulkanRender>();
    VkImageView imageView = _viewportColorTexture->getHandleAs<VkImageView>();
    VkSampler sampler = vkRender->getDefaultSampler(); // 需要从somewhere获取
    
    _viewportTextureDescriptor = ImGui_ImplVulkan_AddTexture(
        sampler,
        imageView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
}
```

### 2. 在EditorLayer中显示

EditorLayer已经实现了 `viewportWindow()`，它会：
- 接收 `_viewportTextureId` (通过 `setViewportTexture()`)
- 使用 `ImGui::Image()` 显示纹理
- 跟踪viewport大小变化
- UV坐标翻转以适配Vulkan

### 3. 渲染流程

```cpp
void HelloMaterial::onRender(float dt)
{
    // 方案A: 渲染到swapchain（当前）
    Super::onRender(dt);
    
    // 方案B: 渲染到editor framebuffer
    // 1. Bind editor framebuffer
    // 2. 渲染场景
    // 3. Unbind
    // 4. Present to swapchain（只渲染ImGui）
}

void HelloMaterial::onRenderGUI()
{
    Super::onRenderGUI();
    
    if (_editorLayer)
    {
        // 同步viewport大小
        auto viewportSize = _editorLayer->getViewportSize();
        if (需要resize) {
            destroyViewportFramebuffer();
            createViewportFramebuffer(viewportSize.x, viewportSize.y);
        }
        
        // 设置纹理
        _editorLayer->setViewportTexture(_viewportTextureDescriptor);
        
        // 渲染ImGui
        _editorLayer->onImGuiRender();
    }
}
```

## 当前实现状态

### ✅ 已完成
- EditorLayer ImGui Docking 主界面
- Viewport窗口框架
- Viewport大小跟踪
- SceneHierarchyPanel 和 ContentBrowserPanel

### ⚠️ 待实现
- 离屏Framebuffer创建
- 纹理注册到ImGui
- Viewport resize处理
- 实际场景渲染到viewport

## 临时方案（快速测试）

如果要快速看到效果，可以使用**方案A**：

```cpp
void EditorLayer::viewportWindow()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
    
    // 设置窗口为透明背景
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    
    if (!ImGui::Begin("Viewport", nullptr, 
        ImGuiWindowFlags_NoScrollbar | 
        ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGui::End();
        return;
    }
    
    // 显示提示文本
    ImGui::TextColored(ImVec4(0, 1, 0, 1), "Scene renders below (passthrough)");
    
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::End();
}
```

这样Viewport窗口会透明，底层的场景渲染会透过窗口显示。

## 推荐实现顺序

1. **先用临时方案测试** ImGui Docking布局是否正常
2. **创建纹理和Framebuffer** 系统
3. **修改App渲染流程** 渲染到editor framebuffer
4. **实现viewport resize** 逻辑
5. **优化性能** 和交互

## 参考资料

- Hazel Editor: `EditorLayer::ViewPort()` 实现
- ImGui Vulkan Backend: `imgui_impl_vulkan.h`
- Ya引擎 Framebuffer: `Engine/Source/Render/Core/FrameBuffer.h`
