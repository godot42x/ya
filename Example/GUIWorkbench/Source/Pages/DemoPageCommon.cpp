#include "DemoPageCommon.h"

#include "GUI/Widgets/WidgetTree.h"

#include <utility>

namespace guiworkbench
{

std::shared_ptr<ya::UIText> makeLabel(const std::string& text, float fontSize)
{
    auto label       = std::make_shared<ya::UIText>(text + "_Label");
    label->_fontSize = static_cast<uint32_t>(fontSize);
    label->setText(text);
    label->setColor(kHeaderColor);
    return label;
}

std::shared_ptr<ya::UIText> makeBodyText(const std::string& text)
{
    auto label       = std::make_shared<ya::UIText>(text + "_Body");
    label->_fontSize = 13;
    label->setText(text);
    label->setColor(kTextColor);
    return label;
}

std::shared_ptr<ya::UIButton> makeDemoButton(const std::string& name, const std::string& label, float width)
{
    auto button = std::make_shared<ya::UIButton>(name);
    if (width <= 0.0f) {
        button->setContentPadding({12.0f, 4.0f});
    }

    auto text       = std::make_shared<ya::UIText>(name + "_Label");
    text->_fontSize = 13;
    text->setText(label);
    text->_hAlign   = ya::EWidgetAlignH::Center;
    text->_vAlign   = ya::EWidgetAlignV::Center;
    button->addDetachedChild(text);
    return button;
}

std::shared_ptr<ya::UIContainer> makeRow(ya::WidgetTree& tree, ya::UIElement& parent, float spacing)
{
    auto row = std::make_shared<ya::UIContainer>("Row");
    row->setDirection(ya::EWidgetBoxLayout::Horizontal);
    row->setSpacing(spacing);
    tree.attach(parent, row);
    return row;
}

std::shared_ptr<ya::UIDragDropTile> makeDemoDragSource(std::string name, std::string label, std::string payload)
{
    auto tile    = std::make_shared<ya::UIDragDropTile>(std::move(name), ya::UIDragDropTile::EKind::Source);
    tile->_label = std::move(label);
    auto behavior = std::make_shared<ya::UIDragSourceBehavior>();
    behavior->bCapturePointerOnPress = true;
    behavior->setPressedState        = [](ya::UIElement& owner, bool bPressed)
    {
        if (auto* tileOwner = dynamic_cast<ya::UIDragDropTile*>(&owner)) {
            tileOwner->setPressed(bPressed);
        }
    };
    const std::string behaviorPayload = std::move(payload);
    behavior->operationFactory        = [behaviorPayload, ghostLabel = tile->_label](ya::UIElement&)
    {
        return ya::UIStringDragDropOperation::make(
            behaviorPayload,
            ghostLabel.empty() ? behaviorPayload : ghostLabel,
            "workbench.payload");
    };
    tile->addBehavior(behavior);
    return tile;
}

std::shared_ptr<ya::UIDragDropTile> makeDemoDropTarget(
    std::string name,
    std::string label,
    std::function<bool(const std::string& payload)> accept,
    std::function<void(const std::string& payload)> onDropped)
{
    auto tile    = std::make_shared<ya::UIDragDropTile>(std::move(name), ya::UIDragDropTile::EKind::Target);
    tile->_label = std::move(label);
    auto behavior = std::make_shared<ya::UIDropTargetBehavior>();
    behavior->canAccept = [accept = std::move(accept)](ya::UIElement& owner,
                                                       const ya::UIDragDropOperation& operation,
                                                       const glm::vec2& logicalPoint)
    {
        const auto* textOp = operation.as<ya::UIStringDragDropOperation>();
        if (!textOp) {
            return false;
        }
        return owner.hitTestLayoutRect(logicalPoint) && (accept ? accept(textOp->text) : !textOp->text.empty());
    };
    behavior->handleDrop = [onDropped = std::move(onDropped)](ya::UIElement&,
                                                              const ya::UIDragDropOperation& operation,
                                                              const glm::vec2&)
    {
        const auto* textOp = operation.as<ya::UIStringDragDropOperation>();
        if (onDropped && textOp) {
            onDropped(textOp->text);
        }
    };
    behavior->setHighlightState = [](ya::UIElement& owner, bool bHighlight)
    {
        if (auto* tileOwner = dynamic_cast<ya::UIDragDropTile*>(&owner)) {
            tileOwner->setHighlighted(bHighlight);
        }
    };
    tile->addBehavior(behavior);
    return tile;
}

void FVectorDemoCanvas::paintSelf(ya::UIFrameBuilder& builder)
{
    builder.addSprite(_layoutRect, {0.10f, 0.11f, 0.14f, 1.0f}, nullptr);

    const glm::vec2 o = _layoutRect.pos + glm::vec2(12.0f, 12.0f);
    builder.addLine(o, o + glm::vec2(120.0f, 0.0f), {0.35f, 0.80f, 0.55f, 1.0f}, 1.0f);
    builder.addLine(o + glm::vec2(0.0f, 24.0f), o + glm::vec2(120.0f, 24.0f), {0.35f, 0.80f, 0.55f, 1.0f}, 2.0f);
    builder.addLine(o + glm::vec2(0.0f, 48.0f), o + glm::vec2(120.0f, 72.0f), {0.85f, 0.65f, 0.30f, 1.0f}, 2.0f);

    const ya::Rect2D rect{.pos = o + glm::vec2(150.0f, 0.0f), .extent = {90.0f, 56.0f}};
    builder.addRectOutline(rect, {0.45f, 0.60f, 0.90f, 1.0f}, 2.0f);

    builder.addBezierCubic(o + glm::vec2(270.0f, 70.0f),
                           o + glm::vec2(310.0f, -20.0f),
                           o + glm::vec2(360.0f, 140.0f),
                           o + glm::vec2(400.0f, 30.0f),
                           {0.90f, 0.45f, 0.70f, 1.0f},
                           2.0f,
                           32);
}

} // namespace guiworkbench
