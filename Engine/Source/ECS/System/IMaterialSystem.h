#pragma once

#include "Core/FWD.h"
#include "ECS/System.h"

#include "glm/mat4x4.hpp"


struct VulkanRenderPass;
struct VulkanRender;

namespace ya
{
struct App;
struct RenderTarget;
struct Scene;

struct IMaterialSystem : public ISystem
{
    std::string _label = "IMaterialSystem";

    // No material and base material?
    // std::shared_ptr<Material> _baseMaterial;

    // TODO: abstract render api
    virtual void onInit(VulkanRenderPass *renderPass)     = 0;
    virtual void onRender(void *cmdBuf, RenderTarget *rt) = 0;
    void         onUpdate(float deltaTime) override {}
    virtual void onDestroy() = 0;
    virtual void onRenderGUI();
    virtual void onEndRenderGUI();


    App          *getApp() const;
    Scene        *getScene() const;
    VulkanRender *getVulkanRender() const;
};
} // namespace ya
