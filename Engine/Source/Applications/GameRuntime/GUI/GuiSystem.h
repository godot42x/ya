#pragma once

#include "GameRuntime/GUI/GuiBackend.h"

#include <memory>

namespace ya
{

class YA_GAME_RUNTIME_API GuiSystem
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

    [[nodiscard]] EventProcessState processEvent(const Event& event);
    [[nodiscard]] FGuiInputClaim    describeInputClaim(const Event& event) const;

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
