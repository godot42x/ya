#pragma once

#include "Core/Api.h"
#include "Core/Delegate.h"

#include <functional>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ya
{

/// Runtime localization: one loaded table per language, string keys, a
/// fallback chain, and a change event so open panels can repaint.
///
/// A language table is a flat JSON object mapping a key to its translation:
/// `{ "File": "文件" }`. Two key styles are supported:
///   * short source strings used directly as the key (`"File"`), so content
///     works before its translation is written;
///   * explicit identifiers with a fallback source string
///     (`editorTr("editor.inspector.empty", "Select an entity")`), so a long
///     display string is not its own key.
///
/// `tr(key, fallbackSource)` walks the active language, then the fallback
/// chain, then returns `fallbackSource` (or the key when no fallback is
/// given), so a missing translation degrades to readable text instead of a
/// raw identifier.
///
/// Games load tables from project content (VFS paths, e.g.
/// `Content/Locale/zh.json`); the editor adds its own table for chrome
/// strings. Languages stack per use: `setLanguage` picks the active one, and
/// lookup falls back through the language stack's remaining entries.
struct YA_CORE_API LocalizationManager
{
    /// Broadcast on every real language change: the new active language
    /// ("" = keys pass through). Multiple listeners may subscribe.
    using LanguageChangedDelegate = MulticastDelegate<void(std::string_view)>;
    using LanguageChangedHandle   = DelegateHandle;

    static YA_CORE_API LocalizationManager& get();

    LocalizationManager(const LocalizationManager&)            = delete;
    LocalizationManager& operator=(const LocalizationManager&) = delete;

    /// Load (or replace) one language's table from a JSON file. `path` goes
    /// through the VirtualFileSystem, so project content paths work. Returns
    /// false (and logs) when the file is missing or not a JSON object.
    bool loadLanguage(std::string_view language, const std::string& path);

    /// Load from an in-memory JSON object string (tests, embedded tables).
    bool loadLanguageFromJson(std::string_view language, std::string_view jsonText);

    /// Merge extra entries into an already loaded language without touching
    /// the file the table came from.
    void mergeTable(std::string_view language,
                    std::unordered_map<std::string, std::string> entries);

    /// Select the active language. Empty string means "keys pass through"
    /// (English by convention). Broadcasts on `LanguageChangedDelegate` when
    /// it changes.
    void setLanguage(std::string_view language);

    [[nodiscard]] const std::string& language() const { return _language; }

    /// Languages with a loaded table, sorted (settings UI, diagnostics).
    [[nodiscard]] std::vector<std::string> loadedLanguages() const;

    /// Translate `key`: walk the active language, then any fallbacks, then
    /// return `fallbackSource` if non-empty, else the key itself. Always
    /// returns something displayable.
    [[nodiscard]] std::string tr(std::string_view key,
                                 std::string_view fallbackSource = "") const;

    /// Substitution for `tr` results: replace each `{N}` placeholder in
    /// `text` with `args[N]`. Missing indices are left as written. The `{}`
    /// style matches std::format, without the compile-time format string.
    [[nodiscard]] static std::string format(std::string_view text,
                                            std::span<const std::string> args);
    [[nodiscard]] static std::string format(std::string_view text,
                                            std::initializer_list<std::string> args);

    /// Keys `tr` could not resolve while a language was active (a loaded
    /// table is missing the entry and no fallback had it). Diagnostics / the
    /// locale lint tool read this. Cleared by `clear`.
    [[nodiscard]] const std::unordered_set<std::string>& missingKeys() const { return _missingKeys; }

    /// Languages tried after the active one, in order. Empty by default.
    void setFallbackLanguages(std::vector<std::string> languages);

    /// Subscribe to language changes. The returned handle unsubscribes via
    /// `removeLanguageChangedListener`. UI panels and reactive text bindings
    /// subscribe to repaint or re-resolve; scripts re-tr their strings next
    /// frame.
    LanguageChangedHandle addLanguageChangedListener(std::function<void(std::string_view)> callback);
    void removeLanguageChangedListener(LanguageChangedHandle handle);

    /// Forget every table and fall back to pass-through. The change event
    /// broadcasts if a language was active.
    void clear();

  private:
    LocalizationManager() = default;

    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> _tables;
    std::vector<std::string> _fallbacks;
    std::string              _language;
    /// Recorded by `tr` from a const method; a missing key is a diagnostic,
    /// not part of the observable translation state.
    mutable std::unordered_set<std::string> _missingKeys;
    LanguageChangedDelegate  _languageChangedDelegate;
};

} // namespace ya
