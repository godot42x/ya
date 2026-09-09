#pragma once

#include "GUI/Widgets/Reactive.h"

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
struct UIDockFloatingHost;
struct WidgetTree;
} // namespace ya

namespace guiworkbench
{

struct FDemoState
{
    int         renderProbeClicks = 0;
    std::string renderLog;

    int         clickCount  = 0;
    bool        bCheckA     = true;
    bool        bCheckB     = false;
    bool        bCheckC     = false;
    float       sliderValue = 0.4f;
    int         comboIndex  = 0;
    std::string textFieldValue;
    std::string widgetLog;

    std::string menuLog;
    std::string dropLog;

    bool        bModalOpen = false;
    std::string modalName  = "YA Engine";

    float layoutSpacing = 8.0f;
    std::string statusText = "Ready";

    std::shared_ptr<ya::UIButton>   renderProbeButton;
    std::shared_ptr<ya::UIButton>   counterButton;
    std::shared_ptr<ya::UICheckBox> checkA;
    std::shared_ptr<ya::UISlider>   slider;
    std::shared_ptr<ya::UIComboBox> combo;
    std::shared_ptr<ya::UIMenuBar>  menuBar;
    std::shared_ptr<ya::UIElement>  dragItem;
    std::shared_ptr<ya::UIElement>  dropZone;
    std::shared_ptr<ya::UIButton>   openModalButton;
    std::shared_ptr<ya::UIDockFloatingHost> dockFloatingHost;

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

void buildRenderDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                     const std::function<void(const std::string&)>& log);
void buildWidgetsDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                      const std::function<void(const std::string&)>& log);
void buildInputsDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                     const std::function<void(const std::string&)>& log);
void buildLayoutDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                     const std::function<void(const std::string&)>& log);
void buildHostsDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                    const std::function<void(const std::string&)>& log);
void buildScrollSplitDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                          const std::function<void(const std::string&)>& log);
void buildBrushDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                    const std::function<void(const std::string&)>& log);
void buildTextDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                   const std::function<void(const std::string&)>& log);
void buildFontsDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                    const std::function<void(const std::string&)>& log);
void buildThemeDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                    const std::function<void(const std::string&)>& log,
                    const std::function<void(bool bDark)>& onToggleTheme);
void buildMenusDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                    const std::function<void(const std::string&)>& log);
void buildDialogDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                     const std::function<void(const std::string&)>& log);
void buildDragDropDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                       const std::function<void(const std::string&)>& log);
void buildEnableDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                     const std::function<void(const std::string&)>& log);
void buildBindingDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                      const std::function<void(const std::string&)>& log);
void buildTreeDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                   const std::function<void(const std::string&)>& log);
void buildTableDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                    const std::function<void(const std::string&)>& log);
void buildDockDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                   const std::function<void(const std::string&)>& log);
void buildWindowsDemo(ya::WidgetTree& tree,
                      ya::UIElement& parent,
                      const std::function<void(const std::string&)>& log,
                      const std::function<void()>& onOpen,
                      const std::function<void()>& onClose,
                      const std::shared_ptr<ya::Reactive<std::string>>& extraCountLabel);

} // namespace guiworkbench
