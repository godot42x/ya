#pragma once

#include "Core/Base.h"
#include "Core/Debug/Instrumentor.h"
#include "Core/System/System.h"

#include "Render/Core/CommandBuffer.h"
#include "glm/mat4x4.hpp"
#include "imgui.h"
#include <any>
#include <unordered_map>
#include <utility>


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
    struct InitParams
    {
        IRenderPass*          renderPass            = nullptr;
        PipelineRenderingInfo pipelineRenderingInfo = {};

        std::optional<ESampleCount::T> sampleCount = {};
        std::optional<ECullMode::T>    cullMode    = {};
        std::optional<EPolygonMode::T> polygonMode = {};
        std::optional<bool>            bEnable     = {};

        std::unordered_map<std::string, std::any> extras = {};

        template <typename T>
        void setExtra(std::string key, T&& value)
        {
            extras[std::move(key)] = std::forward<T>(value);
        }

        template <typename T>
        const T* getExtra(const std::string& key) const
        {
            auto it = extras.find(key);
            if (it == extras.end()) {
                return nullptr;
            }
            return std::any_cast<T>(&(it->second));
        }
    };

    std::string _label            = "IRenderSystem";
    bool        bReverseViewportY = true;
    bool        bEnabled          = true;
    InitParams  _initParams       = {};

    std::shared_ptr<IGraphicsPipeline> _pipeline;

    IRenderSystem(const std::string& label) : _label(label) {}
    virtual ~IRenderSystem() = default;

    void init(const InitParams& initParams)
    {
        _initParams = initParams;
        if (_initParams.bEnable.has_value()) {
            bEnabled = _initParams.bEnable.value();
        }
        // TODO: why  not move the pipeline desc to this layer?
        //      if the template in onInitImpl is conflicts with the init params
        //      from upper, hard to debug.
        //      And it's not good for design pattern.
        onInitImpl(initParams);
        applyCommonInitParams();
    }

    virtual void onDestroy() = 0;

    virtual void onRender(ICommandBuffer* cmdBuf, const FrameContext* ctx) {}

    virtual void resetFrameSlot() {}
    virtual void beginFrame()
    {
        if (bEnabled) {
            resetFrameSlot();
            if (_pipeline) {
                _pipeline->beginFrame();
            }
        }
    }
    void tick(ICommandBuffer* cmdBuf, float deltaTime, const FrameContext* ctx)
    {
        if (!bEnabled) {
            return;
        }
        onRender(cmdBuf, ctx);
    }



    void renderGUI();

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
    IGraphicsPipeline* getPipeline() const { return _pipeline.get(); }

  protected:
    virtual void onInitImpl(const InitParams& initParams) = 0;

    void applyCommonInitParams()
    {
        if (!_pipeline) {
            return;
        }

        if (_initParams.sampleCount.has_value()) {
            _pipeline->setSampleCount(*_initParams.sampleCount);
        }
        if (_initParams.cullMode.has_value()) {
            _pipeline->setCullMode(*_initParams.cullMode);
        }
        if (_initParams.polygonMode.has_value()) {
            _pipeline->setPolygonMode(*_initParams.polygonMode);
        }
    }

    virtual void onRenderGUI()
    {
        ImGui::Separator();
    }
};

} // namespace ya
