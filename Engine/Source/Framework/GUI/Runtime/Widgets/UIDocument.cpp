#include "GUI/Widgets/UIDocument.h"

#include "Core/Log.h"
#include "Core/Reflection/ReflectionSerializer.h"

#include "GUI/Widgets/UITypeRegistry.h"
#include "GUI/Layout/UILayout.h"

#include <limits>

namespace
{
using namespace ya;

/// Geometry is authored by the parent-owned edge, never by the widget fields.
/// Reflection still exposes the transitional members to runtime code, so the
/// document boundary removes only the UIElement base geometry block from newly
/// generated v2 authoring data. Control-specific size fields remain intact.
void stripUIElementGeometry(nlohmann::json& value)
{
    if (!value.is_object()) {
        return;
    }
    if (auto it = value.find("__base__"); it != value.end() && it->is_object()) {
        if (auto baseIt = it->find("UIElement"); baseIt != it->end() && baseIt->is_object()) {
            baseIt->erase("_position");
            baseIt->erase("_size");
            baseIt->erase("_bAutoSize");
        }
    }
}

bool containsUIElementGeometry(const nlohmann::json& value)
{
    if (!value.is_object()) {
        return false;
    }
    if (auto it = value.find("__base__"); it != value.end() && it->is_object()) {
        if (auto baseIt = it->find("UIElement"); baseIt != it->end() && baseIt->is_object()) {
            if (baseIt->contains("_position") || baseIt->contains("_size") ||
                baseIt->contains("_bAutoSize")) {
                return true;
            }
        }
    }
    for (const auto& [key, child] : value.items()) {
        (void)key;
        if (containsUIElementGeometry(child)) {
            return true;
        }
    }
    return false;
}

nlohmann::json slotToJson(const UISlot& slot)
{
    nlohmann::json j;
    slot.serialize(j);
    return j;
}

void applySlotJson(UISlot& slot, const nlohmann::json& j)
{
    if (!j.is_object()) {
        return;
    }
    slot.deserialize(j);
}
}

namespace ya
{

std::shared_ptr<UIDocument> UIDocument::fromWidget(const UIElement& widget)
{
    if (widget._typeId.empty()) {
        YA_CORE_ERROR("UIDocument::fromWidget: widget '{}' has no registry type ID "
                      "(create it via UITypeRegistry::createInstance)",
                      widget._name);
        return nullptr;
    }

    auto document     = std::make_shared<UIDocument>();
    document->typeId  = widget._typeId;
    document->fields  = widget.serializeFields();
    stripUIElementGeometry(document->fields);
    for (const auto& child : widget.getChildren()) {
        if (auto childDoc = fromWidget(*child)) {
            document->children.push_back(std::move(childDoc));
            document->childSlots.push_back(widget.getSlotForChild(*child) ? slotToJson(*widget.getSlotForChild(*child)) : nlohmann::json{{"type", "base"}});
        }
    }
    return document;
}

UIElementRef UIDocument::instantiate() const
{
    auto& registry = UITypeRegistry::instance();
    UIElementRef root = registry.createInstance(typeId);
    if (!root) {
        return nullptr;
    }

    root->deserializeFields(fields);

    for (size_t i = 0; i < children.size(); ++i) {
        const auto& childDoc = children[i];
        if (!childDoc) {
            continue;
        }
        UIElementRef child = childDoc->instantiate();
        if (!child) {
            YA_CORE_ERROR("UIDocument::instantiate: failed to instantiate child of '{}'", typeId);
            continue;
        }
        const nlohmann::json slotIntent = i < childSlots.size() ? childSlots[i] : nlohmann::json{};
        root->addDetachedChild(child, [slotIntent](UIElement&, UISlot& slot) { applySlotJson(slot, slotIntent); });
    }
    return root;
}

nlohmann::json UIDocument::toJson() const
{
    nlohmann::json j;
    j["version"] = kFormatVersion;
    j["typeId"]  = typeId;
    j["fields"]  = fields;
    stripUIElementGeometry(j["fields"]);
    j["children"] = nlohmann::json::array();
    for (const auto& child : children) {
        j["children"].push_back(child->toJson());
    }
    j["childSlots"] = childSlots;
    return j;
}

std::shared_ptr<UIDocument> UIDocument::fromJson(const nlohmann::json& json)
{
    if (!json.is_object() || !json.contains("version") || !json.contains("typeId")) {
        YA_CORE_ERROR("UIDocument::fromJson: malformed document (missing version/typeId)");
        return nullptr;
    }
    if (json["version"].get<uint32_t>() != kFormatVersion) {
        YA_CORE_ERROR("UIDocument::fromJson: unsupported document version {}",
                      json["version"].get<uint32_t>());
        return nullptr;
    }

    auto document    = std::make_shared<UIDocument>();
    document->typeId = json["typeId"].get<std::string>();
    if (json.contains("fields")) {
        document->fields = json["fields"];
        if (containsUIElementGeometry(document->fields)) {
            YA_CORE_ERROR("UIDocument::fromJson: widget geometry must be stored on parent-owned slots");
            return nullptr;
        }
    }
    if (json.contains("children") && json["children"].is_array()) {
        for (const auto& childJson : json["children"]) {
            if (auto child = fromJson(childJson)) {
                document->children.push_back(std::move(child));
            }
            else {
                return nullptr; // A broken child poisons the whole document.
            }
        }
    }
    if (json.contains("childSlots") && json["childSlots"].is_array()) {
        document->childSlots = json["childSlots"].get<std::vector<nlohmann::json>>();
        if (document->childSlots.size() != document->children.size()) return nullptr;
    } else if (!document->children.empty()) {
        YA_CORE_ERROR("UIDocument::fromJson: missing childSlots");
        return nullptr;
    }
    return document;
}

} // namespace ya
