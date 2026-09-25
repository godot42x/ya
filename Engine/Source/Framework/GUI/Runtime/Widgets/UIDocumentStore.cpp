#include "GUI/Widgets/UIDocumentStore.h"

#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"

#include <format>

namespace ya
{

std::shared_ptr<UIDocument> UIDocumentStore::resolve(std::string_view path)
{
    if (path.empty()) {
        YA_CORE_ERROR("UIDocumentStore::resolve: empty document path");
        return nullptr;
    }

    const std::string key(path);
    if (const auto it = _documents.find(key); it != _documents.end()) {
        return it->second;
    }

    VirtualFileSystem* vfs = VirtualFileSystem::get();
    if (!vfs) {
        YA_CORE_ERROR("UIDocumentStore::resolve: no virtual file system mounted for '{}'", key);
        return nullptr;
    }

    std::string text;
    if (!vfs->readFileToString(key, text) || text.empty()) {
        YA_CORE_ERROR("UIDocumentStore::resolve: cannot read Game UI document '{}'", key);
        return nullptr;
    }

    nlohmann::json json;
    try {
        json = nlohmann::json::parse(text);
    }
    catch (const nlohmann::json::exception& error) {
        YA_CORE_ERROR("UIDocumentStore::resolve: '{}' is not valid JSON: {}", key, error.what());
        return nullptr;
    }

    std::shared_ptr<UIDocument> document = UIDocument::fromJson(json);
    if (!document) {
        YA_CORE_ERROR("UIDocumentStore::resolve: '{}' is not a valid Game UI document", key);
        return nullptr;
    }

    _documents.emplace(key, document);
    _revisions[key] = _nextRevision++;
    return document;
}

void UIDocumentStore::put(std::string_view path, std::shared_ptr<UIDocument> document)
{
    if (path.empty()) {
        YA_CORE_ERROR("UIDocumentStore::put: empty document path");
        return;
    }
    if (!document) {
        _documents.erase(std::string(path));
        _revisions[std::string(path)] = _nextRevision++;
        return;
    }
    const std::string key(path);
    _documents[key] = std::move(document);
    _revisions[key] = _nextRevision++;
}

std::shared_ptr<UIDocument> UIDocumentStore::find(std::string_view path) const
{
    if (path.empty()) {
        return nullptr;
    }
    const auto it = _documents.find(std::string(path));
    return it == _documents.end() ? nullptr : it->second;
}

uint64_t UIDocumentStore::revision(std::string_view path) const
{
    if (path.empty()) {
        return 0;
    }
    const auto it = _revisions.find(std::string(path));
    return it == _revisions.end() ? 0 : it->second;
}

bool UIDocumentStore::save(std::string_view path)
{
    if (path.empty()) {
        YA_CORE_ERROR("UIDocumentStore::save: empty document path");
        return false;
    }
    const std::shared_ptr<UIDocument> document = find(path);
    if (!document) {
        YA_CORE_ERROR("UIDocumentStore::save: no live document for '{}'", path);
        return false;
    }
    VirtualFileSystem* vfs = VirtualFileSystem::get();
    if (!vfs) {
        YA_CORE_ERROR("UIDocumentStore::save: no virtual file system mounted for '{}'", path);
        return false;
    }

    const std::string text = document->toJson().dump(2);
    vfs->saveToFile(path, text);
    YA_CORE_INFO("UIDocumentStore: saved Game UI document '{}'", path);
    return true;
}

} // namespace ya
