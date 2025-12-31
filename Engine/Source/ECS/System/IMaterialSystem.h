#pragma once

#include "Core/FWD.h"
#include "Core/System/System.h"

#include "Render/Core/CommandBuffer.h"
#include "glm/mat4x4.hpp"
#include "imgui.h"


namespace ya
{

struct IRender;
struct IRenderPass;
struct VulkanRender;
struct App;
struct IRenderTarget;
struct Scene;

struct IMaterialSystem : public ISystem
{
    std::string  _label            = "IMaterialSystem";
    bool         bReverseViewportY = true;
    bool         bEnabled          = true;
    ECullMode::T _cullMode         = ECullMode::Back;

    // No material and base material?
    // std::shared_ptr<Material> _baseMaterial;

    // TODO: abstract render api
    virtual void onInit(IRenderPass *renderPass)                     = 0;
    virtual void onRender(ICommandBuffer *cmdBuf, IRenderTarget *rt) = 0;
    virtual void onUpdate(float deltaTime) override {}
    virtual void onDestroy() = 0;

    void renderGUI()
    {
        if (!ImGui::CollapsingHeader(_label.c_str())) {
            return;
        }
        ImGui::Indent();
        ImGui::PushID(_label.c_str());
        ImGui::Checkbox("Reverse Viewport Y", &bReverseViewportY);
        ImGui::Checkbox("Enabled", &bEnabled);
        int cull = (int)(_cullMode);
        if (ImGui::Combo("Cull Mode", &cull, "None\0Front\0Back\0FrontAndBack\0")) {
            switch (cull) {
            case 0:
                _cullMode = ECullMode::None;
                break;
            case 1:
                _cullMode = ECullMode::Front;
                break;
            case 2:
                _cullMode = ECullMode::Back;
                break;
            case 3:
                _cullMode = ECullMode::FrontAndBack;
                break;
            default:
                _cullMode = ECullMode::Back;
                break;
            }
        }

        onRenderGUI();
        ImGui::PopID();
        ImGui::Unindent();
    }
    virtual void onRenderGUI() {}


    App     *getApp() const;
    Scene   *getActiveScene() const;
    IRender *getRender() const;

    template <typename T>
    T *as()
    {
        static_assert(std::is_base_of_v<IMaterialSystem, T>, "T must be derived from IMaterialSystem");
        return static_cast<T *>(this);
    }
};
} // namespace ya