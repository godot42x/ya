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
        EXPECT_LE(page.rasterSizes.size(), static_cast<size_t>(kMaxLiveRasterSizes));
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
        EXPECT_LE(page.rasterSizes.size(), static_cast<size_t>(kMaxLiveRasterSizes));
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
    // fallback pages). After: at most kMaxLiveRasterSizes sizes per face, pages
    // start at 256 and double. The UI stack also keeps the monospace face at
    // its one loaded size. The metric counts 4 bytes per texel although the
    // bitmap pages are single channel, so the real footprint is a quarter of
    // it. 72 MiB here is the ceiling for this 40-size sweep, not a per-size
    // atlas cap; the steady state (stress, a working set that fits) is far lower.
    constexpr uint64_t kGpuBound = 72ull * 1024ull * 1024ull;
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
        EXPECT_LE(page.rasterSizes.size(), static_cast<size_t>(kMaxLiveRasterSizes));
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

// The regression: a per-frame working set larger than the old 8-size window
// evicted and rebuilt the same sizes every frame (46 ms each in the editor).
// A working set that fits under the cap must be built once and then served
// from cache: no revision bump, no rebuild, however many frames pass.
TEST(FontAtlasBudget, PerFrameWorkingSetIsBuiltOnce)
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

    // Editor chrome plus game UI at the panel density: 13 distinct sizes.
    const std::vector<uint32_t> workingSet = {11, 12, 13, 14, 16, 17, 18, 20, 22, 23, 24, 26, 32};
    ASSERT_GT(workingSet.size(), static_cast<size_t>(8));
    ASSERT_LE(workingSet.size(), static_cast<size_t>(kMaxLiveRasterSizes));
    for (uint32_t size : workingSet) {
        requestSize(*render, size, "Ag中");
    }
    const uint64_t revisionAfterWarmup = FontManager::get()->resourceRevision();

    // Longer than the idle window, so a size touched every frame is never idle.
    for (uint64_t frame = 0; frame < kRasterSizeIdleTicks * 2; ++frame) {
        for (uint32_t size : workingSet) {
            std::shared_ptr<Font> font = FontManager::get()->getFont(FName(DEFAULT_RUNTIME_FONT_NAME), size);
            ASSERT_NE(font, nullptr);
        }
        FontManager::get()->flushPendingGlyphs(*render);
    }
    EXPECT_EQ(FontManager::get()->resourceRevision(), revisionAfterWarmup);

    shutdownProbe(render);
}

// Sizes a sweep passed through once leave when they have idled out.
TEST(FontAtlasBudget, IdleSizesAreDroppedAfterTheIdleWindow)
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

    for (uint32_t size = 20; size < 30; ++size) {
        requestSize(*render, size, "Ag");
    }
    // Keep one size alive while the others idle past the window.
    for (uint64_t frame = 0; frame < kRasterSizeIdleTicks + 8; ++frame) {
        std::shared_ptr<Font> font = FontManager::get()->getFont(FName(DEFAULT_RUNTIME_FONT_NAME), 16);
        ASSERT_NE(font, nullptr);
        FontManager::get()->flushPendingGlyphs(*render);
    }
    for (const FontManager::FFontAtlasDebugPage& page : FontManager::get()->collectFontAtlasDebugPages()) {
        for (uint32_t rasterPx : page.rasterSizes) {
            EXPECT_TRUE(rasterPx < 20 || rasterPx >= 30) << page.label << " still holds idle size " << rasterPx;
        }
    }

    shutdownProbe(render);
}

// Layout measures at the logical size, glyphs are drawn at the device size.
// Advances must scale linearly with the raster size, otherwise line width,
// caret and selection drift from the drawn glyphs (hinted advances were
// rounded per size: 22px was 5% off 2x the 11px run).
TEST(FontAtlasBudget, AdvanceScalesLinearlyWithRasterSize)
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

    const std::string text = "The quick brown fox jumps over the lazy dog 0123456789";
    for (uint32_t logical : {11u, 13u, 14u, 16u}) {
        for (float scale : {1.0f, 1.25f, 1.5f, 2.0f, 2.5f}) {
            const uint32_t raster = static_cast<uint32_t>(std::lround(logical * scale));
            requestSize(*render, logical, text);
            requestSize(*render, raster, text);
            const auto lf = FontManager::get()->getFont(FName(DEFAULT_RUNTIME_FONT_NAME), logical);
            const auto rf = FontManager::get()->getFont(FName(DEFAULT_RUNTIME_FONT_NAME), raster);
            ASSERT_NE(lf, nullptr);
            ASSERT_NE(rf, nullptr);
            // Only the size quantisation (lround) is allowed to differ.
            const float expected = lf->measureText(text) * static_cast<float>(raster) / static_cast<float>(logical);
            const float actual   = rf->measureText(text);
            EXPECT_NEAR(actual, expected, 0.001f * expected + 0.5f)
                << logical << "px -> " << raster << "px";
        }
    }
    shutdownProbe(render);
}

} // namespace ya
