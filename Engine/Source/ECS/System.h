
#pragma once

#include "Platform/Render/Vulkan/VulkanRenderPass.h"

namespace ya
{


struct RenderTarget;

struct ISystem
{
    virtual void onUpdate(float deltaTime) = 0;


    virtual ~ISystem() = default;
};



struct IMaterialSystem : public ISystem
{
    // TODO: abstract render api
    virtual void onInit(VulkanRenderPass *renderPass)     = 0;
    virtual void onRender(void *cmdBuf, RenderTarget *rt) = 0;
    virtual void onDestroy()                              = 0;
    virtual void onRenderGUI()                            = 0;
};

} // namespace ya