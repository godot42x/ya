#include "GUIWorkbenchWorkspace.h"

#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <sstream>

namespace guiworkbench
{

namespace
{

std::string shortTypeName(const std::string& typeId)
{
    const size_t dot = typeId.find_last_of('.');
    return dot == std::string::npos ? typeId : typeId.substr(dot + 1);
}

/// Document root name: the reflected `_name` lives under the serialized base
/// class section (`__base__.UIElement._name`). Falls back to a recursive
/// search so schema tweaks do not break the row labels.
std::string documentRootName(const ya::UIDocument& doc)
{
    if (const auto it = doc.fields.find("_name"); it != doc.fields.end() && it->is_string()) {
        return it->get<std::string>();
    }
    if (doc.fields.contains("__base__") && doc.fields["__base__"].is_object()) {
        for (const auto& [className, value] : doc.fields["__base__"].items()) {
            if (value.is_object() && value.contains("_name") && value["_name"].is_string()) {
                return value["_name"].get<std::string>();
            }
        }
    }
    return "Widget";
}

void flattenDocument(const ya::UIDocument& doc, const std::string& path, int depth,
                     std::vector<FDocumentRow>& outRows)
{
    const std::string name = documentRootName(doc);
    outRows.push_back(FDocumentRow{
        .path   = path,
        .name   = name,
        .typeId = doc.typeId,
        .depth  = depth,
    });
    for (size_t i = 0; i < doc.children.size(); ++i) {
        const std::string childPath = path.empty() ? std::to_string(i)
                                                   : path + "." + std::to_string(i);
        flattenDocument(*doc.children[i], childPath, depth + 1, outRows);
    }
}

} // namespace

void FWorkbenchWorkspace::newDocument(const std::string& typeId)
{
    auto doc        = std::make_shared<ya::UIDocument>();
    doc->typeId     = typeId;
    doc->fields     = {
        {"__base__", {{"UIElement", {{"_name", "Untitled"}}}}},
    };
    document        = std::move(doc);
    documentPath.clear();
    selectedPath = "";
    bDirty       = false;
    commandResult = "New: " + shortTypeName(typeId);
}

bool FWorkbenchWorkspace::openDocument(const std::string& path)
{
    auto* vfs = VirtualFileSystem::get();
    if (!vfs) {
        commandResult = "Open: virtual file system unavailable";
        return false;
    }
    std::string content;
    if (!vfs->readFileToString(path, content)) {
        commandResult = "Open: cannot read " + path;
        return false;
    }
    try {
        auto parsed = ya::UIDocument::fromJson(nlohmann::json::parse(content));
        if (!parsed) {
            commandResult = "Open: malformed document " + path;
            return false;
        }
        document    = std::move(parsed);
        documentPath = path;
        selectedPath = "";
        bDirty       = false;
        commandResult = "Open: " + path;
        return true;
    }
    catch (const std::exception& e) {
        commandResult = std::string("Open: parse error - ") + e.what();
        return false;
    }
}

bool FWorkbenchWorkspace::saveDocument()
{
    if (!document) {
        commandResult = "Save: no document";
        return false;
    }
    if (documentPath.empty()) {
        commandResult = "Save: no path (use Save As)";
        return false;
    }
    return saveDocumentAs(documentPath);
}

bool FWorkbenchWorkspace::saveDocumentAs(const std::string& path)
{
    if (!document) {
        commandResult = "Save: no document";
        return false;
    }
    auto* vfs = VirtualFileSystem::get();
    if (!vfs) {
        commandResult = "Save: virtual file system unavailable";
        return false;
    }
    vfs->saveToFile(path, document->toJson().dump(4));
    if (!vfs->isFileExists(path)) {
        commandResult = "Save: failed to write " + path;
        return false;
    }
    documentPath = path;
    bDirty       = false;
    commandResult = "Save: " + path;
    return true;
}

void FWorkbenchWorkspace::closeDocument()
{
    document.reset();
    documentPath.clear();
    selectedPath.clear();
    bDirty       = false;
    commandResult = "Closed";
}

void FWorkbenchWorkspace::rebuildFromPreview(const ya::UIElement& previewRoot)
{
    if (auto rebuilt = ya::UIDocument::fromWidget(previewRoot)) {
        document = std::move(rebuilt);
    }
}

void FWorkbenchWorkspace::select(const std::string& path)
{
    if (!document) {
        return;
    }
    // "" always selects the root; other paths must exist in the document.
    if (path.empty()) {
        selectedPath = path;
        return;
    }
    const auto rows = flattenRows();
    const bool bFound = std::any_of(rows.begin(), rows.end(),
                                    [&](const FDocumentRow& row) { return row.path == path; });
    if (bFound) {
        selectedPath = path;
    }
}

void FWorkbenchWorkspace::selectRelative(int delta)
{
    if (!document || delta == 0) {
        return;
    }
    const auto rows = flattenRows();
    if (rows.empty()) {
        return;
    }
    auto it = std::find_if(rows.begin(), rows.end(),
                           [&](const FDocumentRow& row) { return row.path == selectedPath; });
    const size_t index = (it == rows.end())
                             ? 0
                             : static_cast<size_t>(std::distance(rows.begin(), it));
    const int    next  = std::clamp(static_cast<int>(index) + delta,
                                    0, static_cast<int>(rows.size()) - 1);
    if (static_cast<size_t>(next) != index) {
        selectedPath = rows[static_cast<size_t>(next)].path;
    }
}

std::vector<FDocumentRow> FWorkbenchWorkspace::flattenRows() const
{
    std::vector<FDocumentRow> rows;
    if (document) {
        flattenDocument(*document, "", 0, rows);
    }
    return rows;
}

} // namespace guiworkbench
