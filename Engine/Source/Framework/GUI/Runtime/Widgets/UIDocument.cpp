#include "GUI/Widgets/UIDocument.h"

#include "Core/Log.h"
#include "Core/Reflection/ReflectionSerializer.h"

#include "GUI/Widgets/UITypeRegistry.h"

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
    for (const auto& child : widget.getChildren()) {
        if (auto childDoc = fromWidget(*child)) {
            document->children.push_back(std::move(childDoc));
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

    for (const auto& childDoc : children) {
        if (!childDoc) {
            continue;
        }
        UIElementRef child = childDoc->instantiate();
        if (!child) {
            YA_CORE_ERROR("UIDocument::instantiate: failed to instantiate child of '{}'", typeId);
            continue;
        }
        root->addDetachedChild(child);
    }
    return root;
}

nlohmann::json UIDocument::toJson() const
{
    nlohmann::json j;
    j["version"] = kFormatVersion;
    j["typeId"]  = typeId;
    j["fields"]  = fields;
    j["children"] = nlohmann::json::array();
    for (const auto& child : children) {
        j["children"].push_back(child->toJson());
    }
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
    return document;
}

} // namespace ya
