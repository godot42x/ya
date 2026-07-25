#pragma once

#include "Runtime/GUI/GuiBackend.h"

#include <memory>

namespace ya
{

class GuiSystem
{
  public:
    static GuiSystem& get();

    void installBackend(std::unique_ptr<IGuiBackend> backend);

    [[nodiscard]] bool              hasBackend() const;
    [[nodiscard]] const char*       getBackendName() const;
    [[nodiscard]] IGuiBackend*      getBackend() const;

    void init(IRender* render, IRenderPass* renderPass);
    void shutdown();
    void beginFrame();
    void endFrame();
    [[nodiscard]] bool render();
    void submit(ICommandBuffer& commandBuffer);

    [[nodiscard]] EventProcessState processNativeEvent(const SDL_Event& event);
    [[nodiscard]] EventProcessState processEvent(const Event& event);
    [[nodiscard]] bool              wantsInput() const;

    void setBlockEvents(bool block);
    void setViewportRect(float x, float y, float width, float height);

    [[nodiscard]] GuiTextureHandle addTexture(IImageView* imageView,
                                              Sampler* sampler,
                                              EImageLayout::T layout = EImageLayout::ShaderReadOnlyOptimal);
    void removeTexture(GuiTextureHandle textureHandle);

    [[nodiscard]] bool renderBackendSettings();

  private:
    GuiSystem() = default;

    [[nodiscard]] IGuiBackend& requireBackend();
    void ensureDefaultBackend();

  private:
    std::unique_ptr<IGuiBackend> _backend;
};

} // namespace ya
