#pragma once

#include <cstdint>
#include <cctype>
#include <string>
#include <string_view>

namespace ya
{

/// Presentation + input host for the Game Editor shell. Chosen once at
/// startup; the two stacks cannot share a frame (full-window WidgetTree
/// replay and ImGui both own the swapchain overlay).
enum class EEditorChromeHost : uint8_t
{
    ImGui = 0,
    WidgetTree,
};

[[nodiscard]] inline const char* editorChromeHostName(EEditorChromeHost host)
{
    switch (host) {
    case EEditorChromeHost::WidgetTree:
        return "widgettree";
    case EEditorChromeHost::ImGui:
    default:
        return "imgui";
    }
}

[[nodiscard]] inline bool tryParseEditorChromeHost(std::string_view text, EEditorChromeHost& out)
{
    std::string normalized(text);
    for (char& ch : normalized) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (normalized == "imgui") {
        out = EEditorChromeHost::ImGui;
        return true;
    }
    if (normalized == "widgettree" || normalized == "widget-tree" || normalized == "ya-gui") {
        out = EEditorChromeHost::WidgetTree;
        return true;
    }
    return false;
}

} // namespace ya
