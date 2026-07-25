#pragma once

#include "Core/Event.h"
#include "Render/RenderDefines.h"

#include <SDL3/SDL.h>

namespace ya
{

struct IRender;
struct IRenderPass;
struct ICommandBuffer;
struct Sampler;

using GuiTextureHandle = void*;

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

    [[nodiscard]] virtual EventProcessState processNativeEvent(const SDL_Event& event) = 0;
    [[nodiscard]] virtual EventProcessState processEvent(const Event& event)            = 0;
    [[nodiscard]] virtual bool              wantsInput() const                          = 0;

    virtual void setBlockEvents(bool block)                             = 0;
    virtual void setViewportRect(float x, float y, float width, float height) = 0;

    [[nodiscard]] virtual GuiTextureHandle addTexture(IImageView* imageView,
                                                      Sampler* sampler,
                                                      EImageLayout::T layout = EImageLayout::ShaderReadOnlyOptimal) = 0;
    virtual void removeTexture(GuiTextureHandle textureHandle) = 0;

    [[nodiscard]] virtual bool renderBackendSettings() = 0;
};

} // namespace ya
