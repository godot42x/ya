#pragma once

// ============================================================================
// TestSurface - a presentation surface with identity and nothing else.
//
// Several policies are asked ABOUT a window ("does the host's content fill this
// window?", "which window's render targets does this catalog describe?"). Those
// questions need surface identity, not a device, a swapchain or a swapchain
// image -- and making the question take a surface is what stopped the answer from
// being "whichever window happens to be primary". This stand-in lets a test ask
// about a window without standing up a GPU device.
//
// Do not extend it into a fake renderer: anything that needs real sync or real
// images belongs in a test that builds a device (see RhiVulkan/).
// ============================================================================

#include "RHI/Core/RenderSurfaceContext.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace ya::test
{

struct StandInSurface final : IRenderSurfaceContext
{
    [[nodiscard]] INativeWindow* getNativeWindow() const override { return nullptr; }
    [[nodiscard]] ISwapchain*    getSwapchain() const override { return nullptr; }
    bool buildPresentationImages(IRenderResourceFactory&,
                                 const char*,
                                 std::vector<std::shared_ptr<RenderTexture>>&) override
    {
        return false;
    }
    [[nodiscard]] bool isPresentable() const override { return true; }
    void               requestRecreate() override {}
    bool               begin(int32_t* imageIndex) override
    {
        *imageIndex = -1;
        return true;
    }
    bool end(int32_t, std::vector<void*>) override { return true; }
    void waitInFlight() override {}
    [[nodiscard]] void* getCurrentImageAvailableSemaphore() override { return nullptr; }
    [[nodiscard]] void* getCurrentFrameFence() override { return nullptr; }
    [[nodiscard]] void* getRenderFinishedSemaphore(uint32_t) override { return nullptr; }
};

} // namespace ya::test
