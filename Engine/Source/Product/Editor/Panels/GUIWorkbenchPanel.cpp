#include "Editor/Panels/GUIWorkbenchPanel.h"

#include "Editor/EditorLayer.h"
#include "Core/KeyCode.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Host/GUI/ImGui/ImGuiSystem.h"
#include "RHI/Backend/TextureLibrary.h"
#include "RHI/Core/RenderImage.h"

#include <imgui.h>

namespace ya
{

namespace
{

constexpr ImGuiKey kForwardedKeys[] = {
    ImGuiKey_Tab,
    ImGuiKey_Enter,
    ImGuiKey_Space,
    ImGuiKey_UpArrow,
    ImGuiKey_DownArrow,
    ImGuiKey_LeftArrow,
    ImGuiKey_RightArrow,
    ImGuiKey_Home,
    ImGuiKey_End,
    ImGuiKey_Backspace,
    ImGuiKey_Delete,
};

} // namespace

GUIWorkbenchPanel::GUIWorkbenchPanel(EditorLayer* owner)
    : _owner(owner)
{
}

void GUIWorkbenchPanel::ensureTree()
{
    if (_tree) {
        _tree->setLogicalExtent(_logicalExtent);
        return;
    }

    _tree = std::make_unique<WidgetTree>(_logicalExtent);
    _surface.buildUI(*_tree);
}

UIFrameSnapshot GUIWorkbenchPanel::buildSnapshot()
{
    if (!hasRenderableExtent()) {
        return {};
    }
    ensureTree();
    _tree->setLogicalExtent(_logicalExtent);
    _surface.updateUI();
    return _tree->buildSnapshot(UIFrameBuildContext{});
}

void GUIWorkbenchPanel::onImGuiRender()
{
    if (!ImGui::Begin("GUI Workbench")) {
        ImGui::End();
        return;
    }

    const ImVec2 contentSize = ImGui::GetContentRegionAvail();
    _logicalExtent.width     = static_cast<uint32_t>(std::max(contentSize.x, 0.0f));
    _logicalExtent.height    = static_cast<uint32_t>(std::max(contentSize.y, 0.0f));

    if (_displayImage && _displayImage->isValid() && _displayImage->getImageView() && contentSize.x > 0.0f && contentSize.y > 0.0f) {
        ImGuiHelper::Image(_displayImage->getImageView(),
                           TextureLibrary::get().getLinearSampler(),
                           "GUIWorkbench",
                           contentSize,
                           ImVec2(0, 0),
                           ImVec2(1, 1));
        routePanelInput(ImGui::GetItemRectMin(), ImGui::GetItemRectSize());
    } else {
        ImGui::TextDisabled("GUI workbench surface waiting for first composed frame...");
    }

    ImGui::End();
}

void GUIWorkbenchPanel::routePanelInput(const ImVec2& imageMin, const ImVec2& imageSize)
{
    if (!_tree || imageSize.x <= 0.0f || imageSize.y <= 0.0f) {
        return;
    }

    ImGuiIO& io      = ImGui::GetIO();
    const bool bHover = ImGui::IsItemHovered();
    const bool bItemClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right);
    if (bItemClicked) {
        _bFocused = true;
    } else if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        _bFocused = false;
    }

    glm::vec2 logicalPoint = _lastPointerPoint;
    if (bHover) {
        logicalPoint = {
            std::clamp(io.MousePos.x - imageMin.x, 0.0f, imageSize.x),
            std::clamp(io.MousePos.y - imageMin.y, 0.0f, imageSize.y),
        };
        _lastPointerPoint = logicalPoint;
        dispatchPointerEvent(MouseMoveEvent(logicalPoint.x, logicalPoint.y), logicalPoint);
    }

    if (bHover && io.MouseWheel != 0.0f) {
        dispatchPointerEvent(MouseScrolledEvent(0.0f, io.MouseWheel), logicalPoint);
    }

    for (int button = 0; button < 3; ++button) {
        if (bHover && ImGui::IsMouseClicked(button)) {
            dispatchPointerEvent(MouseButtonPressedEvent(button), logicalPoint);
        }
        if ((_bFocused || bHover) && ImGui::IsMouseReleased(button)) {
            dispatchPointerEvent(MouseButtonReleasedEvent(button), logicalPoint);
        }
    }

    if (!_bFocused) {
        return;
    }

    for (ImGuiKey key : kForwardedKeys) {
        if (!ImGui::IsKeyPressed(key)) {
            continue;
        }
        KeyPressedEvent event{};
        event._keyCode = mapImGuiKeyToEKey(static_cast<int>(key));
        event._mod     = currentKeyModMask();
        if (event._keyCode != EKey::NONE) {
            dispatchKeyboardEvent(event);
        }
    }

    for (ImWchar ch : io.InputQueueCharacters) {
        if (ch == 0 || ch == '\t' || ch == '\r' || ch == '\n') {
            continue;
        }
        char utf8[5] = {};
        const int len = ImTextCharToUtf8(utf8, ch);
        if (len <= 0) {
            continue;
        }
        KeyTypedEvent event(std::string(utf8, utf8 + len));
        event._mod = currentKeyModMask();
        dispatchKeyboardEvent(event);
    }
}

void GUIWorkbenchPanel::dispatchPointerEvent(const Event& event, const glm::vec2& logicalPoint)
{
    WidgetEventContext ctx;
    ctx.logicalPoint = logicalPoint;
    _surface.onRoutedEvent(event, _tree->dispatchEvent(event, ctx));
}

void GUIWorkbenchPanel::dispatchKeyboardEvent(const Event& event)
{
    WidgetEventContext ctx;
    ctx.logicalPoint = _lastPointerPoint;
    _surface.onRoutedEvent(event, _tree->dispatchEvent(event, ctx));
}

EKey::T GUIWorkbenchPanel::mapImGuiKeyToEKey(int imguiKey)
{
    switch (imguiKey) {
    case ImGuiKey_Tab: return EKey::Tab;
    case ImGuiKey_Enter: return EKey::Enter;
    case ImGuiKey_Space: return EKey::Space;
    case ImGuiKey_UpArrow: return EKey::Up;
    case ImGuiKey_DownArrow: return EKey::Down;
    case ImGuiKey_LeftArrow: return EKey::Left;
    case ImGuiKey_RightArrow: return EKey::Right;
    case ImGuiKey_Home: return EKey::Home;
    case ImGuiKey_End: return EKey::End;
    case ImGuiKey_Backspace: return EKey::Backspace;
    case ImGuiKey_Delete: return EKey::Delete;
    default: return EKey::NONE;
    }
}

uint32_t GUIWorkbenchPanel::currentKeyModMask()
{
    uint32_t mod = 0;
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl) {
        mod |= EKeyMod::Ctrl;
    }
    if (io.KeyShift) {
        mod |= EKeyMod::Shift;
    }
    if (io.KeyAlt) {
        mod |= EKeyMod::Alt;
    }
#if defined(__APPLE__)
    if (io.KeySuper) {
        mod |= EKeyMod::LMeta;
    }
#endif
    return mod;
}

} // namespace ya
