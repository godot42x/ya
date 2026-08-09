#include "Scene/Serialization/LegacyUIMigration.h"

#include "Core/Log.h"

#include <optional>

namespace ya
{

namespace
{

constexpr const char* kCanvasType = "UICanvasNode";

std::optional<std::string> mapType(const std::string& legacyTypeName)
{
    if (legacyTypeName == "UIPanelNode") {
        return std::string("engine.panel");
    }
    if (legacyTypeName == "UITextNode") {
        return std::string("engine.text");
    }
    if (legacyTypeName == "UIButtonNode") {
        return std::string("engine.button");
    }
    if (legacyTypeName == "UIContainerNode") {
        return std::string("engine.container");
    }
    return std::nullopt;
}

/// Translate legacy field JSON into the new schema: rename the base block and
/// convert the old `_visible` bool into the `_visibility` enum name.
nlohmann::json translateFields(nlohmann::json fields)
{
    if (fields.contains("__base__") && fields["__base__"].is_object() &&
        fields["__base__"].contains("Node2D")) {
        fields["__base__"]["UIElement"] = fields["__base__"]["Node2D"];
        fields["__base__"].erase("Node2D");
    }
    // Old schemas stored `_visible` as a base-class bool inside the block.
    if (fields.contains("__base__") && fields["__base__"].is_object() &&
        fields["__base__"].contains("UIElement") && fields["__base__"]["UIElement"].is_object()) {
        auto& base = fields["__base__"]["UIElement"];
        if (base.contains("_visible") && base["_visible"].is_boolean()) {
            base["_visibility"] = base["_visible"].get<bool>() ? "Visible" : "Hidden";
            base.erase("_visible");
        }
    }
    if (fields.contains("_visible") && fields["_visible"].is_boolean()) {
        fields["_visibility"] = fields["_visible"].get<bool>() ? "Visible" : "Hidden";
        fields.erase("_visible");
    }
    return fields;
}

} // namespace

std::string legacyUITypeToTypeId(const std::string& legacyTypeName)
{
    const auto mapped = mapType(legacyTypeName);
    return mapped.value_or("");
}

namespace
{

/// zOrder from a legacy UI node JSON (base block or inline field).
int32_t legacyNodeZOrder(const nlohmann::json& nodeJson)
{
    if (!nodeJson.contains("fields") || !nodeJson["fields"].is_object()) {
        return 0;
    }
    const auto& fields = nodeJson["fields"];
    if (fields.contains("_zOrder") && fields["_zOrder"].is_number_integer()) {
        return fields["_zOrder"].get<int32_t>();
    }
    if (fields.contains("__base__") && fields["__base__"].is_object() &&
        fields["__base__"].contains("Node2D") && fields["__base__"]["Node2D"].is_object()) {
        const auto& base = fields["__base__"]["Node2D"];
        if (base.contains("_zOrder") && base["_zOrder"].is_number_integer()) {
            return base["_zOrder"].get<int32_t>();
        }
    }
    return 0;
}

} // namespace

std::vector<LegacyUIMigrationResult> migrateLegacyUINode(const nlohmann::json& nodeJson)
{
    if (!nodeJson.is_object() || !nodeJson.contains("nodeType")) {
        return {};
    }

    const std::string legacyType = nodeJson["nodeType"].get<std::string>();

    // The canvas was the old UI root filler; the content layer replaces it,
    // so its children become independent top-level entries.
    if (legacyType == kCanvasType) {
        std::vector<LegacyUIMigrationResult> results;
        if (nodeJson.contains("children") && nodeJson["children"].is_array()) {
            for (const auto& childJson : nodeJson["children"]) {
                auto childResults = migrateLegacyUINode(childJson);
                results.insert(results.end(), childResults.begin(), childResults.end());
            }
        }
        return results;
    }

    const auto typeId = mapType(legacyType);
    if (!typeId) {
        YA_CORE_ERROR("LegacyUIMigration: unknown legacy UI node type '{}'; "
                      "entry dropped (no silent empty node)",
                      legacyType);
        return {};
    }

    nlohmann::json documentJson;
    documentJson["version"] = UIDocument::kFormatVersion;
    documentJson["typeId"]  = *typeId;
    if (nodeJson.contains("fields")) {
        documentJson["fields"] = translateFields(nodeJson["fields"]);
    }
    else {
        documentJson["fields"] = nlohmann::json::object();
    }
    documentJson["children"] = nlohmann::json::array();
    if (nodeJson.contains("children") && nodeJson["children"].is_array()) {
        for (const auto& childJson : nodeJson["children"]) {
            auto childResults = migrateLegacyUINode(childJson);
            for (const auto& childResult : childResults) {
                documentJson["children"].push_back(childResult.document->toJson());
            }
        }
    }

    auto document = UIDocument::fromJson(documentJson);
    if (!document) {
        YA_CORE_ERROR("LegacyUIMigration: failed to build document for legacy type '{}'", legacyType);
        return {};
    }
    LegacyUIMigrationResult result;
    result.document = std::move(document);
    result.name     = nodeJson.value("name", "UI");
    result.zOrder   = legacyNodeZOrder(nodeJson);
    return {std::move(result)};
}

} // namespace ya
