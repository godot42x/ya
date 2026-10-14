#pragma once

#include "Core/Localization/LocalizationManager.h"

#include <initializer_list>
#include <string>
#include <string_view>

namespace ya
{

/// Editor chrome localization. The editor adds its own table on top of the
/// engine's LocalizationManager (content tables load through project VFS,
/// editor chrome tables live in `Engine/Content/Locale/editor.<lang>.json`)
/// and reads every chrome string through `editorTr`.
///
/// Key policy: short labels (`"File"`, `"Edit"`) use the source string as the
/// key; anything long uses a dotted identifier plus the English source as the
/// fallback (`editorTr("editor.inspector.empty", "Select an entity")`), so a
/// missing translation degrades to readable English instead of an identifier.
namespace EditorLocalization
{

/// Load (or reload) the editor chrome table for `language`. An empty language
/// is a no-op: there is no "English table" file, English is the fallback that
/// `editorTr` returns when the key is untranslated.
void loadEditorTables(std::string_view language);

/// Switch the active language: load this language's editor table (if any) and
/// select it on the manager. Broadcasts through
/// LocalizationManager::addLanguageChangedListener, so bound chrome refreshes.
void setEditorLanguage(std::string_view language);

/// Translate one editor chrome string. `fallback` is the English source shown
/// when the key has no translation in the active language.
[[nodiscard]] inline std::string editorTr(std::string_view key, std::string_view fallback = "")
{
    return LocalizationManager::get().tr(key, fallback);
}

/// `editorTr` + `{N}` substitution in one call (see LocalizationManager::format).
[[nodiscard]] inline std::string editorFormat(std::string_view key,
                                              std::string_view fallback,
                                              std::initializer_list<std::string> args)
{
    return LocalizationManager::format(editorTr(key, fallback), args);
}

} // namespace EditorLocalization

} // namespace ya
