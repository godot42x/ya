#pragma once

#include "GUI/Tooling/Workbench/WorkbenchSurface.h"
#include "GUI/Widgets/UIFrameSnapshot.h"

#include <array>
#include <memory>

struct ImVec2;

namespace ya
{

struct EditorLayer;
struct RenderImage;
struct WidgetTree;
struct IImageView;
struct Sampler;

struct GUIWorkbenchPanel
{
  private:
    EditorLayer* _owner = nullptr;
    guiworkbench::FWorkbenchSurface _surface;
    std::unique_ptr<WidgetTree>     _tree;
    std::shared_ptr<RenderImage>    _displayImage;
    Extent2D                        _logicalExtent{};
    glm::vec2                       _lastPointerPoint = {-1.0f, -1.0f};
    bool                            _bFocused = false;

  public:
    explicit GUIWorkbenchPanel(EditorLayer* owner);

    void onImGuiRender();
    void setDisplayImage(std::shared_ptr<RenderImage> image) { _displayImage = std::move(image); }
    [[nodiscard]] bool      hasRenderableExtent() const { return _logicalExtent.width > 0 && _logicalExtent.height > 0; }
    [[nodiscard]] Extent2D  getLogicalExtent() const { return _logicalExtent; }
    [[nodiscard]] UIFrameSnapshot buildSnapshot();

  private:
    void ensureTree();
    void routePanelInput(const ImVec2& imageMin, const ImVec2& imageSize);
    void dispatchPointerEvent(const Event& event, const glm::vec2& logicalPoint);
    void dispatchKeyboardEvent(const Event& event);
    [[nodiscard]] static EKey::T mapImGuiKeyToEKey(int imguiKey);
    [[nodiscard]] static uint32_t currentKeyModMask();
};

} // namespace ya
