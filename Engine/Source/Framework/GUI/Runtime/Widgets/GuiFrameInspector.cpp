#include "GUI/Widgets/GuiFrameInspector.h"

#include "Core/Event.h"
#include "Core/KeyCode.h"
#include "Core/Log.h"

#include <algorithm>
#include <cctype>

namespace ya
{

void FGuiFrameInspectorRecord::resetForFrame()
{
    paintDirtyDelta        = 0;
    layoutDirtyDelta       = 0;
    arrangeDirtyDelta      = 0;
    bRebuildRectsOverflow  = false;
    rebuiltRects.clear();
    modelScreenFlush  = 0;
    gpuScreenFlush    = 0;
    gpuWorldFlush     = 0;
    gpuScreenVertices = 0;
    gpuScreenIndices  = 0;
    meanCoverage      = 0.0f;
    maxCoverage       = 0.0f;
    overdrawFactor    = 0.0f;
}

void FGuiFrameInspectorRecord::recordRebuild(const UIElement& widget)
{
#if defined(YA_PROFILING_DISABLED)
    (void)widget;
#else
    if (!profiling::isGuiFrameInspectorEnabled()) {
        return;
    }
    if (rebuiltRects.size() >= kMaxRebuildRects) {
        bRebuildRectsOverflow = true;
        return;
    }
    FGuiRebuildRect entry;
    entry.layoutRect = widget.getLayoutRect();
    entry.reason     = widget.getLastInvalidationReason();
    entry.name       = widget._name;
    rebuiltRects.push_back(std::move(entry));
#endif
}

void FGuiFrameInspectorRecord::finishDirtyDeltas(uint64_t  paintNow,
                                                 uint64_t  layoutNow,
                                                 uint64_t  arrangeNow,
                                                 uint64_t& paintPrev,
                                                 uint64_t& layoutPrev,
                                                 uint64_t& arrangePrev)
{
    paintDirtyDelta   = static_cast<uint32_t>(paintNow - paintPrev);
    layoutDirtyDelta  = static_cast<uint32_t>(layoutNow - layoutPrev);
    arrangeDirtyDelta = static_cast<uint32_t>(arrangeNow - arrangePrev);
    paintPrev         = paintNow;
    layoutPrev        = layoutNow;
    arrangePrev       = arrangeNow;
}

bool applyGuiFrameInspectorSpec(std::string_view spec)
{
    if (profiling::isCompiledOut()) {
        YA_CORE_WARN("GUI Frame Inspector compiled out (YA_PROFILING_DISABLED); ignoring '{}'", spec);
        return false;
    }

    uint8_t channels = 0;
    auto consumeToken = [&](std::string_view token) {
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front()))) {
            token.remove_prefix(1);
        }
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) {
            token.remove_suffix(1);
        }
        if (token.empty() || token == "1" || token == "true" || token == "on") {
            channels |= guiFrameInspectorChannelMask(EGuiFrameInspectorChannel::Hud);
            channels |= guiFrameInspectorChannelMask(EGuiFrameInspectorChannel::Rebuild);
            return;
        }
        if (token == "hud") {
            channels |= guiFrameInspectorChannelMask(EGuiFrameInspectorChannel::Hud);
            return;
        }
        if (token == "rebuild") {
            channels |= guiFrameInspectorChannelMask(EGuiFrameInspectorChannel::Rebuild);
            return;
        }
        if (token == "overdraw") {
            channels |= guiFrameInspectorChannelMask(EGuiFrameInspectorChannel::Overdraw);
            return;
        }
        YA_CORE_WARN("GUI Frame Inspector: unknown channel '{}'", token);
    };

    if (spec.empty()) {
        consumeToken("on");
    }
    else {
        size_t start = 0;
        while (start <= spec.size()) {
            const size_t comma = spec.find(',', start);
            const size_t end   = comma == std::string_view::npos ? spec.size() : comma;
            consumeToken(spec.substr(start, end - start));
            if (comma == std::string_view::npos) {
                break;
            }
            start = comma + 1;
        }
    }

    profiling::setGuiFrameInspectorChannels(channels);
    profiling::setGuiFrameInspectorEnabled(channels != 0);
    return channels != 0;
}

