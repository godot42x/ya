#include "Core/Localization/LocalizationManager.h"

#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace ya
{

LocalizationManager& LocalizationManager::get()
{
    static LocalizationManager instance;
    return instance;
}

bool LocalizationManager::loadLanguage(std::string_view language, const std::string& path)
{
    VirtualFileSystem* vfs = VirtualFileSystem::get();
    if (!vfs) {
        YA_CORE_ERROR("Localization: no virtual filesystem, cannot load '{}'", path);
        return false;
    }
    std::string text;
    if (!vfs->readFileToString(path, text)) {
        YA_CORE_ERROR("Localization: cannot read language file '{}'", path);
        return false;
    }
    return loadLanguageFromJson(language, text);
}

bool LocalizationManager::loadLanguageFromJson(std::string_view language, std::string_view jsonText)
{
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(jsonText);
    }
    catch (const nlohmann::json::exception& error) {
        YA_CORE_ERROR("Localization: language '{}' is not JSON: {}", language, error.what());
        return false;
    }
    if (!parsed.is_object()) {
        YA_CORE_ERROR("Localization: language '{}' table must be a JSON object", language);
        return false;
    }

    std::unordered_map<std::string, std::string> table;
    table.reserve(parsed.size());
    for (auto it = parsed.begin(); it != parsed.end(); ++it) {
        if (it.value().is_string()) {
            table[it.key()] = it.value().get<std::string>();
        }
        else {
            YA_CORE_WARN("Localization: '{}' entry '{}' is not a string, skipped",
                         language, it.key());
        }
    }
    _tables[std::string(language)] = std::move(table);
    return true;
}

void LocalizationManager::mergeTable(std::string_view language,
                                     std::unordered_map<std::string, std::string> entries)
{
    auto& table = _tables[std::string(language)];
    for (auto& [key, value] : entries) {
        table[key] = std::move(value);
    }
}

void LocalizationManager::setLanguage(std::string_view language)
{
    std::string next(language);
    if (next.empty()) {
        next.clear();
    }
    if (next == _language) {
        return;
    }
    if (!next.empty() && _tables.find(next) == _tables.end()) {
        YA_CORE_WARN("Localization: language '{}' has no table loaded; keys pass through", next);
    }
    _language = std::move(next);
    if (onLanguageChanged) {
        onLanguageChanged(_language);
    }
}

std::vector<std::string> LocalizationManager::loadedLanguages() const
{
    std::vector<std::string> names;
    names.reserve(_tables.size());
    for (const auto& [language, table] : _tables) {
        names.push_back(language);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string_view LocalizationManager::tr(std::string_view key) const
{
    if (_language.empty()) {
        return key;
    }
    // The active language first, then the fallback chain, then the key.
    if (const auto active = _tables.find(_language); active != _tables.end()) {
        if (const auto found = active->second.find(std::string(key)); found != active->second.end()) {
            return found->second;
        }
    }
    for (const auto& fallback : _fallbacks) {
        if (const auto table = _tables.find(fallback); table != _tables.end()) {
            if (const auto found = table->second.find(std::string(key)); found != table->second.end()) {
                return found->second;
            }
        }
    }
    return key;
}

void LocalizationManager::setFallbackLanguages(std::vector<std::string> languages)
{
    _fallbacks = std::move(languages);
}

void LocalizationManager::clear()
{
    const bool bHadLanguage = !_language.empty();
    _tables.clear();
    _fallbacks.clear();
    _language.clear();
    if (bHadLanguage && onLanguageChanged) {
        onLanguageChanged(_language);
    }
}

} // namespace ya
