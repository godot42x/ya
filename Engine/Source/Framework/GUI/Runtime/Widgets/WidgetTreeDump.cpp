#include "GUI/Widgets/WidgetTreeDump.h"

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/DockSpace/DockSpace.h"
#include "GUI/Widgets/Controls/Overlay.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/SizeBox.h"
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

nlohmann::json marginJson(const FMargin& margin)
{
    return {
        {"left", margin.left},
        {"top", margin.top},
        {"right", margin.right},
        {"bottom", margin.bottom},
    };
}

const char* overlayAlignName(EUIOverlayAlignment align)
{
    switch (align) {
    case EUIOverlayAlignment::Fill: return "fill";
    case EUIOverlayAlignment::Start: return "start";
    case EUIOverlayAlignment::Center: return "center";
    case EUIOverlayAlignment::End: return "end";
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

    element.appendRuntimeLayoutDiagnostics(node["layout"]);
    if (const UISlot* slot = element.getSlot()) {
        nlohmann::json slotNode = {{"parent", slot->getParent()._name}};
        slot->appendRuntimeDiagnostics(slotNode);
        node["slot"] = std::move(slotNode);
    }
    element.appendRuntimeDiagnostics(node, tree);

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
        const std::string selector = widgetIt->get<std::string>();
        // A leading '!' asserts ABSENCE: the widget must not exist in the
        // tree (e.g. a floating dock window that was re-docked away).
        const bool bExpectAbsent = !selector.empty() && selector[0] == '!';
        const std::string widgetName = bExpectAbsent ? selector.substr(1) : selector;
        const nlohmann::json* node = findWidgetNode(treeDump, widgetName);
        if (bExpectAbsent) {
            if (node) {
                error = std::format("widget '{}' expected absent but found", widgetName);
                return false;
            }
            expected.erase(widgetIt);
            return jsonContains(treeDump, expected, "tree", error);
        }
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
