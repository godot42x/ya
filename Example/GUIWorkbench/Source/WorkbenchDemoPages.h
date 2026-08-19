#pragma once

// GUIWorkbench feature gallery pages (imgui-demo style). This is example
// content: it lives under Example/, never under Engine/Source/Framework —
// the framework provides the controls and the workbench shell, apps assemble
// their own pages. One builder per gallery section; each section has its own
// scenario file under Example/GUIWorkbench/Scenarios/.

#include <glm/glm.hpp>

#include <functional>
#include <memory>
#include <string>

namespace ya
{
struct UIButton;
struct UICheckBox;
struct UIComboBox;
struct UIElement;
struct UIMenuBar;
struct UISlider;
struct UITabBar;
struct UITreeView;
class  UIStyleSet;
struct WidgetTree;
} // namespace ya

namespace guiworkbench
{

/// Runtime state shared by the demo pages and the workbench surface
/// (survives page rebuilds). Pages read/write this; the surface owns it.
struct FDemoState
{
    // Render correctness page
    int    renderProbeClicks = 0;
    std::string renderLog;

    // Widgets page
    int    clickCount    = 0;
    bool   bCheckA       = true;
    bool   bCheckB       = false;
    bool   bCheckC       = false;
    float  sliderValue   = 0.4f;
    int    comboIndex    = 0;
    std::string textFieldValue;
    std::string widgetLog;

    // Menus page
    std::string menuLog;

    // Drag & drop page
    std::string dropLog;

    // Modal page
    bool        bModalOpen = false;
    std::string modalName  = "YA Engine";

    // Layout page
    float layoutSpacing = 8.0f;

    // Status line
    std::string statusText = "Ready";

    // Live widget handles for the smoke automation (page-built, valid while
    // the page is mounted).
    std::shared_ptr<ya::UIButton>    renderProbeButton;
    std::shared_ptr<ya::UIButton>    counterButton;
    std::shared_ptr<ya::UICheckBox>  checkA;
    std::shared_ptr<ya::UISlider>    slider;
    std::shared_ptr<ya::UIComboBox>  combo;
    std::shared_ptr<ya::UIMenuBar>   menuBar;
    std::shared_ptr<ya::UIElement>   dragItem;
    std::shared_ptr<ya::UIElement>   dropZone;
    std::shared_ptr<ya::UIButton>    openModalButton;

    /// Drop stale page handles (called by the app before rebuilding a page)
    /// so detached demo widgets can be destroyed.
    void resetHandles()
    {
        renderProbeButton.reset();
        counterButton.reset();
        checkA.reset();
        slider.reset();
        combo.reset();
        dragItem.reset();
        dropZone.reset();
        openModalButton.reset();
    }
};

/// One feature demo page, built into `parent` (a fresh content host panel).
/// `log` appends to the status line (single source for smoke assertions).
using FDemoPageBuilder = std::function<void(ya::WidgetTree& tree,
                                            ya::UIElement& parent,
                                            FDemoState& state,
                                            const std::function<void(const std::string&)>& log)>;

void buildRenderDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                     const std::function<void(const std::string&)>& log);
void buildWidgetsDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                      const std::function<void(const std::string&)>& log);
void buildLayoutDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                     const std::function<void(const std::string&)>& log);
void buildMenusDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                    const std::function<void(const std::string&)>& log);
void buildDragDropDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                       const std::function<void(const std::string&)>& log);
void buildModalDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                    const std::function<void(const std::string&)>& log);
void buildScrollSplitDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                          const std::function<void(const std::string&)>& log);

/// Feature gallery: one page that exercises the framework's reactive binding
/// layer, the style system, and the data-driven TreeView — three capabilities
/// that the other demo pages do not cover.
void buildGalleryDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                      const std::function<void(const std::string&)>& log);

/// Interaction-completion page (editor-parity P6): tooltip, wrapped text,
/// subtree disable and the modal dialog.
void buildInteractionsDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                           const std::function<void(const std::string&)>& log);

/// DockSpace page (editor-parity P7): three docking zones with draggable
/// tabs.
void buildDockDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                   const std::function<void(const std::string&)>& log);

} // namespace guiworkbench
