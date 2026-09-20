#include "Scene/Core/SceneWidgetEntry.h"

#include "Core/Log.h"
#include "Core/Reflection/ReflectionSerializer.h"

#include "GUI/Widgets/UIElement.h"

#include <functional>

namespace
{
using namespace ya;

nlohmann::json vec2Json(glm::vec2 value) { return {value.x, value.y}; }
glm::vec2 vec2FromJson(const nlohmann::json& value, glm::vec2 fallback)
{
    return value.is_array() && value.size() == 2
               ? glm::vec2(value[0].get<float>(), value[1].get<float>())
               : fallback;
}
nlohmann::json marginJson(const FMargin& value)
{
    return {{"left", value.left}, {"top", value.top},
            {"right", value.right}, {"bottom", value.bottom}};
}
FMargin marginFromJson(const nlohmann::json& value, FMargin fallback = {})
{
    return value.is_object()
               ? FMargin(value.value("left", fallback.left), value.value("top", fallback.top),
                         value.value("right", fallback.right), value.value("bottom", fallback.bottom))
               : fallback;
}
nlohmann::json canvasSlotToJson(const FCanvasSlotArgs& args)
{
    return {{"type", "canvas"},
            {"anchorMin", vec2Json(args.anchorMin)},
            {"anchorMax", vec2Json(args.anchorMax)},
            {"offset", vec2Json(args.offset)},
            {"minSize", vec2Json(args.minSize)},
            {"maxSize", vec2Json(args.maxSize)},
            {"offsets", marginJson(args.offsets)},
            {"alignmentH", static_cast<int>(args.alignmentH)},
            {"alignmentV", static_cast<int>(args.alignmentV)},
            {"widthSizeMode", static_cast<int>(args.widthSizeMode)},
            {"heightSizeMode", static_cast<int>(args.heightSizeMode)},
            {"pivot", vec2Json(args.pivot)},
            {"preferredSize", vec2Json(args.preferredSize)},
            {"fixedSize", vec2Json(args.fixedSize)}};
}
bool canvasSlotFromJson(const nlohmann::json& value, FCanvasSlotArgs& args)
{
    if (!value.is_object() || value.value("type", std::string{}) != "canvas") return false;
    args.anchorMin = vec2FromJson(value["anchorMin"], args.anchorMin);
    args.anchorMax = vec2FromJson(value["anchorMax"], args.anchorMax);
    args.offset = vec2FromJson(value["offset"], args.offset);
    args.minSize = vec2FromJson(value["minSize"], args.minSize);
    args.maxSize = vec2FromJson(value["maxSize"], args.maxSize);
    args.offsets = marginFromJson(value["offsets"], args.offsets);
    args.alignmentH = static_cast<EWidgetAlignH>(value.value("alignmentH", static_cast<int>(args.alignmentH)));
    args.alignmentV = static_cast<EWidgetAlignV>(value.value("alignmentV", static_cast<int>(args.alignmentV)));
    args.widthSizeMode = static_cast<EWidgetSizeMode>(value.value("widthSizeMode", static_cast<int>(args.widthSizeMode)));
    args.heightSizeMode = static_cast<EWidgetSizeMode>(value.value("heightSizeMode", static_cast<int>(args.heightSizeMode)));
    args.pivot = vec2FromJson(value["pivot"], args.pivot);
    args.preferredSize = vec2FromJson(value["preferredSize"], args.preferredSize);
    args.fixedSize = vec2FromJson(value["fixedSize"], args.fixedSize);
    return true;
}
}

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
        const Property* prop = owner->getProperty(fieldName);
        if (!prop || !prop->metadata.hasFlag(FieldFlags::InstanceEditable)) {
            YA_CORE_ERROR("UIInstanceOverrideSet::applyTo: field '{}' on type '{}' is not "
                          "InstanceEditable; entry overrides only allow instance-editable fields",
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
    else {
        YA_CORE_ERROR("SceneWidgetEntry::toJson: entry '{}' has no document path", entryId);
    }
    j["rootSlot"] = canvasSlotToJson(rootSlot);
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
    else {
        YA_CORE_ERROR("SceneWidgetEntry::fromJson: entry '{}' has no document path", entry.entryId);
    }
    if (!json.contains("rootSlot") || !canvasSlotFromJson(json["rootSlot"], entry.rootSlot)) {
        YA_CORE_ERROR("SceneWidgetEntry::fromJson: entry '{}' has no valid rootSlot", entry.entryId);
        entry.documentPath.clear();
        return entry;
    }
    if (json.contains("overrides")) {
        entry.overrides = UIInstanceOverrideSet::fromJson(json["overrides"]);
    }
    return entry;
}

} // namespace ya
