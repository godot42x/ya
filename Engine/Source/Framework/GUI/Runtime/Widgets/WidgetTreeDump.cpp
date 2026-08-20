#include "GUI/Widgets/WidgetTreeDump.h"

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/DockSpace.h"
#include "GUI/Widgets/Controls/DragDrop.h"
#include "GUI/Widgets/Controls/InputExtras.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/Slider.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/TableGrid.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/Controls/TreeView.h"
#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/WidgetTree.h"

#include <format>
#include <functional>

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
    else if (const auto* drag = dynamic_cast<const UIDragFloat*>(&element)) {
        node["control"] = {
            {"type", "dragFloat"},
            {"value", drag->_value},
        };
    }
    else if (const auto* spin = dynamic_cast<const UISpinBox*>(&element)) {
        node["control"] = {
            {"type", "spinBox"},
            {"value", spin->_value},
        };
    }
    else if (const auto* radio = dynamic_cast<const UIRadioButton*>(&element)) {
        node["control"] = {
            {"type", "radioButton"},
            {"checked", radio->_bChecked},
        };
    }
    else if (const auto* colorEdit = dynamic_cast<const UIColorEdit*>(&element)) {
        node["control"] = {
            {"type", "colorEdit"},
            {"color", {colorEdit->_color.r, colorEdit->_color.g, colorEdit->_color.b, colorEdit->_color.a}},
            {"activeChannel", colorEdit->_activeChannel},
        };
    }
    else if (const auto* searchCombo = dynamic_cast<const UISearchComboBox*>(&element)) {
        node["control"] = {
            {"type", "searchComboBox"},
            {"selectedIndex", searchCombo->_selectedIndex},
            {"filter", searchCombo->_filter},
        };
    }
    else if (const auto* dragSource = dynamic_cast<const UIDragSource*>(&element)) {
        node["control"] = {
            {"type", "dragSource"},
            {"label", dragSource->_label},
        };
    }
    else if (const auto* dropTarget = dynamic_cast<const UIDropTarget*>(&element)) {
        node["control"] = {
            {"type", "dropTarget"},
        };
    }
    else if (const auto* treeView = dynamic_cast<const UITreeView*>(&element)) {
        node["control"] = {
            {"type", "treeView"},
            {"visibleRows", treeView->getVisibleRowCount()},
            {"selected", treeView->getSelection() ? treeView->getSelection()->value() : std::string{}},
        };
    }
    else if (const auto* dockSpace = dynamic_cast<const UIDockSpace*>(&element)) {
        nlohmann::json preview = {
            {"active", dockSpace->hasDropPreview()},
            {"disabled", dockSpace->isDropPreviewDisabled()},
            {"targetLeafId", dockSpace->getDropPreviewTargetLeafId()},
            {"kind", dockSpace->isDropPreviewMerge() ? "merge" : "cardinal"},
            {"disabledReason", dockSpace->getDropPreviewDisabledReason()},
        };
        if (!dockSpace->hasDropPreview()) {
            preview["kind"] = "none";
        }
        node["control"] = {
            {"type", "dockSpace"},
            {"preview", std::move(preview)},
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

namespace
{

bool jsonContains(const nlohmann::json& actual,
                  const nlohmann::json& expected,
                  std::string_view path,
                  std::string& error)
{
    // Numeric predicates keep resize/drag scenarios semantic: they can
    // assert that geometry changed without baking one machine's exact float
    // result into every checkpoint.
    if (expected.is_object() &&
        (expected.contains("$gt") || expected.contains("$gte") ||
         expected.contains("$lt") || expected.contains("$lte"))) {
        if (!actual.is_number()) {
            error = std::format("{}: comparison requires a number, got {}", path, actual.type_name());
            return false;
        }
        const double value = actual.get<double>();
        const auto check = [&](const char* op, const std::function<bool(double, double)>& predicate) {
            const auto it = expected.find(op);
            if (it == expected.end()) {
                return true;
            }
            if (!it->is_number()) {
                error = std::format("{}: {} must be numeric", path, op);
                return false;
            }
            if (!predicate(value, it->get<double>())) {
                error = std::format("{}: {} {} {} failed", path, value, op, it->dump());
                return false;
            }
            return true;
        };
        return check("$gt", [](double lhs, double rhs) { return lhs > rhs; }) &&
               check("$gte", [](double lhs, double rhs) { return lhs >= rhs; }) &&
               check("$lt", [](double lhs, double rhs) { return lhs < rhs; }) &&
               check("$lte", [](double lhs, double rhs) { return lhs <= rhs; });
    }
    if (expected.is_object()) {
        if (!actual.is_object()) {
            error = std::format("{}: expected object, got {}", path, actual.type_name());
            return false;
        }
        for (const auto& entry : expected.items()) {
            const auto actualIt = actual.find(entry.key());
            if (actualIt == actual.end()) {
                error = std::format("{}: missing field '{}'", path, entry.key());
                return false;
            }
            if (!jsonContains(*actualIt, entry.value(),
                              std::format("{}.{}", path, entry.key()), error)) {
                return false;
            }
        }
        return true;
    }
    if (expected.is_array()) {
        if (actual != expected) {
            error = std::format("{}: expected array {} but got {}", path, expected.dump(), actual.dump());
            return false;
        }
        return true;
    }
    if (actual != expected) {
        error = std::format("{}: expected {} but got {}", path, expected.dump(), actual.dump());
        return false;
    }
    return true;
}

} // namespace

bool assertScenarioTree(const WidgetTree& tree, std::string_view assertion, std::string& error)
{
    nlohmann::json expected;
    try {
        expected = nlohmann::json::parse(assertion);
    }
    catch (const std::exception& e) {
        error = std::format("invalid assertion JSON: {}", e.what());
        return false;
    }

    const nlohmann::json treeDump = dumpWidgetTree(tree);
    if (const auto widgetIt = expected.find("widget"); widgetIt != expected.end()) {
        if (!widgetIt->is_string()) {
            error = "widget assertion selector must be a string";
            return false;
        }
        const std::string widgetName = widgetIt->get<std::string>();
        const nlohmann::json* node = findWidgetNode(treeDump, widgetName);
        if (!node) {
            error = std::format("widget '{}' not found", widgetName);
            return false;
        }
        expected.erase(widgetIt);
        return jsonContains(*node, expected, std::format("widget[{}]", widgetName), error);
    }
    return jsonContains(treeDump, expected, "tree", error);
}

} // namespace ya
