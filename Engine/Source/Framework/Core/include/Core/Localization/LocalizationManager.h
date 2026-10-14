#pragma once

#include "Core/Api.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ya
{

/// Runtime localization: one loaded table per language, string keys, a
/// fallback chain, and a change event so open panels can repaint.
///
/// A language table is a flat JSON object mapping the source string to its
/// translation: `{ "Attack": "攻击" }`. Keys are the text as authored in
/// content (English by convention); a missing key translates to the key
/// itself, so content works before its translation is written.
///
/// Games load tables from project content (VFS paths, e.g.
/// `Content/Locale/zh.json`); the editor may add its own table for chrome
/// strings. Languages stack per use: `setLanguage` picks the active one, and
/// lookup falls back through the language stack's remaining entries to the
/// raw key.
struct YA_CORE_API LocalizationManager
{
    /// Change payload: the new active language ("" = keys pass through).
    using LanguageChangedEvent = void(std::string_view language);

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
    /// (English by convention). Fires `onLanguageChanged` when it changes.
    void setLanguage(std::string_view language);

    [[nodiscard]] const std::string& language() const { return _language; }

    /// Languages with a loaded table, sorted (settings UI, diagnostics).
    [[nodiscard]] std::vector<std::string> loadedLanguages() const;

    /// Translate `key`: walk the active language, then any fallbacks, then
    /// return the key itself. Always returns something displayable.
    [[nodiscard]] std::string_view tr(std::string_view key) const;

    /// Languages tried after the active one, in order. Empty by default.
    void setFallbackLanguages(std::vector<std::string> languages);

    /// Fired by setLanguage when the active language actually changes. UI
    /// panels subscribe to repaint; scripts re-tr their strings next frame.
    std::function<LanguageChangedEvent> onLanguageChanged;

    /// Forget every table and fall back to pass-through. The change event
    /// fires if a language was active.
    void clear();

  private:
    LocalizationManager() = default;

    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> _tables;
    std::vector<std::string> _fallbacks;
    std::string              _language;
};

} // namespace ya
