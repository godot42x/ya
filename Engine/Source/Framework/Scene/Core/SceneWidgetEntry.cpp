#include "Scene/Core/SceneWidgetEntry.h"

#include "Core/Log.h"
#include "Core/Reflection/ReflectionSerializer.h"

#include <functional>

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

// === Document-level reparenting (Game UI hierarchy drag-drop) ===

namespace
{

std::shared_ptr<UIDocument> resolveEntryNode(const SceneWidgetEntry& entry,
                                             const std::vector<size_t>& path,
                                             const std::function<std::shared_ptr<UIDocument>(const std::string&)>& resolveFile)
{
    std::shared_ptr<UIDocument> doc = entry.inlineDocument;
    if (!doc && !entry.documentPath.empty() && resolveFile) {
        doc = resolveFile(entry.documentPath);
    }
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

} // namespace

bool canMoveWidgetEntryDocument(std::vector<SceneWidgetEntry>& entries,
                                size_t                    srcEntryIndex,
                                const std::vector<size_t>& srcPath,
                                size_t                    dstEntryIndex,
                                const std::vector<size_t>& dstPath,
                                EWidgetEntryDropPosition  position,
                                const std::function<std::shared_ptr<UIDocument>(const std::string&)>& resolveFile)
{
    if (srcEntryIndex >= entries.size() || dstEntryIndex >= entries.size()) {
        return false;
    }
    SceneWidgetEntry& srcEntry = entries[srcEntryIndex];
    SceneWidgetEntry& dstEntry = entries[dstEntryIndex];
    const bool bSrcIsEntryRoot = srcPath.empty();
    const bool bDstIsEntryRoot = dstPath.empty();

    std::shared_ptr<UIDocument> srcDoc = resolveEntryNode(srcEntry, srcPath, resolveFile);
    std::shared_ptr<UIDocument> dstDoc = resolveEntryNode(dstEntry, dstPath, resolveFile);
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
                             EWidgetEntryDropPosition  position,
                             const std::function<std::shared_ptr<UIDocument>(const std::string&)>& resolveFile,
                             std::vector<std::string>* changedFiles)
{
    if (srcEntryIndex >= entries.size() || dstEntryIndex >= entries.size()) {
        YA_CORE_WARN("moveWidgetEntryDocument: stale entry index");
        return false;
    }
    SceneWidgetEntry& srcEntry = entries[srcEntryIndex];
    SceneWidgetEntry& dstEntry = entries[dstEntryIndex];

    const bool bSrcIsEntryRoot = srcPath.empty();
    const bool bDstIsEntryRoot = dstPath.empty();
    const bool bSrcIsFile      = !srcEntry.inlineDocument && !srcEntry.documentPath.empty();
    const bool bDstIsFile      = !dstEntry.inlineDocument && !dstEntry.documentPath.empty();
    // Capture the file paths BEFORE any mutation: `srcEntry`/`dstEntry` are
    // references into `entries` and the entry-vector erase invalidates them.
    const std::string srcFilePath = srcEntry.documentPath;
    const std::string dstFilePath = dstEntry.documentPath;

    // --- Resolve every document BEFORE any mutation (shared_ptrs survive
    // entry-vector reallocation and entry removal) ---
    std::shared_ptr<UIDocument> srcDoc = resolveEntryNode(srcEntry, srcPath, resolveFile);
    std::shared_ptr<UIDocument> dstDoc = resolveEntryNode(dstEntry, dstPath, resolveFile);
    if (!srcDoc || !dstDoc) {
        YA_CORE_WARN("moveWidgetEntryDocument: unresolvable source/target "
                     "(inline or file-resolved documents required)");
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
    if (!bSrcIsEntryRoot) {
        srcParentDoc = resolveEntryNode(srcEntry, std::vector<size_t>(srcPath.begin(), srcPath.end() - 1), resolveFile);
        srcSiblingIndex = srcPath.back();
        if (!srcParentDoc || srcSiblingIndex >= srcParentDoc->children.size()) {
            return false;
        }
    }
    std::shared_ptr<UIDocument> dstParentDoc;
    size_t dstSiblingIndex = 0;
    if (!bDstIsEntryRoot) {
        dstParentDoc = resolveEntryNode(dstEntry, std::vector<size_t>(dstPath.begin(), dstPath.end() - 1), resolveFile);
        dstSiblingIndex = dstPath.back();
        if (!dstParentDoc || dstSiblingIndex >= dstParentDoc->children.size()) {
            return false;
        }
    }

    // --- Keep the visual position for the common case: a top-level widget
    // nested into another top-level widget (both point-anchored). The child's
    // position becomes parent-relative, so subtract the parent's origin. ---
    if (bSrcIsEntryRoot && bDstIsEntryRoot && position == EWidgetEntryDropPosition::Into) {
        if (auto srcWidget = srcDoc->instantiate()) {
            if (auto dstWidget = dstDoc->instantiate()) {
                if (srcWidget->_anchorMin == glm::vec2(0.0f) && srcWidget->_anchorMax == glm::vec2(0.0f) &&
                    dstWidget->_anchorMin == glm::vec2(0.0f) && dstWidget->_anchorMax == glm::vec2(0.0f)) {
                    srcWidget->setPosition(srcWidget->getPosition() - dstWidget->getPosition());
                    if (auto adjusted = UIDocument::fromWidget(*srcWidget)) {
                        srcDoc = std::move(adjusted);
                    }
                }
            }
        }
    }

    // --- Detach the source ---
    if (bSrcIsEntryRoot) {
        SceneWidgetEntry srcEntryCopy = std::move(entries[srcEntryIndex]);
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
        srcParentDoc->children.erase(srcParentDoc->children.begin() + srcSiblingIndex);
    }

    // --- Attach under the target ---
    if (position == EWidgetEntryDropPosition::Into) {
        dstDoc->children.push_back(srcDoc);
    }
    else {
        const size_t insertAt = dstSiblingIndex + (position == EWidgetEntryDropPosition::After ? 1 : 0);
        dstParentDoc->children.insert(dstParentDoc->children.begin() + insertAt, srcDoc);
    }

    // Report file-backed documents that changed so the caller can persist
    // them (and invalidate resolvers): the destination gained a child/sibling,
    // and a nested source removed a node from its file.
    if (changedFiles) {
        if (bDstIsFile && !dstFilePath.empty()) {
            changedFiles->push_back(dstFilePath);
        }
        if (bSrcIsFile && !bSrcIsEntryRoot && !srcFilePath.empty()) {
            changedFiles->push_back(srcFilePath);
        }
    }
    return true;
}

} // namespace ya
