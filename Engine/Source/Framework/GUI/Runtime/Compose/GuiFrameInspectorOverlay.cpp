#include "GUI/Compose/GuiFrameInspectorOverlay.h"
#include "GUI/Compose/UIFrameComposeReplay.h"

#include "Core/Log.h"
#include "Core/Profiling/Profiling.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Render/Resources/FontManager.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

namespace ya
{
namespace
{

[[nodiscard]] bool inspectorChannelOn(EGuiFrameInspectorChannel channel)
{
    return isGuiFrameInspectorChannelOn(channel);
}

[[nodiscard]] Rect2D intersectRects(Rect2D a, const Rect2D& b)
{
    const glm::vec2 maxA = a.pos + a.extent;
    const glm::vec2 maxB = b.pos + b.extent;
    a.pos    = glm::max(a.pos, b.pos);
    a.extent = glm::max(glm::vec2(0.0f), glm::min(maxA, maxB) - a.pos);
    return a;
}

[[nodiscard]] Rect2D itemCoverageRect(const UIFrameDrawItem& item, const Extent2D& fb)
{
    Rect2D rect{.pos = item.pos, .extent = item.size};
    if (item.kind == UIFrameDrawItem::EKind::Line) {
        const glm::vec2 minP = glm::min(item.lineFrom, item.lineTo);
        const glm::vec2 maxP = glm::max(item.lineFrom, item.lineTo);
        rect.pos    = minP;
        rect.extent = glm::max(glm::vec2(0.0f), maxP - minP);
        rect.extent = glm::max(rect.extent, glm::vec2(item.lineThickness));
    }
    if (item.bClipped) {
        rect = intersectRects(rect, item.clip);
    }
    const Rect2D fbRect{.pos = {0.0f, 0.0f},
                        .extent = {static_cast<float>(fb.width), static_cast<float>(fb.height)}};
    return intersectRects(rect, fbRect);
}

void addFilled(Render2DList& list, const glm::vec2& pos, const glm::vec2& size, const glm::vec4& color)
{
    if (size.x <= 0.0f || size.y <= 0.0f) {
        return;
    }
    list.makeSprite(glm::vec3(pos, 0.0f), size, nullptr, color);
}

void addOutline(Render2DList& list, const Rect2D& rect, const glm::vec4& color)
{
    constexpr float t = 1.0f;
    if (rect.extent.x <= 0.0f || rect.extent.y <= 0.0f) {
        return;
    }
    addFilled(list, rect.pos, {rect.extent.x, t}, color);
    addFilled(list, {rect.pos.x, rect.pos.y + std::max(0.0f, rect.extent.y - t)}, {rect.extent.x, t}, color);
    addFilled(list, rect.pos, {t, rect.extent.y}, color);
    addFilled(list, {rect.pos.x + std::max(0.0f, rect.extent.x - t), rect.pos.y}, {t, rect.extent.y}, color);
}

[[nodiscard]] glm::vec4 overdrawColor(uint16_t coverage)
{
    if (coverage <= 1) {
        return {0.15f, 0.35f, 1.0f, 0.22f};
    }
    if (coverage == 2) {
        return {0.15f, 0.85f, 0.25f, 0.28f};
    }
    if (coverage == 3) {
        return {0.95f, 0.35f, 0.75f, 0.32f};
    }
    return {1.0f, 0.12f, 0.12f, 0.38f};
}

void stampOverdrawGrid(const UIFrameSnapshot& snapshot,
                       Extent2D               framebuffer,
                       uint32_t               gridWidth,
                       uint32_t               gridHeight,
                       std::vector<uint16_t>& grid,
                       float&                 overdrawFactor)
{
    grid.assign(static_cast<size_t>(gridWidth) * static_cast<size_t>(gridHeight), 0);
    overdrawFactor = 0.0f;
    const Extent2D fb = framebuffer.width == 0 || framebuffer.height == 0 ? snapshot.logicalExtent
                                                                          : framebuffer;
    if (fb.width == 0 || fb.height == 0 || gridWidth == 0 || gridHeight == 0) {
        return;
    }
    const float fbArea = static_cast<float>(fb.width) * static_cast<float>(fb.height);
    float       coveredArea = 0.0f;
    const float cellW = static_cast<float>(fb.width) / static_cast<float>(gridWidth);
    const float cellH = static_cast<float>(fb.height) / static_cast<float>(gridHeight);

    for (const UIFrameDrawItem& item : snapshot.items) {
        const Rect2D rect = itemCoverageRect(item, fb);
        coveredArea += rect.extent.x * rect.extent.y;
        if (rect.extent.x <= 0.0f || rect.extent.y <= 0.0f) {
            continue;
        }
        const int x0 = std::clamp(static_cast<int>(std::floor(rect.pos.x / cellW)), 0, static_cast<int>(gridWidth) - 1);
        const int y0 = std::clamp(static_cast<int>(std::floor(rect.pos.y / cellH)), 0, static_cast<int>(gridHeight) - 1);
        const int x1 = std::clamp(static_cast<int>(std::floor((rect.pos.x + rect.extent.x - 1.0f) / cellW)),
                                  0,
                                  static_cast<int>(gridWidth) - 1);
        const int y1 = std::clamp(static_cast<int>(std::floor((rect.pos.y + rect.extent.y - 1.0f) / cellH)),
                                  0,
                                  static_cast<int>(gridHeight) - 1);
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                uint16_t& cell = grid[static_cast<size_t>(y) * gridWidth + static_cast<size_t>(x)];
                if (cell < 0xFFFFu) {
                    ++cell;
                }
            }
        }
    }
    overdrawFactor = fbArea > 0.0f ? coveredArea / fbArea : 0.0f;
}

} // namespace

