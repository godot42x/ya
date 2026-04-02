#include "ConfigManager.h"

#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"

namespace ya
{

ConfigManager& ConfigManager::get()
{
    static ConfigManager instance;
    return instance;
}

void ConfigManager::init()
{
    if (_initialized) {
        return;
    }
    _initialized = true;
}

void ConfigManager::shutdown()
{
    flushAll();
    _documents.clear();
    _initialized = false;
}

void ConfigManager::flushAll()
{
    for (auto& [name, doc] : _documents) {
        (void)name;
        if (!doc.dirty || doc.path.empty()) {
            continue;
        }

        if (auto* vfs = VirtualFileSystem::get()) {
            vfs->saveToFile(doc.path, doc.root.dump(4));
            doc.dirty = false;
        }
    }
}

void ConfigManager::markDirty(const std::string& docName)
{
    if (auto* doc = findDocument(docName)) {
        doc->dirty = true;
    }
}

ConfigManager::Document& ConfigManager::openDocument(const std::string& name, const std::string& path, OpenDocumentOptions options)
{
    Document& doc = _documents[name];
    if (doc.path == path && doc.isLoaded()) {
        return doc;
    }

    doc.path      = path;
    doc.root      = nlohmann::json::object();
    doc.dirty     = false;
    doc.bReadOnly = options.bReadOnly;

    bool bLoadedFromDisk = false;
    if (auto* vfs = VirtualFileSystem::get()) {
        std::string content;
        if (vfs->readFileToString(path, content) && !content.empty()) {
            try {
                doc.root = nlohmann::json::parse(content, nullptr, true, true);
                bLoadedFromDisk = true;
            }
            catch (const nlohmann::json::exception& e) {
                YA_CORE_WARN("ConfigManager: failed to parse '{}': {}", path, e.what());
                doc.root = nlohmann::json::object();
            }
        }
    }

    if (!doc.root.is_object()) {
        doc.root = nlohmann::json::object();
    }

    // Persist an empty/default document on first run so Saved/Config has a stable anchor.
    doc.dirty = !bLoadedFromDisk && options.bPersistIfMissing && !doc.bReadOnly;
    if (doc.dirty) {
        flushDocument(name);
    }

    return doc;
}

bool ConfigManager::closeDocument(const std::string& name, bool bFlush)
{
    auto it = _documents.find(name);
    if (it == _documents.end()) {
        return false;
    }

    if (bFlush && it->second.dirty && !it->second.path.empty()) {
        if (auto* vfs = VirtualFileSystem::get()) {
            vfs->saveToFile(it->second.path, it->second.root.dump(4));
        }
    }

    _documents.erase(it);
    return true;
}

bool ConfigManager::hasDocument(const std::string& name) const
{
    return _documents.contains(name);
}

ConfigManager::Document* ConfigManager::findDocument(const std::string& name)
{
    auto it = _documents.find(name);
    return it == _documents.end() ? nullptr : &it->second;
}

const ConfigManager::Document* ConfigManager::findDocument(const std::string& name) const
{
    auto it = _documents.find(name);
    return it == _documents.end() ? nullptr : &it->second;
}

bool ConfigManager::hasValue(const std::string& docName, std::string_view key) const
{
    return findNode(docName, key) != nullptr;
}

void ConfigManager::removeValue(const std::string& docName, std::string_view key)
{
    Document* doc = findDocument(docName);
    if (!doc || !doc->root.is_object()) {
        return;
    }

    const nlohmann::json::json_pointer pointer = toJsonPointer(key);
    const std::string                 pointerStr = pointer.to_string();
    const size_t lastSlash = pointerStr.find_last_of('/');
    if (lastSlash == std::string::npos || lastSlash == 0) {
        std::string keyName = pointerStr.substr(1);
        if (!doc->root.contains(keyName)) {
            return;
        }
        doc->root.erase(keyName);
        doc->dirty = true;
        return;
    }

    nlohmann::json::json_pointer parentPointer(pointerStr.substr(0, lastSlash));
    if (!doc->root.contains(parentPointer)) {
        return;
    }

    auto& parent = doc->root.at(parentPointer);
    if (!parent.is_object()) {
        return;
    }

    std::string keyName = pointerStr.substr(lastSlash + 1);
    if (!parent.contains(keyName)) {
        return;
    }

    parent.erase(keyName);
    doc->dirty = true;
}

bool ConfigManager::pruneEmptyParents(const std::string& docName, std::string_view key)
{
    Document* doc = findDocument(docName);
    if (!doc || !doc->root.is_object()) {
        return false;
    }

    const std::string pointerStr = toJsonPointer(key).to_string();
    if (pointerStr.empty() || pointerStr == "/") {
        return false;
    }

    bool         removedAny  = false;
    std::string  currentPath = pointerStr;
    const size_t leafSlash   = currentPath.find_last_of('/');
    if (leafSlash == std::string::npos || leafSlash == 0) {
        return false;
    }

    currentPath.resize(leafSlash);
    while (!currentPath.empty()) {
        nlohmann::json::json_pointer currentPointer(currentPath);
        if (!doc->root.contains(currentPointer)) {
            break;
        }

        auto& currentNode = doc->root.at(currentPointer);
        if (!currentNode.is_object() || !currentNode.empty()) {
            break;
        }

        const size_t parentSlash = currentPath.find_last_of('/');
        if (parentSlash == std::string::npos) {
            break;
        }

        if (parentSlash == 0) {
            const std::string rootKey = currentPath.substr(1);
            if (doc->root.contains(rootKey)) {
                doc->root.erase(rootKey);
                removedAny = true;
            }
            break;
        }

        const std::string            parentPath = currentPath.substr(0, parentSlash);
        nlohmann::json::json_pointer parentPointer(parentPath);
        if (!doc->root.contains(parentPointer)) {
            break;
        }

        auto& parentNode = doc->root.at(parentPointer);
        if (!parentNode.is_object()) {
            break;
        }

        std::string keyName = currentPath.substr(parentSlash + 1);
        if (!parentNode.contains(keyName)) {
            break;
        }

        parentNode.erase(keyName);
        removedAny  = true;
        currentPath = parentPath;
    }

    if (removedAny) {
        doc->dirty = true;
    }
    return removedAny;
}

bool ConfigManager::flushDocument(const std::string& docName)
{
    auto* doc = findDocument(docName);
    if (!doc || !doc->dirty || doc->path.empty() || doc->bReadOnly) {
        return false;
    }

    if (auto* vfs = VirtualFileSystem::get()) {
        vfs->saveToFile(doc->path, doc->root.dump(4));
        doc->dirty = false;
        return true;
    }

    return false;
}

const nlohmann::json* ConfigManager::findNode(const std::string& docName, std::string_view key) const
{
    const Document* doc = findDocument(docName);
    if (!doc || !doc->root.is_object()) {
        return nullptr;
    }

    const nlohmann::json::json_pointer pointer = toJsonPointer(key);
    if (!doc->root.contains(pointer)) {
        return nullptr;
    }
    return &doc->root.at(pointer);
}

nlohmann::json* ConfigManager::ensureNode(const std::string& docName, std::string_view key)
{
    Document* doc = findDocument(docName);
    if (!doc) {
        return nullptr;
    }

    if (!doc->root.is_object()) {
        doc->root = nlohmann::json::object();
    }

    const nlohmann::json::json_pointer pointer = toJsonPointer(key);
    return &doc->root[pointer];
}

nlohmann::json::json_pointer ConfigManager::toJsonPointer(std::string_view key)
{
    std::string pointer;
    pointer.reserve(key.size() + 1);
    pointer.push_back('/');

    for (char ch : key) {
        if (ch == '.') {
            pointer.push_back('/');
            continue;
        }
        if (ch == '~') {
            pointer += "~0";
            continue;
        }
        if (ch == '/') {
            pointer += "~1";
            continue;
        }
        pointer.push_back(ch);
    }

    return nlohmann::json::json_pointer(pointer);
}

} // namespace ya
