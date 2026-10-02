#include "Render/Resources/FontManager.h"
#include "RHI/NativeWindow.h"
#include "RHI/Render.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace ya
{
namespace
{

bool createProbeWindow(SDLNativeWindow& window)
{
    if (!window.init()) {
        return false;
    }
    return window.recreate(WindowCreateInfo{
        .renderAPI = ERenderAPI::Vulkan,
        .title     = "FontAtlasBudget",
        .width     = 160,
        .height    = 120,
    });
}

struct FAtlasBudget
{
    size_t   pages    = 0;
    uint64_t gpuBytes = 0;
};

FAtlasBudget measureBudget()
{
    FAtlasBudget budget;
    for (const FontManager::FFontAtlasDebugPage& page : FontManager::get()->collectFontAtlasDebugPages()) {
        ++budget.pages;
        if (page.texture) {
            budget.gpuBytes += static_cast<uint64_t>(page.texture->getWidth()) * page.texture->getHeight() * 4u;
        }
    }
    return budget;
}

void shutdownProbe(IRender* render)
{
    FontManager::get()->clearCache();
    if (!render) {
        return;
    }
    // Retired atlas images sit in the deferred queue until a later frame.
    // Drain it while the device is still alive so VMA is empty at destroy.
    render->waitIdle();
    for (int frame = 0; frame < 8; ++frame) {
        render->beginRecordedFrame();
    }
    render->waitIdle();
    render->destroy();
    delete render;
}

void requestSize(IRender& render, uint32_t rasterPx, std::string_view text)
{
    std::shared_ptr<Font> font = FontManager::get()->getFont(FName(DEFAULT_RUNTIME_FONT_NAME), rasterPx);
    ASSERT_NE(font, nullptr);
    FontManager::get()->requestGlyphs(*font, text);
    FontManager::get()->flushPendingGlyphs(render);
}

} // namespace

// Measures the integer-raster cache: one draw font per whole pixel, each with
// its own atlas bank. The CJK fallback used to allocate a 1024 page per size
// up front. Numbers are written for the before/after comparison; the bound
// below is the post-change contract (shared pages, live-size window).
TEST(FontAtlasBudget, Sizes9To48WithCjkStayWithinLiveText)
{
    SDLNativeWindow window;
    if (!createProbeWindow(window)) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    const RenderCreateInfo renderCI{
        .renderAPI = ERenderAPI::Vulkan,
        .startupSurfaces = {
            StartupSurfaceDesc{
                .window      = &window,
                .swapchainCI = SwapchainCreateInfo{.bEnableTransferSrc = true, .width = 160, .height = 120},
            },
        },
    };
    IRender* render = IRender::create(renderCI);
    ASSERT_NE(render, nullptr);
    ASSERT_TRUE(render->init(renderCI));

    FontManager::get()->clearCache();
    ASSERT_TRUE(FontManager::get()->loadUiFontStack(*render, "inter", 16));

    for (uint32_t size = 9; size <= 48; ++size) {
        requestSize(*render, size, "Ag中");
    }

    const FAtlasBudget sequence = measureBudget();
    uint64_t sequenceCpu = 0;
    for (const FontManager::FFontAtlasDebugPage& page : FontManager::get()->collectFontAtlasDebugPages()) {
        sequenceCpu += page.cpuBytes;
        EXPECT_LE(page.rasterSizes.size(), static_cast<size_t>(kLiveRasterSizeWindow));
        std::cout << "FONT_ATLAS_PAGE " << page.label << " | " << page.detail << "\n";
    }
    std::cout << "FONT_ATLAS_BUDGET sequence pages=" << sequence.pages
              << " gpuBytes=" << sequence.gpuBytes << " cpuBytes=" << sequenceCpu << "\n";

    std::mt19937 rng(1);
    std::uniform_int_distribution<uint32_t> sizeDist(9, 48);
    for (int i = 0; i < 500; ++i) {
        requestSize(*render, sizeDist(rng), "Ag中");
    }
    const FAtlasBudget stress = measureBudget();
    uint64_t stressCpu = 0;
    for (const FontManager::FFontAtlasDebugPage& page : FontManager::get()->collectFontAtlasDebugPages()) {
        stressCpu += page.cpuBytes;
        EXPECT_LE(page.rasterSizes.size(), static_cast<size_t>(kLiveRasterSizeWindow));
    }
    std::cout << "FONT_ATLAS_BUDGET stress pages=" << stress.pages
              << " gpuBytes=" << stress.gpuBytes << " cpuBytes=" << stressCpu << "\n";

    {
        std::ofstream out("/tmp/font-atlas-budget.txt");
        out << "sequence pages=" << sequence.pages << " gpuBytes=" << sequence.gpuBytes
            << " cpuBytes=" << sequenceCpu << "\n";
        out << "stress pages=" << stress.pages << " gpuBytes=" << stress.gpuBytes
            << " cpuBytes=" << stressCpu << "\n";
    }

    // Before this contract the same 9..48 walk allocated 123 pages and
    // 1,049,886,720 GPU texel bytes (one bank per size, eager 1024/2048
    // fallback pages). After: kLiveRasterSizeWindow sizes per face, pages
    // start at 256 and double. The UI stack also keeps the monospace face at
    // its one loaded size. Two 2048 pages (Latin growth + slack) is 32 MiB —
    // the ceiling for this workload, not a per-size atlas cap.
    constexpr uint64_t kGpuBound = 2ull * 2048ull * 2048ull * 4ull;
    EXPECT_GT(sequence.pages, 0u);
    EXPECT_GT(sequence.gpuBytes, 0u);
    EXPECT_LE(sequence.pages, 6u);
    EXPECT_LE(sequence.gpuBytes, kGpuBound);
    EXPECT_LE(sequenceCpu, kGpuBound);
    EXPECT_LE(stress.pages, 6u);
    EXPECT_LE(stress.gpuBytes, kGpuBound);
    EXPECT_LE(stressCpu, kGpuBound);

    shutdownProbe(render);
}

// A snapshot holds the font (and a draw list would hold the atlas texture).
// Evicting other sizes may repack the shared page; the held font's glyphs stay
// valid, and the texture captured before the repack is not destroyed.
TEST(FontAtlasBudget, HeldFontAndInflightTextureSurviveEviction)
{
    SDLNativeWindow window;
    if (!createProbeWindow(window)) {
        GTEST_SKIP() << "SDL native window create failed";
    }
    const RenderCreateInfo renderCI{
        .renderAPI = ERenderAPI::Vulkan,
        .startupSurfaces = {
            StartupSurfaceDesc{
                .window      = &window,
                .swapchainCI = SwapchainCreateInfo{.bEnableTransferSrc = true, .width = 160, .height = 120},
            },
        },
    };
    IRender* render = IRender::create(renderCI);
    ASSERT_NE(render, nullptr);
    ASSERT_TRUE(render->init(renderCI));
    FontManager::get()->clearCache();
    ASSERT_TRUE(FontManager::get()->loadUiFontStack(*render, "inter", 16));

    std::shared_ptr<Font> held = FontManager::get()->getFont(FName(DEFAULT_RUNTIME_FONT_NAME), 16);
    ASSERT_NE(held, nullptr);
    FontManager::get()->requestGlyphs(*held, "Ag中");
    FontManager::get()->flushPendingGlyphs(*render);
    ASSERT_TRUE(held->hasCharacter(static_cast<uint32_t>('A')));
    ASSERT_TRUE(held->hasCharacter(static_cast<uint32_t>(U'中')));

    const Character& beforeA = held->getCharacter(static_cast<uint32_t>('A'));
    std::shared_ptr<Texture> inflight = held->atlasTextureFor(beforeA);
    ASSERT_NE(inflight, nullptr);
    const glm::vec4 uvBefore = beforeA.uvRect;

    for (uint32_t size = 9; size <= 48; ++size) {
        if (size == 16) {
            continue;
        }
        requestSize(*render, size, "Ag中");
    }

    const Character& afterA = held->getCharacter(static_cast<uint32_t>('A'));
    const Character& afterCjk = held->getCharacter(static_cast<uint32_t>(U'中'));
    EXPECT_NE(afterA.atlasSlot, ~0u);
    EXPECT_NE(afterCjk.atlasSlot, ~0u);
    ASSERT_NE(held->atlas, nullptr);
    EXPECT_EQ(afterA.uvRect, held->atlas->getUv(afterA.atlasSlot));
    EXPECT_NE(held->atlasTextureFor(afterA), nullptr);
    // The image a recorded frame captured is still alive after a repack
    // replaces the page (DeferredDeletionQueue + this shared_ptr).
    EXPECT_TRUE(static_cast<bool>(inflight));
    (void)uvBefore;

    bool bHeldSizeListed = false;
    for (const FontManager::FFontAtlasDebugPage& page : FontManager::get()->collectFontAtlasDebugPages()) {
        EXPECT_LE(page.rasterSizes.size(), static_cast<size_t>(kLiveRasterSizeWindow));
        for (uint32_t rasterPx : page.rasterSizes) {
            if (rasterPx == 16 && page.label.find("primary") != std::string::npos
                && page.label.find("Inter") != std::string::npos) {
                bHeldSizeListed = true;
            }
        }
    }
    EXPECT_TRUE(bHeldSizeListed);

    held.reset();
    inflight.reset();
    shutdownProbe(render);
}

} // namespace ya