void captureGuiComposeInspector(FGuiFrameInspectorRecord& record,
                                const UIFrameSnapshot&    snapshot,
                                const FQuadRender::FRender2dFrameStats& gpuStats)
{
    const FUIFrameComposeReplayStats model = measureUIFrameComposeReplay(snapshot);
    record.modelScreenFlush = model.screenFlushCount;
#if defined(YA_PROFILING_DISABLED)
    (void)gpuStats;
#else
    record.gpuScreenFlush    = gpuStats.screenFlushCount;
    record.gpuScreenVertices = gpuStats.screenVertexCount;
    record.gpuScreenIndices  = gpuStats.screenIndexCount;
#endif
}

void captureGuiOverdrawInspector(FGuiFrameInspectorRecord& record,
                                 const UIFrameSnapshot&    snapshot,
                                 Extent2D                  framebuffer)
{
    std::vector<uint16_t> grid;
    stampOverdrawGrid(snapshot, framebuffer, 64, 64, grid, record.overdrawFactor);
    uint32_t maxCov  = 0;
    uint64_t sumCov  = 0;
    for (uint16_t cell : grid) {
        maxCov = std::max(maxCov, static_cast<uint32_t>(cell));
        sumCov += cell;
    }
    record.maxCoverage  = static_cast<float>(maxCov);
    record.meanCoverage = grid.empty() ? 0.0f : static_cast<float>(sumCov) / static_cast<float>(grid.size());
}

