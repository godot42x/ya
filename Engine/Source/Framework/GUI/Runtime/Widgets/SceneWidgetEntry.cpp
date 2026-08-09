#include "GUI/Widgets/SceneWidgetEntry.h"

#include "Core/Log.h"
#include "Core/Reflection/ReflectionSerializer.h"

namespace ya
{

namespace
{

/// Find the class in the reflected hierarchy that owns `fieldName`.
const Class* findFieldOwner(const Class* cls, const std::string& fieldName)
{
    if (!cls) {
        return nullptr;
    }
    if (cls->hasProperty(fieldName)) {
        return cls;
    }
    for (auto parentTypeId : cls->parents) {
        if (const Class* found = findFieldOwner(cls->getClassByTypeId(parentTypeId), fieldName)) {
            return found;
        }
    }
    return nullptr;
}

/// Build the reflected-fields JSON for one field value: fields on the widget
/// class itself go flat; inherited fields nest under the `__base__` blocks
/// from the owner class up to the widget class (mirrors the serializer shape).
nlohmann::json buildFieldJson(const Class* rootClass, const Class* ownerClass,
                              const std::string& fieldName, const nlohmann::json& value)
{
    if (ownerClass == rootClass) {
        return nlohmann::json{{fieldName, value}};
    }

    // Collect the chain [owner, ..., direct parent of root].
    std::vector<const Class*> chain;
    for (const Class* cls = ownerClass; cls && cls != rootClass;) {
        chain.push_back(cls);
        const Class* next = nullptr;
        for (auto parentTypeId : cls->parents) {
            const Class* parent = cls->getClassByTypeId(parentTypeId);
            if (parent == rootClass || findFieldOwner(parent, fieldName)) {
                next = parent;
                break;
            }
        }
        cls = next;
    }

    nlohmann::json result = nlohmann::json{{fieldName, value}};
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        nlohmann::json block;
        block["__base__"]            = nlohmann::json::object();
        block["__base__"][(*it)->name] = result;
        result                       = std::move(block);
    }
    return result;
}

} // namespace

bool UIInstanceOverrideSet::applyTo(UIElement& widget) const
{
    bool bAllApplied = true;
    for (const auto& [fieldName, value] : fieldOverrides) {
        auto* cls   = ClassRegistry::instance().getClass(widget.getTypeIndex());
        auto* owner = cls ? findFieldOwner(cls, fieldName) : nullptr;
        if (!owner) {
            YA_CORE_ERROR("UIInstanceOverrideSet::applyTo: field '{}' does not exist on type '{}'",
                          fieldName, widget._typeId);
            bAllApplied = false;
            continue;
        }
        widget.deserializeFields(buildFieldJson(cls, owner, fieldName, value));
    }
    return bAllApplied;
}

nlohmann::json UIInstanceOverrideSet::toJson() const
{
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [fieldName, value] : fieldOverrides) {
        j[fieldName] = value;
    }
    return j;
}

UIInstanceOverrideSet UIInstanceOverrideSet::fromJson(const nlohmann::json& json)
{
    UIInstanceOverrideSet set;
    if (!json.is_object()) {
        return set;
    }
    for (auto it = json.begin(); it != json.end(); ++it) {
        set.fieldOverrides[it.key()] = it.value();
    }
    return set;
}

nlohmann::json SceneWidgetEntry::toJson() const
{
    nlohmann::json j;
    j["entryId"]   = entryId;
    j["zOrder"]    = zOrder;
    j["autoMount"] = autoMount;
    if (!documentPath.empty()) {
        j["document"] = documentPath;
    }
    else if (inlineDocument) {
        j["inline"] = inlineDocument->toJson();
    }
    else {
        YA_CORE_ERROR("SceneWidgetEntry::toJson: entry '{}' has neither document nor inline definition",
                      entryId);
    }
    j["overrides"] = overrides.toJson();
    return j;
}

SceneWidgetEntry SceneWidgetEntry::fromJson(const nlohmann::json& json)
{
    SceneWidgetEntry entry;
    if (json.contains("entryId")) {
        entry.entryId = json["entryId"].get<std::string>();
    }
    if (json.contains("zOrder")) {
        entry.zOrder = json["zOrder"].get<int32_t>();
    }
    if (json.contains("autoMount")) {
        entry.autoMount = json["autoMount"].get<bool>();
    }
    if (json.contains("document")) {
        entry.documentPath = json["document"].get<std::string>();
    }
    else if (json.contains("inline")) {
        entry.inlineDocument = UIDocument::fromJson(json["inline"]);
        if (!entry.inlineDocument) {
            YA_CORE_ERROR("SceneWidgetEntry::fromJson: entry '{}' has an invalid inline document",
                          entry.entryId);
        }
    }
    else {
        YA_CORE_ERROR("SceneWidgetEntry::fromJson: entry '{}' has neither document nor inline definition",
                      entry.entryId);
    }
    if (json.contains("overrides")) {
        entry.overrides = UIInstanceOverrideSet::fromJson(json["overrides"]);
    }
    return entry;
}

} // namespace ya
