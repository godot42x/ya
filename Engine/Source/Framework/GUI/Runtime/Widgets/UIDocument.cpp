#include "GUI/Widgets/UIDocument.h"

#include "Core/Log.h"
#include "Core/Reflection/ReflectionSerializer.h"

#include "GUI/Widgets/UITypeRegistry.h"
#include "GUI/Layout/UILayout.h"

#include <limits>

namespace
{
using namespace ya;

nlohmann::json marginJson(const FMargin& m)
{
    return {{"left", m.left}, {"top", m.top}, {"right", m.right}, {"bottom", m.bottom}};
}

FMargin marginFromJson(const nlohmann::json& j)
{
    return FMargin(j.value("left", 0.0f), j.value("top", 0.0f),
                   j.value("right", 0.0f), j.value("bottom", 0.0f));
}

nlohmann::json vecJson(glm::vec2 v) { return {v.x, v.y}; }
glm::vec2 vecFromJson(const nlohmann::json& j, glm::vec2 d = {})
{
    return j.is_array() && j.size() == 2 ? glm::vec2(j[0].get<float>(), j[1].get<float>()) : d;
}

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
    if (auto* s = slot.as<const UICanvasSlot>()) {
        j["type"] = "canvas"; j["anchorMin"] = vecJson(s->getAnchorMin());
        j["anchorMax"] = vecJson(s->getAnchorMax()); j["offset"] = vecJson(s->getOffset());
        j["minSize"] = vecJson(s->getMinSize()); j["maxSize"] = vecJson(s->getMaxSize());
        j["offsets"] = marginJson(s->getOffsets()); j["alignmentH"] = static_cast<int>(s->getAlignmentH());
        j["alignmentV"] = static_cast<int>(s->getAlignmentV()); j["widthSizeMode"] = static_cast<int>(s->getWidthSizeMode());
        j["heightSizeMode"] = static_cast<int>(s->getHeightSizeMode()); j["pivot"] = vecJson(s->getPivot());
        j["preferredSize"] = vecJson(s->getPreferredSize()); j["fixedSize"] = vecJson(s->getFixedSize());
    } else if (auto* s = slot.as<const UIBoxSlot>()) {
        j["type"] = "box"; j["sizeRule"] = static_cast<int>(s->getSizeRule()); j["weight"] = s->getWeight();
        j["margin"] = marginJson(s->getMargin()); j["crossAlignment"] = static_cast<int>(s->getCrossAlignment());
        j["minSize"] = vecJson(s->getMinSize()); j["maxSize"] = vecJson(s->getMaxSize()); j["preferredSize"] = vecJson(s->getPreferredSize());
        j["participatesInLayout"] = s->participatesInLayout(); j["reserveSpaceWhenHidden"] = s->reservesSpaceWhenHidden();
    } else if (auto* s = slot.as<const UIOverlaySlot>()) {
        j["type"] = "overlay"; j["hAlign"] = static_cast<int>(s->getHAlign()); j["vAlign"] = static_cast<int>(s->getVAlign());
        j["padding"] = marginJson(s->getPadding()); j["preferredSize"] = vecJson(s->getPreferredSize());
    } else if (auto* s = slot.as<const UISingleChildSlot>()) {
        j["type"] = "singleChild"; j["hAlign"] = static_cast<int>(s->getHAlign()); j["vAlign"] = static_cast<int>(s->getVAlign());
        j["preferredSize"] = vecJson(s->getPreferredSize());
    } else if (auto* s = slot.as<const UITableSlot>()) {
        j["type"] = "table"; j["row"] = s->getRow(); j["column"] = s->getColumn();
    } else {
        j["type"] = "base";
    }
    return j;
}

void applySlotJson(UISlot& slot, const nlohmann::json& j)
{
    if (!j.is_object()) return;
    if (auto* s = slot.as<UICanvasSlot>()) { FCanvasSlotArgs a; a.anchorMin=vecFromJson(j["anchorMin"]); a.anchorMax=vecFromJson(j["anchorMax"]); a.offset=vecFromJson(j["offset"]); a.minSize=vecFromJson(j["minSize"]); a.maxSize=vecFromJson(j["maxSize"], a.maxSize); a.offsets=marginFromJson(j["offsets"]); a.alignmentH=static_cast<EWidgetAlignH>(j.value("alignmentH",0)); a.alignmentV=static_cast<EWidgetAlignV>(j.value("alignmentV",0)); a.widthSizeMode=static_cast<EWidgetSizeMode>(j.value("widthSizeMode",0)); a.heightSizeMode=static_cast<EWidgetSizeMode>(j.value("heightSizeMode",0)); a.pivot=vecFromJson(j["pivot"]); a.preferredSize=vecFromJson(j["preferredSize"]); a.fixedSize=vecFromJson(j["fixedSize"]); s->apply(a); }
    else if (auto* s = slot.as<UIBoxSlot>()) { FBoxSlotArgs a; a.sizeRule=static_cast<EUIBoxSlotSizeRule>(j.value("sizeRule",0)); a.weight=j.value("weight",1.0f); a.margin=marginFromJson(j["margin"]); a.crossAlignment=static_cast<EUIBoxSlotCrossAlignment>(j.value("crossAlignment",0)); a.preferredSize=vecFromJson(j["preferredSize"]); s->apply(a); s->setMinSize(vecFromJson(j["minSize"])); s->setMaxSize(vecFromJson(j["maxSize"], s->getMaxSize())); s->setParticipatesInLayout(j.value("participatesInLayout",true)); s->setReserveSpaceWhenHidden(j.value("reserveSpaceWhenHidden",true)); }
    else if (auto* s = slot.as<UIOverlaySlot>()) { FOverlaySlotArgs a; a.hAlign=static_cast<EUIOverlayAlignment>(j.value("hAlign",0)); a.vAlign=static_cast<EUIOverlayAlignment>(j.value("vAlign",0)); a.padding=marginFromJson(j["padding"]); a.preferredSize=vecFromJson(j["preferredSize"]); s->apply(a); }
    else if (auto* s = slot.as<UISingleChildSlot>()) { FSingleChildSlotArgs a; a.hAlign=static_cast<EUIOverlayAlignment>(j.value("hAlign",0)); a.vAlign=static_cast<EUIOverlayAlignment>(j.value("vAlign",0)); a.preferredSize=vecFromJson(j["preferredSize"]); s->apply(a); }
    else if (auto* s = slot.as<UITableSlot>()) s->setCell(j.value("row",0), j.value("column",0));
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