void emitGuiFrameInspectorOverlay(const FGuiFrameInspectorRecord& record,
                                  const UIFrameSnapshot&          snapshot,
                                  const GuiPerfStats&             perf,
                                  Render2DList&                   list,
                                  Extent2D                        framebuffer)
{
#if defined(YA_PROFILING_DISABLED)
    (void)record;
    (void)snapshot;
    (void)perf;
#else
    if (!profiling::isGuiFrameInspectorEnabled()) {
        return;
    }

    if (inspectorChannelOn(EGuiFrameInspectorChannel::Overdraw)) {
        std::vector<uint16_t> grid;
        float                 unusedFactor = 0.0f;
        constexpr uint32_t    kGrid = 64;
        const Extent2D fb = framebuffer;
        stampOverdrawGrid(snapshot, fb, kGrid, kGrid, grid, unusedFactor);
        const float cellW = static_cast<float>(fb.width) / static_cast<float>(kGrid);
        const float cellH = static_cast<float>(fb.height) / static_cast<float>(kGrid);
        for (uint32_t y = 0; y < kGrid; ++y) {
            for (uint32_t x = 0; x < kGrid; ++x) {
                const uint16_t coverage = grid[static_cast<size_t>(y) * kGrid + x];
                if (coverage == 0) {
                    continue;
                }
                addFilled(list, {static_cast<float>(x) * cellW, static_cast<float>(y) * cellH},
                          {cellW, cellH},
                          overdrawColor(coverage));
            }
        }
    }

    if (inspectorChannelOn(EGuiFrameInspectorChannel::Rebuild)) {
        static const glm::vec4 kColors[] = {
            {1.0f, 0.35f, 0.15f, 0.95f},
            {0.25f, 0.85f, 1.0f, 0.95f},
            {0.40f, 1.0f, 0.40f, 0.95f},
            {1.0f, 0.85f, 0.20f, 0.95f},
        };
        for (size_t i = 0; i < record.rebuiltRects.size(); ++i) {
            Rect2D px = record.rebuiltRects[i].layoutRect;
            px.pos    = record.targetOffset + px.pos * record.targetScale;
            px.extent = px.extent * record.targetScale;
            addOutline(list, px, kColors[i % 4]);
        }
    }

    if (inspectorChannelOn(EGuiFrameInspectorChannel::Hud)) {
        const Extent2D fb = framebuffer;
        Rect2D hud = guiFrameInspectorHudRect();
        hud.pos.x = std::clamp(hud.pos.x, 0.0f, std::max(0.0f, static_cast<float>(fb.width) - hud.extent.x));
        hud.pos.y = std::clamp(hud.pos.y, 0.0f, std::max(0.0f, static_cast<float>(fb.height) - hud.extent.y));
        setGuiFrameInspectorHudPos(hud.pos);
        addFilled(list, hud.pos, hud.extent, {0.05f, 0.06f, 0.08f, 0.72f});
        auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, 14);
        if (font) {
            const glm::vec4 color{0.95f, 0.96f, 0.90f, 1.0f};
            const std::string line0 = std::format("GUI {}/{} rebuilt  items {}  modelFlush {}  gpuFlush {}",
                                                  perf.rebuiltWidgets,
                                                  perf.paintedWidgets,
                                                  perf.drawItems,
                                                  record.modelScreenFlush,
                                                  record.gpuScreenFlush);
            const std::string line1 = std::format("layout {:.2f}ms  paint {:.2f}ms  dirtyP/L {}/{}",
                                                  perf.layoutMS,
                                                  perf.paintMS,
                                                  record.paintDirtyDelta,
                                                  record.layoutDirtyDelta);
            const std::string line2 = std::format("overdraw mean {:.2f} max {:.0f} factor {:.2f}",
                                                  record.meanCoverage,
                                                  record.maxCoverage,
                                                  record.overdrawFactor);
            list.makeText(line0, glm::vec3(hud.pos.x + 6.0f, hud.pos.y + 4.0f, 0.0f), color, font.get());
            list.makeText(line1, glm::vec3(hud.pos.x + 6.0f, hud.pos.y + 20.0f, 0.0f), color, font.get());
            list.makeText(line2, glm::vec3(hud.pos.x + 6.0f, hud.pos.y + 36.0f, 0.0f), color, font.get());
        }
    }
#endif
}

void runGuiFrameInspectorOverlay(WidgetTree& tree, const UIFrameSnapshot& snapshot, Render2DList& list, Extent2D framebuffer)
{
#if defined(YA_PROFILING_DISABLED)
    (void)tree;
    (void)snapshot;
    (void)list;
    (void)framebuffer;
#else
    if (!YA_GUI_INSPECTOR_IS_ENABLED()) {
        return;
    }
    FGuiFrameInspectorRecord& record = tree.getFrameInspectorRecord();
    // Product content is already in the list; the overlay appends after the
    // capture, so HUD GPU counts exclude overlay draws.
    captureGuiComposeInspector(record,
                               snapshot,
                               list.capturedStats());
    captureGuiOverdrawInspector(record, snapshot, framebuffer);
    emitGuiFrameInspectorOverlay(record, snapshot, tree.getPerfStats(), list, framebuffer);
#endif
}

} // namespace ya
