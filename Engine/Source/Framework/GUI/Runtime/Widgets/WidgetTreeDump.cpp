#include "GUI/Widgets/WidgetTreeDump.h"

#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/WidgetTree.h"

namespace ya
{

namespace
{

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
    node["visibility"]  = static_cast<int>(element._visibility);
    node["zOrder"]      = element._zOrder;
    node["hitFilter"]   = static_cast<int>(element._hitFilter);
    node["focusPolicy"] = static_cast<int>(element._focusPolicy);
    node["focused"]     = tree.getFocused() == &element;
    node["hovered"]     = tree.getHovered() == &element;
    node["captured"]    = tree.getPointerCapture() == &element;

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

    return {
        {"logicalExtent",
         {
             {"width", tree.getLogicalExtent().width},
             {"height", tree.getLogicalExtent().height},
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
