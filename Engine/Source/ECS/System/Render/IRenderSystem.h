#pragma once

#include "Core/Base.h"
#include "Core/Debug/Instrumentor.h"
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
struct FrameContext;

struct IRenderSystem
{

    std::string  _label            = "IRenderSystem";
    bool         bReverseViewportY = true;
    bool         bEnabled          = true;
    ECullMode::T _cullMode         = ECullMode::Back;

    std::shared_ptr<IGraphicsPipeline> _pipeline;

    IRenderSystem(const std::string& label) : _label(label) {}
    virtual ~IRenderSystem() = default;

    virtual void onInit(IRenderPass* renderPass, const PipelineRenderingInfo& pipelineRenderingInfo) = 0;
    virtual void onRender(ICommandBuffer* cmdBuf, FrameContext* ctx)                                 = 0;
    virtual void onDestroy()                                                                         = 0;

    virtual void resetFrameSlot() {}
    virtual void beginFrame()
    {
        resetFrameSlot();
        if (_pipeline) {
            _pipeline->tryUpdateShader();
        }
    }

    void renderGUI()
    {
        bool bOpen = ImGui::TreeNode(_label.c_str());
        if (!bOpen) {
            return;
        }
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

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.7f, 0.0f, 1.0f));
        if (ImGui::Button("Reload Shaders")) {
            reloadShaders();
        }
        ImGui::PopStyleColor();

        onRenderGUI();
        ImGui::TreePop();
    }

    virtual void reloadShaders(std::optional<GraphicsPipelineCreateInfo> ci = {})
    {
        if (_pipeline) {
            _pipeline->reloadShaders(ci);
        }
    }

    App*     getApp() const;
    Scene*   getActiveScene() const;
    IRender* getRender() const;

    template <typename T>
    T* as()
    {
        static_assert(std::is_base_of_v<IRenderSystem, T>, "T must be derived from IRenderSystem");
        return static_cast<T*>(this);
    }

    const std::string& getLabel() const { return _label; }

  protected:
    virtual void onRenderGUI()
    {
        ImGui::Separator();
    }
};

} // namespace ya
