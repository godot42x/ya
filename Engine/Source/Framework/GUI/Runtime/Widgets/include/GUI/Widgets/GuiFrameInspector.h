#pragma once

#include "Core/Api.h"
#include "Core/Common/Types.h"
#include "Core/Profiling/Profiling.h"
#include "GUI/Widgets/UIElement.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ya
{

/// Opt-in overlay / collection channels. Independent bits; master enable is
/// `profiling::isGuiFrameInspectorEnabled()`.
enum class EGuiFrameInspectorChannel : uint8_t
{
    None     = 0,
    Hud      = 1 << 0,
    Rebuild  = 1 << 1,
    Overdraw = 1 << 2,
};

[[nodiscard]] constexpr uint8_t guiFrameInspectorChannelMask(EGuiFrameInspectorChannel channel)
{
    return static_cast<uint8_t>(channel);
}

struct FGuiRebuildRect
{
    Rect2D                layoutRect{};
    EUIInvalidationReason reason = EUIInvalidationReason::None;
    std::string           name;
};

/// Per-snapshot CPU inspector packet. Heavy fields stay empty unless the
/// profiling runtime toggle is on. GPU compose stats are filled by the host
/// from the live Render2D session after product replay and before overlay
/// emit, so HUD counts exclude inspector draws.
struct YA_GUI_API FGuiFrameInspectorRecord
{
    static constexpr uint32_t kMaxRebuildRects = 128;

    glm::vec2 targetScale{1.0f, 1.0f};
    glm::vec2 targetOffset{0.0f, 0.0f};

    uint32_t paintDirtyDelta   = 0;
    uint32_t layoutDirtyDelta  = 0;
    uint32_t arrangeDirtyDelta = 0;

    bool     bRebuildRectsOverflow = false;
    std::vector<FGuiRebuildRect> rebuiltRects;

    uint32_t modelScreenFlush   = 0;
    uint32_t gpuScreenFlush     = 0;
    uint32_t gpuScreenVertices  = 0;
    uint32_t gpuScreenIndices   = 0;

    float meanCoverage   = 0.0f;
    float maxCoverage    = 0.0f;
    float overdrawFactor = 0.0f;

    void resetForFrame();
    void recordRebuild(const UIElement& widget);
    void finishDirtyDeltas(uint64_t paintNow,
                           uint64_t layoutNow,
                           uint64_t arrangeNow,
                           uint64_t& paintPrev,
                           uint64_t& layoutPrev,
                           uint64_t& arrangePrev);
};

[[nodiscard]] inline bool isGuiFrameInspectorChannelOn(EGuiFrameInspectorChannel channel)
{
    return profiling::isGuiFrameInspectorEnabled() &&
           (profiling::getGuiFrameInspectorChannels() & guiFrameInspectorChannelMask(channel)) != 0;
}

[[nodiscard]] YA_GUI_API bool applyGuiFrameInspectorSpec(std::string_view spec);
YA_GUI_API void toggleGuiFrameInspectorChannel(EGuiFrameInspectorChannel channel);

inline constexpr glm::vec2 kGuiFrameInspectorHudSize{430.0f, 62.0f};
/// Below typical chrome (Workbench menu 30px, Editor menu+toolbar 50px).
inline constexpr glm::vec2 kGuiFrameInspectorHudDefaultPos{8.0f, 52.0f};

[[nodiscard]] YA_GUI_API glm::vec2 getGuiFrameInspectorHudPos();
YA_GUI_API void                    setGuiFrameInspectorHudPos(glm::vec2 pos);
YA_GUI_API void                    resetGuiFrameInspectorHudPlacement();
[[nodiscard]] YA_GUI_API Rect2D    guiFrameInspectorHudRect();

class Event;
/// Overlay HUD is not a widget. Hosts call this before tree dispatch so the
/// panel can be dragged. Returns true when the event was consumed.
YA_GUI_API bool handleGuiFrameInspectorHudInput(const Event&     event,
                                                const glm::vec2& windowPoint,
                                                Extent2D         framebuffer,
                                                bool             bPopupOpen = false);

} // namespace ya

#if defined(YA_PROFILING_DISABLED)

    #define YA_GUI_INSPECTOR_IS_ENABLED() (false)
    #define YA_GUI_INSPECTOR_RECORD_REBUILD(sink, widget) ((void)0)

#elif defined(YA_PROFILING_CONDITIONAL) || defined(YA_PROFILING_ENABLED)

    #define YA_GUI_INSPECTOR_IS_ENABLED() (::ya::profiling::isGuiFrameInspectorEnabled())
    #define YA_GUI_INSPECTOR_RECORD_REBUILD(sink, widget)                                 \
        do {                                                                              \
            if ((sink) != nullptr && YA_GUI_INSPECTOR_IS_ENABLED() && (widget) != nullptr) { \
                (sink)->recordRebuild(*(widget));                                         \
            }                                                                             \
        } while (0)

#else

    #define YA_GUI_INSPECTOR_IS_ENABLED() (false)
    #define YA_GUI_INSPECTOR_RECORD_REBUILD(sink, widget) ((void)0)

#endif
