#pragma once

#include "Core/FWD.h"
#include "ECS/System.h"

#include "Render/Core/CommandBuffer.h"
#include "glm/mat4x4.hpp"


namespace ya
{

struct IRender;
struct IRenderPass;
struct VulkanRender;
struct App;
struct RenderTarget;
struct Scene;

struct IMaterialSystem : public ISystem
{
    std::string _label            = "IMaterialSystem";
    bool        bReverseViewportY = true;
    bool        bEnabled          = true;

    // No material and base material?
    // std::shared_ptr<Material> _baseMaterial;

    // TODO: abstract render api
    virtual void onInit(IRenderPass *renderPass)                    = 0;
    virtual void onRender(ICommandBuffer *cmdBuf, RenderTarget *rt) = 0;
    void         onUpdate(float deltaTime) override {}
    virtual void onDestroy() = 0;
    virtual void onRenderGUI();
    virtual void onEndRenderGUI();


    App          *getApp() const;
    Scene        *getScene() const;
    IRender      *getRender() const;
    VulkanRender *getVulkanRender() const; // Deprecated: use getRender() instead
};
} // namespace ya