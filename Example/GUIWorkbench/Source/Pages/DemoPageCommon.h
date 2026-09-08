#pragma once

#include "GUI/Declarative/Build.h"
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

constexpr glm::vec4 kPanelColor  = {0.11f, 0.12f, 0.15f, 1.0f};
constexpr glm::vec4 kHeaderColor = {0.55f, 0.60f, 0.68f, 1.0f};
constexpr glm::vec4 kTextColor   = {0.88f, 0.90f, 0.94f, 1.0f};

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
