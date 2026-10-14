#include "Core/Localization/LocalizationManager.h"

#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace ya
{

namespace
{
/// Replace `{N}` with `args[N]`. A placeholder with no matching arg is left
/// as written, so a mistyped index is visible in the UI instead of eating the
/// rest of the string. `{{` is not an escape (this is not std::format; the
/// tables hold display text, not format strings).
void substitutePlaceholders(std::string_view text, std::span<const std::string> args, std::string& out)
{
    out.reserve(text.size() + 16);
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c != '{') {
            out.push_back(c);
            continue;
        }
        size_t j = i + 1;
        size_t index = 0;
        bool   bHasDigits = false;
        while (j < text.size() && text[j] >= '0' && text[j] <= '9') {
            index = index * 10 + static_cast<size_t>(text[j] - '0');
            bHasDigits = true;
            ++j;
        }
        if (bHasDigits && j < text.size() && text[j] == '}') {
            if (index < args.size()) {
                out.append(args[index]);
            }
            else {
                out.append(text.substr(i, j - i + 1));
            }
            i = j;
            continue;
        }
        out.push_back(c);
    }
}
} // namespace

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
    _languageChangedDelegate.broadcast(std::string_view(_language));
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

std::string LocalizationManager::tr(std::string_view key, std::string_view fallbackSource) const
{
    if (!_language.empty()) {
        // The active language first, then the fallback chain.
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
        // A language is active but nothing has this key: worth recording.
        _missingKeys.insert(std::string(key));
    }
    if (!fallbackSource.empty()) {
        return std::string(fallbackSource);
    }
    return std::string(key);
}

std::string LocalizationManager::format(std::string_view text, std::span<const std::string> args)
{
    std::string out;
    substitutePlaceholders(text, args, out);
    return out;
}

std::string LocalizationManager::format(std::string_view text, std::initializer_list<std::string> args)
{
    return format(text, std::span<const std::string>(args.begin(), args.size()));
}

void LocalizationManager::setFallbackLanguages(std::vector<std::string> languages)
{
    _fallbacks = std::move(languages);
}

LocalizationManager::LanguageChangedHandle
LocalizationManager::addLanguageChangedListener(std::function<void(std::string_view)> callback)
{
    return _languageChangedDelegate.addLambda(std::move(callback));
}

void LocalizationManager::removeLanguageChangedListener(LanguageChangedHandle handle)
{
    _languageChangedDelegate.remove(handle);
}

void LocalizationManager::clear()
{
    const bool bHadLanguage = !_language.empty();
    _tables.clear();
    _fallbacks.clear();
    _language.clear();
    _missingKeys.clear();
    if (bHadLanguage) {
        _languageChangedDelegate.broadcast(std::string_view(_language));
    }
}

} // namespace ya
