#include "GameEditor/Localization/EditorLocalization.h"

#include "Core/Log.h"

#include <format>

namespace ya
{
namespace EditorLocalization
{

namespace
{
/// Editor chrome tables ship with the engine (not project content), so the
/// path is workspace-relative and resolves through the VFS working root.
std::string editorTablePath(std::string_view language)
{
    return std::format("Engine/Content/Locale/editor.{}.json", language);
}
} // namespace

void loadEditorTables(std::string_view language)
{
    if (language.empty()) {
        return;
    }
    const std::string path = editorTablePath(language);
    // A missing table is not fatal: English passthrough covers languages the
    // engine has not translated yet.
    if (!LocalizationManager::get().loadLanguage(language, path)) {
        YA_CORE_WARN("Editor localization: no editor table for '{}' at '{}'", language, path);
    }
}

void setEditorLanguage(std::string_view language)
{
    loadEditorTables(language);
    LocalizationManager::get().setLanguage(language);
}

} // namespace EditorLocalization
} // namespace ya