void toggleGuiFrameInspectorChannel(EGuiFrameInspectorChannel channel)
{
    if (profiling::isCompiledOut()) {
        YA_CORE_WARN("GUI Frame Inspector compiled out (YA_PROFILING_DISABLED)");
        return;
    }
    const uint8_t bit = guiFrameInspectorChannelMask(channel);
    uint8_t       channels = profiling::getGuiFrameInspectorChannels() ^ bit;
    profiling::setGuiFrameInspectorChannels(channels);
    profiling::setGuiFrameInspectorEnabled(channels != 0);
}

namespace
{

struct FHudPlacement
{
    glm::vec2 pos       = kGuiFrameInspectorHudDefaultPos;
    bool      bDragging = false;
    glm::vec2 grabOffset{0.0f, 0.0f};
};

FHudPlacement gHud;

[[nodiscard]] bool pointInRect(const glm::vec2& point, const Rect2D& rect)
{
    return point.x >= rect.pos.x && point.x <= rect.pos.x + rect.extent.x &&
           point.y >= rect.pos.y && point.y <= rect.pos.y + rect.extent.y;
}

void clampHudPos(Extent2D framebuffer)
{
    const float maxX = std::max(0.0f, static_cast<float>(framebuffer.width) - kGuiFrameInspectorHudSize.x);
    const float maxY = std::max(0.0f, static_cast<float>(framebuffer.height) - kGuiFrameInspectorHudSize.y);
    gHud.pos.x = std::clamp(gHud.pos.x, 0.0f, maxX);
    gHud.pos.y = std::clamp(gHud.pos.y, 0.0f, maxY);
}

} // namespace

glm::vec2 getGuiFrameInspectorHudPos()
{
    return gHud.pos;
}

void setGuiFrameInspectorHudPos(glm::vec2 pos)
{
    gHud.pos = pos;
}

void resetGuiFrameInspectorHudPlacement()
{
    gHud = {};
}

Rect2D guiFrameInspectorHudRect()
{
    return Rect2D{.pos = gHud.pos, .extent = kGuiFrameInspectorHudSize};
}

bool handleGuiFrameInspectorHudInput(const Event&     event,
                                     const glm::vec2& windowPoint,
                                     Extent2D         framebuffer,
                                     bool             bPopupOpen)
{
#if defined(YA_PROFILING_DISABLED)
    (void)event;
    (void)windowPoint;
    (void)framebuffer;
    (void)bPopupOpen;
    return false;
#else
    if (!isGuiFrameInspectorChannelOn(EGuiFrameInspectorChannel::Hud) && !gHud.bDragging) {
        return false;
    }

    const EEvent::T type = event.getEventType();
    if (type == EEvent::MouseButtonPressed) {
        const auto& press = static_cast<const MouseButtonPressedEvent&>(event);
        if (press.GetMouseButton() != EMouse::Left) {
            return false;
        }
        if (bPopupOpen) {
            return false;
        }
        if (!pointInRect(windowPoint, guiFrameInspectorHudRect())) {
            return false;
        }
        gHud.bDragging  = true;
        gHud.grabOffset = windowPoint - gHud.pos;
        return true;
    }
    if (type == EEvent::MouseMoved && gHud.bDragging) {
        gHud.pos = windowPoint - gHud.grabOffset;
        clampHudPos(framebuffer);
        return true;
    }
    if (type == EEvent::MouseButtonReleased && gHud.bDragging) {
        gHud.bDragging = false;
        clampHudPos(framebuffer);
        return true;
    }
    return false;
#endif
}

} // namespace ya
