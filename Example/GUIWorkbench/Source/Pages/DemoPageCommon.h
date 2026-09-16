#pragma once

#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/DefaultChromeTheme.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/DragDrop.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/UIFrameSnapshot.h"

#include <functional>
#include <memory>
#include <string>

namespace ya
{
struct WidgetTree;
} // namespace ya

namespace guiworkbench
{

// Demo-page palette aliases. These used to be literals restated here, which is
// how the gallery drifted from the chrome when the theme moved (two copies of
// "panel colour"). They now read the shared role palette, so a gallery caption
// and the chrome it sits on can never disagree.
constexpr glm::vec4 kPanelColor  = ya::gui_chrome::tokens::kPanelColor;
constexpr glm::vec4 kHeaderColor = ya::gui_chrome::tokens::kHeaderColor;
constexpr glm::vec4 kTextColor   = ya::gui_chrome::tokens::kTextColor;

std::shared_ptr<ya::UIText> makeLabel(const std::string& text, float fontSize = 13.0f);
std::shared_ptr<ya::UIText> makeBodyText(const std::string& text);
std::shared_ptr<ya::UIButton> makeDemoButton(const std::string& name, const std::string& label, float width = 0.0f);
std::shared_ptr<ya::UIContainer> makeRow(ya::WidgetTree& tree, ya::UIElement& parent, float spacing = 8.0f);

std::shared_ptr<ya::UIDragDropTile> makeDemoDragSource(std::string name,
                                                       std::string label,
                                                       std::string payload);
std::shared_ptr<ya::UIDragDropTile> makeDemoDropTarget(
    std::string name,
    std::string label,
    std::function<bool(const std::string& payload)> accept,
    std::function<void(const std::string& payload)> onDropped);

struct FVectorDemoCanvas : public ya::UIElement
{
    explicit FVectorDemoCanvas(std::string name) : ya::UIElement(std::move(name)) {}
    void paintSelf(ya::UIFrameBuilder& builder) override;
};

} // namespace guiworkbench
