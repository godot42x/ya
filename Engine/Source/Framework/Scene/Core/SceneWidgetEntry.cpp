#include "Scene/Core/SceneWidgetEntry.h"

#include "Core/Log.h"
#include "Core/Reflection/ReflectionSerializer.h"

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
    if (inlineDocument) {
        j["inline"] = inlineDocument->toJson();
        j["rootSlot"] = canvasSlotToJson(rootSlot);
    }
    else {
        YA_CORE_ERROR("SceneWidgetEntry::toJson: entry '{}' has no inline document definition",
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
    if (json.contains("inline")) {
        entry.inlineDocument = UIDocument::fromJson(json["inline"]);
        if (!entry.inlineDocument) {
            YA_CORE_ERROR("SceneWidgetEntry::fromJson: entry '{}' has an invalid inline document",
                          entry.entryId);
        }
    }
    else {
        YA_CORE_ERROR("SceneWidgetEntry::fromJson: entry '{}' has no inline document definition",
                      entry.entryId);
    }
    if (!json.contains("rootSlot") || !canvasSlotFromJson(json["rootSlot"], entry.rootSlot)) {
        YA_CORE_ERROR("SceneWidgetEntry::fromJson: entry '{}' has no valid rootSlot", entry.entryId);
        entry.inlineDocument.reset();
        return entry;
    }
    if (json.contains("overrides")) {
        entry.overrides = UIInstanceOverrideSet::fromJson(json["overrides"]);
    }
    return entry;
}

// === Document-level reparenting (Game UI hierarchy drag-drop) ===

namespace
{

std::shared_ptr<UIDocument> resolveEntryNode(const SceneWidgetEntry& entry,
                                             const std::vector<size_t>& path)
{
    std::shared_ptr<UIDocument> doc = entry.inlineDocument;
    for (const size_t index : path) {
        if (!doc || index >= doc->children.size()) {
            return nullptr;
        }
        doc = doc->children[index];
    }
    return doc;
}

bool documentContains(const std::shared_ptr<UIDocument>& root, const UIDocument* probe)
{
    if (!root || !probe) {
        return false;
    }
    if (root.get() == probe) {
        return true;
    }
    for (const auto& child : root->children) {
        if (documentContains(child, probe)) {
            return true;
        }
    }
    return false;
}

struct FDocumentChildEdge
{
    std::shared_ptr<UIDocument> document;
    nlohmann::json slot;
};

FDocumentChildEdge takeChildEdge(UIDocument& parent, size_t index)
{
    FDocumentChildEdge edge;
    edge.document = std::move(parent.children[index]);
    if (index < parent.childSlots.size()) {
        edge.slot = std::move(parent.childSlots[index]);
    }
    parent.children.erase(parent.children.begin() + static_cast<std::ptrdiff_t>(index));
    if (index < parent.childSlots.size()) {
        parent.childSlots.erase(parent.childSlots.begin() + static_cast<std::ptrdiff_t>(index));
    }
    return edge;
}

void insertChildEdge(UIDocument& parent, size_t index, FDocumentChildEdge edge)
{
    const size_t at = std::min(index, parent.children.size());
    parent.children.insert(parent.children.begin() + static_cast<std::ptrdiff_t>(at),
                           std::move(edge.document));
    const nlohmann::json slot = edge.slot.is_object() ? std::move(edge.slot) : nlohmann::json{};
    if (parent.childSlots.size() < at) {
        parent.childSlots.resize(at);
    }
    parent.childSlots.insert(parent.childSlots.begin() + static_cast<std::ptrdiff_t>(at), slot);
}

} // namespace

bool canMoveWidgetEntryDocument(std::vector<SceneWidgetEntry>& entries,
                                size_t                    srcEntryIndex,
                                const std::vector<size_t>& srcPath,
                                size_t                    dstEntryIndex,
                                const std::vector<size_t>& dstPath,
                                EWidgetEntryDropPosition  position)
{
    if (srcEntryIndex >= entries.size() || dstEntryIndex >= entries.size()) {
        return false;
    }
    SceneWidgetEntry& srcEntry = entries[srcEntryIndex];
    SceneWidgetEntry& dstEntry = entries[dstEntryIndex];
    const bool bSrcIsEntryRoot = srcPath.empty();
    const bool bDstIsEntryRoot = dstPath.empty();

    std::shared_ptr<UIDocument> srcDoc = resolveEntryNode(srcEntry, srcPath);
    std::shared_ptr<UIDocument> dstDoc = resolveEntryNode(dstEntry, dstPath);
    if (!srcDoc || !dstDoc) {
        return false;
    }
    if (srcDoc.get() == dstDoc.get()) {
        return false; // self-drop / no-op: nothing meaningful to do
    }
    if (bSrcIsEntryRoot && bDstIsEntryRoot && srcEntryIndex == dstEntryIndex) {
        return false; // dropping an entry onto its own row
    }
    if (documentContains(srcDoc, dstDoc.get())) {
        return false; // cycle: target lives inside the dragged subtree
    }
    if (!bSrcIsEntryRoot && bDstIsEntryRoot && position != EWidgetEntryDropPosition::Into) {
        return false; // nested widgets cannot become top-level entries via Before/After
    }
    return true;
}

bool moveWidgetEntryDocument(std::vector<SceneWidgetEntry>& entries,
                             size_t                    srcEntryIndex,
                             const std::vector<size_t>& srcPath,
                             size_t                    dstEntryIndex,
                             const std::vector<size_t>& dstPath,
                             EWidgetEntryDropPosition  position)
{
    if (srcEntryIndex >= entries.size() || dstEntryIndex >= entries.size()) {
        YA_CORE_WARN("moveWidgetEntryDocument: stale entry index");
        return false;
    }
    SceneWidgetEntry& srcEntry = entries[srcEntryIndex];
    SceneWidgetEntry& dstEntry = entries[dstEntryIndex];

    const bool bSrcIsEntryRoot = srcPath.empty();
    const bool bDstIsEntryRoot = dstPath.empty();

    // --- Resolve every document BEFORE any mutation (shared_ptrs survive
    // entry-vector reallocation and entry removal) ---
    std::shared_ptr<UIDocument> srcDoc = resolveEntryNode(srcEntry, srcPath);
    std::shared_ptr<UIDocument> dstDoc = resolveEntryNode(dstEntry, dstPath);
    if (!srcDoc || !dstDoc) {
        YA_CORE_WARN("moveWidgetEntryDocument: unresolvable source/target "
                     "(inline documents required)");
        return false;
    }
    if (srcDoc.get() == dstDoc.get()) {
        return true; // no-op
    }
    if (bSrcIsEntryRoot && bDstIsEntryRoot && srcEntryIndex == dstEntryIndex) {
        return true; // no-op
    }
    if (documentContains(srcDoc, dstDoc.get())) {
        YA_CORE_WARN("moveWidgetEntryDocument: cannot drop into the dragged subtree");
        return false;
    }
    // A nested widget cannot become a top-level entry via Before/After.
    if (!bSrcIsEntryRoot && bDstIsEntryRoot && position != EWidgetEntryDropPosition::Into) {
        YA_CORE_WARN("moveWidgetEntryDocument: a nested widget can only be dropped Into an entry");
        return false;
    }

    std::shared_ptr<UIDocument> srcParentDoc;
    size_t srcSiblingIndex = 0;
    FDocumentChildEdge movedEdge;
    bool bMovedNestedEdge = false;
    nlohmann::json movedRootSlot;
    if (!bSrcIsEntryRoot) {
        srcParentDoc = resolveEntryNode(srcEntry, std::vector<size_t>(srcPath.begin(), srcPath.end() - 1));
        srcSiblingIndex = srcPath.back();
        if (!srcParentDoc || srcSiblingIndex >= srcParentDoc->children.size()) {
            return false;
        }
    }
    std::shared_ptr<UIDocument> dstParentDoc;
    size_t dstSiblingIndex = 0;
    if (!bDstIsEntryRoot) {
        dstParentDoc = resolveEntryNode(dstEntry, std::vector<size_t>(dstPath.begin(), dstPath.end() - 1));
        dstSiblingIndex = dstPath.back();
        if (!dstParentDoc || dstSiblingIndex >= dstParentDoc->children.size()) {
            return false;
        }
    }

    // --- Detach the source ---
    if (bSrcIsEntryRoot) {
        SceneWidgetEntry srcEntryCopy = std::move(entries[srcEntryIndex]);
        movedRootSlot = canvasSlotToJson(srcEntryCopy.rootSlot);
        entries.erase(entries.begin() + srcEntryIndex);
        if (bDstIsEntryRoot && position != EWidgetEntryDropPosition::Into) {
            // Plain reorder at the entry level (Before/After on entry rows).
            const size_t dstIdx   = dstEntryIndex > srcEntryIndex ? dstEntryIndex - 1 : dstEntryIndex;
            const size_t insertAt = dstIdx + (position == EWidgetEntryDropPosition::After ? 1 : 0);
            entries.insert(entries.begin() + std::min(insertAt, entries.size()), std::move(srcEntryCopy));
            return true;
        }
        (void)srcEntryCopy; // the document moved into the target; the entry is gone
    }
    else {
        movedEdge = takeChildEdge(*srcParentDoc, srcSiblingIndex);
        bMovedNestedEdge = true;
    }

    // --- Attach under the target ---
    if (position == EWidgetEntryDropPosition::Into) {
        if (bMovedNestedEdge) {
            dstDoc->children.push_back(std::move(movedEdge.document));
            dstDoc->childSlots.push_back(std::move(movedEdge.slot));
        }
        else {
            dstDoc->children.push_back(srcDoc);
            dstDoc->childSlots.push_back(std::move(movedRootSlot));
        }
    }
    else {
        const size_t insertAt = dstSiblingIndex + (position == EWidgetEntryDropPosition::After ? 1 : 0);
        if (bMovedNestedEdge) {
            insertChildEdge(*dstParentDoc, insertAt, std::move(movedEdge));
        }
        else {
            dstParentDoc->children.insert(dstParentDoc->children.begin() + insertAt, srcDoc);
            dstParentDoc->childSlots.insert(dstParentDoc->childSlots.begin() + insertAt, nlohmann::json{});
        }
    }

    return true;
}

} // namespace ya
