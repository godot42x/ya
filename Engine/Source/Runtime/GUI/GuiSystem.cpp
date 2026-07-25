#include "Runtime/GUI/GuiSystem.h"

#include "Runtime/GUI/ImGui/ImGuiSystem.h"

#include "Render/Core/CommandBuffer.h"

namespace ya
{

namespace
{

class ImGuiBackendAdapter final : public IGuiBackend
{
  public:
    [[nodiscard]] const char* getName() const override { return "ImGui"; }

    void init(IRender* render, IRenderPass* renderPass) override
    {
        ImGuiManager::get().init(render, renderPass);
    }

    void shutdown() override
    {
        ImGuiManager::get().shutdown();
    }

    void beginFrame() override
    {
        ImGuiManager::get().beginFrame();
    }

    void endFrame() override
    {
        ImGuiManager::get().endFrame();
    }

    [[nodiscard]] bool render() override
    {
        return ImGuiManager::get().render();
    }

    void submit(ICommandBuffer& commandBuffer) override
    {
        ImGuiManager::get().submitVulkan(commandBuffer.getHandleAs<VkCommandBuffer>());
    }

    [[nodiscard]] EventProcessState processEvent(const Event& event) override
    {
        return ImGuiManager::get().processEvent(event);
    }

    [[nodiscard]] FGuiInputClaim describeInputClaim(const Event& event) const override
    {
        return ImGuiManager::get().describeInputClaim(event);
    }

    [[nodiscard]] bool wantsInput() const override
    {
        return ImGuiManager::get().isWantInput();
    }

    void setBlockEvents(bool block) override
    {
        (void)block;
    }

    void setViewportRect(float x, float y, float width, float height) override
    {
        ImGuiManager::get().setGizmoRect(x, y, width, height);
    }

    [[nodiscard]] GuiTextureHandle addTexture(IImageView* imageView,
                                              Sampler* sampler,
                                              EImageLayout::T layout) override
    {
        return ImGuiManager::addTexture(imageView, sampler, layout);
    }

    void removeTexture(GuiTextureHandle textureHandle) override
    {
        ImGuiManager::removeTexture(textureHandle);
    }

    [[nodiscard]] bool renderBackendSettings() override
    {
        return ImGuiManager::get().onRenderGUI();
    }
};

} // namespace

GuiSystem& GuiSystem::get()
{
    static GuiSystem instance;
    return instance;
}

void GuiSystem::installBackend(std::unique_ptr<IGuiBackend> backend)
{
    _backend = std::move(backend);
}

bool GuiSystem::hasBackend() const
{
    return static_cast<bool>(_backend);
}

const char* GuiSystem::getBackendName() const
{
    return _backend ? _backend->getName() : "None";
}

IGuiBackend* GuiSystem::getBackend() const
{
    return _backend.get();
}

void GuiSystem::init(IRender* render, IRenderPass* renderPass)
{
    ensureDefaultBackend();
    requireBackend().init(render, renderPass);
}

void GuiSystem::shutdown()
{
    if (_backend) {
        _backend->shutdown();
    }
}

void GuiSystem::beginFrame()
{
    requireBackend().beginFrame();
}

void GuiSystem::endFrame()
{
    requireBackend().endFrame();
}

bool GuiSystem::render()
{
    return requireBackend().render();
}

void GuiSystem::submit(ICommandBuffer& commandBuffer)
{
    requireBackend().submit(commandBuffer);
}

EventProcessState GuiSystem::processEvent(const Event& event)
{
    return requireBackend().processEvent(event);
}

FGuiInputClaim GuiSystem::describeInputClaim(const Event& event) const
{
    return _backend ? _backend->describeInputClaim(event) : FGuiInputClaim{};
}

bool GuiSystem::wantsInput() const
{
    return _backend && _backend->wantsInput();
}

void GuiSystem::setBlockEvents(bool block)
{
    requireBackend().setBlockEvents(block);
}

void GuiSystem::setViewportRect(float x, float y, float width, float height)
{
    requireBackend().setViewportRect(x, y, width, height);
}

GuiTextureHandle GuiSystem::addTexture(IImageView* imageView, Sampler* sampler, EImageLayout::T layout)
{
    return requireBackend().addTexture(imageView, sampler, layout);
}

void GuiSystem::removeTexture(GuiTextureHandle textureHandle)
{
    requireBackend().removeTexture(textureHandle);
}

bool GuiSystem::renderBackendSettings()
{
    return requireBackend().renderBackendSettings();
}

IGuiBackend& GuiSystem::requireBackend()
{
    ensureDefaultBackend();
    YA_CORE_ASSERT(_backend, "GUI backend is not installed");
    return *_backend;
}

void GuiSystem::ensureDefaultBackend()
{
    if (!_backend) {
        _backend = std::make_unique<ImGuiBackendAdapter>();
    }
}

} // namespace ya
