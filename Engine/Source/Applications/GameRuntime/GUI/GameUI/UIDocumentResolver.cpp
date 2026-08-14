#include "GameRuntime/GUI/GameUI/UIDocumentResolver.h"

#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"

namespace ya
{

std::shared_ptr<UIDocument> UIDocumentResolver::load(const std::string& path)
{
    if (const auto it = _cache.find(path); it != _cache.end()) {
        return it->second;
    }

    std::string content;
    if (!VirtualFileSystem::get() || !VirtualFileSystem::get()->readFileToString(path, content)) {
        YA_CORE_ERROR("UIDocumentResolver::load: failed to read '{}'", path);
        return nullptr;
    }
    try {
        auto document = UIDocument::fromJson(nlohmann::json::parse(content));
        if (document) {
            _cache.emplace(path, document);
        }
        else {
            YA_CORE_ERROR("UIDocumentResolver::load: invalid document '{}'", path);
        }
        return document;
    }
    catch (const std::exception& e) {
        YA_CORE_ERROR("UIDocumentResolver::load: failed to parse '{}': {}", path, e.what());
        return nullptr;
    }
}

} // namespace ya
