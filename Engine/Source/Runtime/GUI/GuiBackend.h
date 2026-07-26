#pragma once

#include "Core/Event.h"
#include "Render/RenderDefines.h"

namespace ya
{

struct IRender;
struct IRenderPass;
struct ICommandBuffer;
struct Sampler;

using GuiTextureHandle = void*;

struct FGuiInputClaim
{
    bool pointer   = false;
    bool keyboard  = false;
    bool text      = false;
    bool exclusive = false;

    [[nodiscard]] bool wantsEvent(const Event& event) const
    {
        if (event.isInCategory(EEventCategory::Mouse) ||
            event.isInCategory(EEventCategory::MouseButton)) {
            return pointer;
        }
        if (event.isInCategory(EEventCategory::Keyboard)) {
            return keyboard || text;
        }
        return false;
    }
};

struct IGuiBackend
{
    virtual ~IGuiBackend() = default;

    [[nodiscard]] virtual const char* getName() const = 0;

    virtual void init(IRender* render, IRenderPass* renderPass) = 0;
    virtual void shutdown()                                     = 0;
    virtual void beginFrame()                                   = 0;
    virtual void endFrame()                                     = 0;
    [[nodiscard]] virtual bool render()                         = 0;
    virtual void submit(ICommandBuffer& commandBuffer)          = 0;

    [[nodiscard]] virtual EventProcessState processEvent(const Event& event)            = 0;
    [[nodiscard]] virtual FGuiInputClaim    describeInputClaim(const Event& event) const = 0;

    virtual void setViewportRect(float x, float y, float width, float height) = 0;

    [[nodiscard]] virtual GuiTextureHandle addTexture(IImageView* imageView,
                                                      Sampler* sampler,
                                                      EImageLayout::T layout = EImageLayout::ShaderReadOnlyOptimal) = 0;
    virtual void removeTexture(GuiTextureHandle textureHandle) = 0;

    [[nodiscard]] virtual bool renderBackendSettings() = 0;
};

} // namespace ya
