#include "GUI/Widgets/WidgetTreeDump.h"

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/Slider.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/TableGrid.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/WidgetTree.h"

namespace ya
{

namespace
{

nlohmann::json routePolicyName(EWidgetRoutePolicy policy)
{
    switch (policy) {
    case EWidgetRoutePolicy::None: return "none";
    case EWidgetRoutePolicy::HitTest: return "hitTest";
    case EWidgetRoutePolicy::PointerCapture: return "pointerCapture";
    case EWidgetRoutePolicy::Focus: return "focus";
    case EWidgetRoutePolicy::TabTraversal: return "tabTraversal";
    case EWidgetRoutePolicy::DragSession: return "dragSession";
    case EWidgetRoutePolicy::Popup: return "popup";
    case EWidgetRoutePolicy::Modal: return "modal";
    }
    return "unknown";
}

nlohmann::json routePhaseName(EWidgetEventRoutePhase phase)
{
    switch (phase) {
    case EWidgetEventRoutePhase::Preview: return "preview";
    case EWidgetEventRoutePhase::Target: return "target";
    case EWidgetEventRoutePhase::Bubble: return "bubble";
    }
    return "unknown";
}

nlohmann::json routeResultName(EWidgetRouteResult result)
{
    switch (result) {
    case EWidgetRouteResult::NotHandled: return "notHandled";
    case EWidgetRouteResult::HandledPass: return "handledPass";
    case EWidgetRouteResult::HandledExclusive: return "handledExclusive";
    }
    return "unknown";
}

nlohmann::json serializeNode(const UIElement& element, const WidgetTree& tree)
{
    nlohmann::json node;
    node["name"]   = element._name;
    node["typeId"] = element._typeId;
    node["rect"]   = {
        {"x", element._layoutRect.pos.x},
        {"y", element._layoutRect.pos.y},
        {"w", element._layoutRect.extent.x},
        {"h", element._layoutRect.extent.y},
    };
    node["visibility"]  = static_cast<int>(element.getVisibility());
    node["zOrder"]      = element._zOrder;
    node["hitFilter"]   = static_cast<int>(element._hitFilter);
    node["focusPolicy"] = static_cast<int>(element._focusPolicy);
    node["focused"]     = tree.getFocused() == &element;
    node["hovered"]     = tree.getHovered() == &element;
    node["captured"]    = tree.getPointerCapture() == &element;

    if (const auto* box = dynamic_cast<const UIContainer*>(&element)) {
        const UIBoxLayout& layout = box->getBoxLayout();
        node["layout"] = {
            {"type", "box"},
            {"direction", layout.getDirection() == EWidgetBoxLayout::Horizontal ? "horizontal" : "vertical"},
            {"spacing", layout.getSpacing()},
            {"padding", {{"x", layout.getPadding().x}, {"y", layout.getPadding().y}}},
            {"mainAxisAlignment", static_cast<int>(layout.getMainAxisAlignment())},
            {"clipsChildren", layout.clipsChildren()},
            {"stretchLastChild", layout.stretchesLastChild()},
        };
    }
    else if (const auto* button = dynamic_cast<const UIButton*>(&element)) {
        node["layout"] = {
            {"type", "singleChild"},
            {"padding",
             {
                 {"x", button->getContentPadding().x},
                 {"y", button->getContentPadding().y},
             }},
        };
    }
    else if (const auto* split = dynamic_cast<const UISplitPane*>(&element)) {
        const UISplitLayout& layout = split->getSplitLayout();
        node["layout"] = {
            {"type", "split"},
            {"orientation", layout.getOrientation() == ESplitOrientation::Vertical ? "vertical" : "horizontal"},
            {"ratio", layout.getSplitRatio()},
            {"dividerThickness", layout.getDividerThickness()},
            {"minFirstExtent", layout.getMinFirstExtent()},
            {"minSecondExtent", layout.getMinSecondExtent()},
        };
    }
    else if (const auto* scroll = dynamic_cast<const UIScrollViewport*>(&element)) {
        const UIScrollLayout& layout = scroll->getScrollLayout();
        node["layout"] = {
            {"type", "scroll"},
            {"axis", layout.getAxis() == EScrollAxis::Vertical ? "vertical" : "horizontal"},
            {"offset", layout.getScrollOffset()},
            {"maxOffset", layout.getMaxScrollOffset()},
            {"step", layout.getScrollStep()},
        };
    }
    if (const UISlot* slot = element.getSlot()) {
        nlohmann::json slotNode = {
            {"parent", slot->getParent()._name},
            {"type", "base"},
        };
        if (const auto* boxSlot = dynamic_cast<const UIBoxSlot*>(slot)) {
            slotNode["type"] = "box";
            slotNode["sizeRule"] = boxSlot->getSizeRule() == EUIBoxSlotSizeRule::Fill ? "fill" : "auto";
            slotNode["weight"] = boxSlot->getWeight();
            slotNode["margin"] = {{"x", boxSlot->getMargin().x}, {"y", boxSlot->getMargin().y}};
            slotNode["crossAlignment"] = static_cast<int>(boxSlot->getCrossAlignment());
            slotNode["participatesInLayout"] = boxSlot->participatesInLayout();
        }
        node["slot"] = std::move(slotNode);
    }
    if (const auto* button = dynamic_cast<const UIButton*>(&element)) {
        node["control"] = {
            {"type", "button"},
            {"hovered", button->_bHovered.get()},
            {"pressed", button->_bPressed.get()},
            {"focused", button->_bFocused.get()},
        };
    }
    else if (const auto* checkBox = dynamic_cast<const UICheckBox*>(&element)) {
        node["control"] = {
            {"type", "checkBox"},
            {"checked", checkBox->_bChecked},
        };
    }
    else if (const auto* slider = dynamic_cast<const UISlider*>(&element)) {
        node["control"] = {
            {"type", "slider"},
            {"value", slider->_value},
            {"step", slider->_step},
        };
    }
    else if (const auto* combo = dynamic_cast<const UIComboBox*>(&element)) {
        node["control"] = {
            {"type", "comboBox"},
            {"selectedIndex", combo->_selectedIndex},
            {"label", combo->currentLabel()},
        };
    }
    else if (const auto* textField = dynamic_cast<const UITextField*>(&element)) {
        node["control"] = {
            {"type", "textField"},
            {"text", textField->_text},
            {"cursor", textField->getCursorIndex()},
        };
    }
    else if (const auto* text = dynamic_cast<const UIText*>(&element)) {
        node["control"] = {
            {"type", "text"},
            {"text", text->getText()},
        };
    }
    else if (const auto* scroll = dynamic_cast<const UIScrollViewport*>(&element)) {
        node["control"] = {
            {"type", "scrollViewport"},
            {"offset", scroll->getScrollOffset()},
            {"maxOffset", scroll->getMaxScrollOffset()},
        };
    }
    else if (const auto* split = dynamic_cast<const UISplitPane*>(&element)) {
        const Rect2D divider = split->getDividerRect();
        node["control"] = {
            {"type", "splitPane"},
            {"ratio", split->getSplitRatio()},
            {"divider",
             {
                 {"x", divider.pos.x},
                 {"y", divider.pos.y},
                 {"w", divider.extent.x},
                 {"h", divider.extent.y},
             }},
        };
    }
    else if (const auto* table = dynamic_cast<const UITableGrid*>(&element)) {
        node["control"] = {
            {"type", "tableGrid"},
            {"selected", table->getSelection() ? table->getSelection()->value() : -1},
        };
    }
    else if (const auto* overlay = dynamic_cast<const UIPopupOverlay*>(&element)) {
        node["control"] = {
            {"type", "popupOverlay"},
            {"modal", overlay->_bModal},
        };
    }

    nlohmann::json children = nlohmann::json::array();
    for (UIElement* child : element.getChildrenInPaintOrder()) {
        children.push_back(serializeNode(*child, tree));
    }
    node["children"] = std::move(children);
    return node;
}

} // namespace

nlohmann::json dumpWidgetTree(const WidgetTree& tree)
{
    static constexpr std::pair<WidgetTree::ELayer, const char*> kLayerNames[] = {
        {WidgetTree::ELayer::Content, "Content"},
        {WidgetTree::ELayer::Popup, "Popup"},
        {WidgetTree::ELayer::Tooltip, "Tooltip"},
        {WidgetTree::ELayer::DragIme, "DragIme"},
    };

    nlohmann::json layers = nlohmann::json::object();
    for (const auto& entry : kLayerNames) {
        layers[entry.second] = serializeNode(*tree.getLayer(entry.first), tree);
    }
    const auto serializePath = [](const std::vector<UIElement*>& path) {
        nlohmann::json names = nlohmann::json::array();
        for (const UIElement* node : path) {
            names.push_back(node->_name);
        }
        return names;
    };
    const WidgetPointerState& pointer = tree.getPointerState();
    const WidgetRouteTrace& route = tree.getLastRouteTrace();
    nlohmann::json routeSteps = nlohmann::json::array();
    for (const WidgetRouteTrace::Step& step : route.steps) {
        routeSteps.push_back({
            {"widget", step.widget},
            {"phase", static_cast<int>(step.phase)},
            {"phaseName", routePhaseName(step.phase)},
            {"handled", step.bHandled},
            {"hitFilter", static_cast<int>(step.hitFilter)},
        });
    }

    return {
        {"logicalExtent",
         {
             {"width", tree.getLogicalExtent().width},
             {"height", tree.getLogicalExtent().height},
         }},
        {"pointer",
         {
             {"known", pointer.bKnown},
             {"x", pointer.logicalPoint.x},
             {"y", pointer.logicalPoint.y},
             {"path", serializePath(tree.getPointerPath())},
         }},
        {"focusPath", serializePath(tree.getFocusPath())},
        {"lastRoute",
         {
             {"policy", static_cast<int>(route.policy)},
             {"policyName", routePolicyName(route.policy)},
             {"target", route.target},
             {"path", route.path},
             {"steps", std::move(routeSteps)},
             {"result", static_cast<int>(route.result)},
             {"resultName", routeResultName(route.result)},
         }},
        {"layers", std::move(layers)},
    };
}

const nlohmann::json* findWidgetNode(const nlohmann::json& root,
                                     const std::string& name)
{
    if (root.is_object() && root.value("name", "") == name) {
        return &root;
    }
    if (root.is_object()) {
        // Descend through container keys ("layers", "children") and any
        // nested object/array value; scalar leaves ("rect", ...) are skipped.
        for (const auto& entry : root.items()) {
            const auto& value = entry.value();
            if (value.is_object() || value.is_array()) {
                if (const auto* hit = findWidgetNode(value, name)) {
                    return hit;
                }
            }
        }
    }
    else if (root.is_array()) {
        for (const auto& item : root) {
            if (const auto* hit = findWidgetNode(item, name)) {
                return hit;
            }
        }
    }
    return nullptr;
}

} // namespace ya
