#include "GameEditor/UI/Shell/EditorContentOpenerRegistry.h"

#include "Core/System/VirtualFileSystem.h"
#include "GameEditor/EditorLayer.h"

namespace ya
{

namespace
{

[[nodiscard]] char asciiLower(unsigned char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return static_cast<char>(ch - 'A' + 'a');
    }
    return static_cast<char>(ch);
}

[[nodiscard]] std::string normalizeExtension(std::string_view extension)
{
    std::string normalized;
    normalized.reserve(extension.size() + 1);
    if (extension.empty() || extension.front() != '.') {
        normalized.push_back('.');
    }
    for (char ch : extension) {
        normalized.push_back(asciiLower(static_cast<unsigned char>(ch)));
    }
    if (normalized.size() <= 1) {
        return {};
    }
    return normalized;
}

[[nodiscard]] bool endsWithIgnoreCase(std::string_view path, std::string_view lowerSuffix)
{
    if (lowerSuffix.empty() || path.size() < lowerSuffix.size()) {
        return false;
    }
    const std::string_view tail = path.substr(path.size() - lowerSuffix.size());
    for (size_t i = 0; i < lowerSuffix.size(); ++i) {
        if (asciiLower(static_cast<unsigned char>(tail[i])) != lowerSuffix[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

EditorContentOpenerRegistry& EditorContentOpenerRegistry::get()
{
    static EditorContentOpenerRegistry registry = [] {
        EditorContentOpenerRegistry created;
        registerBuiltinContentOpeners(created);
        return created;
    }();
    return registry;
}

void EditorContentOpenerRegistry::registerOpener(std::string extension, EditorContentOpener opener)
{
    if (!opener) {
        return;
    }
    std::string key = normalizeExtension(extension);
    if (key.empty()) {
        return;
    }
    for (FEditorContentOpener& entry : _openers) {
        if (entry.extension == key) {
            entry.open = std::move(opener);
            return;
        }
    }
    _openers.push_back(FEditorContentOpener{
        .extension = std::move(key),
        .open      = std::move(opener),
    });
}

const EditorContentOpener* EditorContentOpenerRegistry::find(std::string_view utf8Path) const
{
    const FEditorContentOpener* best = nullptr;
    for (const FEditorContentOpener& entry : _openers) {
        if (!endsWithIgnoreCase(utf8Path, entry.extension)) {
            continue;
        }
        if (best == nullptr || entry.extension.size() > best->extension.size()) {
            best = &entry;
        }
    }
    if (best == nullptr) {
        return nullptr;
    }
    return &best->open;
}

void registerBuiltinContentOpeners(EditorContentOpenerRegistry& registry)
{
    registry.registerOpener(".scene.json", [](EditorLayer& layer, std::string utf8Path) {
        layer.cmdLoadScene(std::move(utf8Path));
    });
    registry.registerOpener(".lua", [](EditorLayer& layer, std::string utf8Path) {
        (void)layer.openDocumentEditor(EEditorDocumentKind::Script, std::move(utf8Path));
    });
    registry.registerOpener(".yaui.json", [](EditorLayer& layer, std::string utf8Path) {
        // One funnel for every UI entry (content browser, inspector, hierarchy
        // context menu): it loads the designer session, binds the UI page and
        // raises the tab.
        std::string assetPath = utf8Path;
        if (VirtualFileSystem* vfs = VirtualFileSystem::get()) {
            const std::string vfsPath = vfs->toVfsPath(assetPath);
            if (!vfsPath.empty()) {
                assetPath = vfsPath;
            }
        }
        (void)layer.openDocumentEditor(EEditorDocumentKind::UI, std::move(assetPath));
    });

    const EditorContentOpener openMaterial = [](EditorLayer& layer, std::string utf8Path) {
        (void)layer.openDocumentEditor(EEditorDocumentKind::Material, std::move(utf8Path));
    };
    registry.registerOpener(".mat", openMaterial);
    registry.registerOpener(".material", openMaterial);
}

} // namespace ya
