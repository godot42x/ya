#pragma once

#include "Core/Trait.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <unordered_map>

namespace ya
{

struct ConfigManager : public disable_copy
{
    struct OpenDocumentOptions
    {
        bool bPersistIfMissing = true;
        bool bReadOnly         = false;
    };

    struct Document
    {
        std::string    path;
        nlohmann::json root  = nlohmann::json::object();
        bool           dirty = false;
        bool           bReadOnly = false;

        [[nodiscard]] bool isLoaded() const { return !path.empty(); }
    };

    static ConfigManager& get();

    void init();
    void shutdown();
    void flushAll();
    void markDirty(const std::string& docName);

    Document& openDocument(const std::string& name, const std::string& path, OpenDocumentOptions options = {});
    bool      closeDocument(const std::string& name, bool bFlush = true);

    [[nodiscard]] bool hasDocument(const std::string& name) const;
    Document*          findDocument(const std::string& name);
    const Document*    findDocument(const std::string& name) const;

    [[nodiscard]] bool hasValue(const std::string& docName, std::string_view key) const;
    void               removeValue(const std::string& docName, std::string_view key);
    bool               pruneEmptyParents(const std::string& docName, std::string_view key);

    template <typename T>
    bool tryGet(const std::string& docName, std::string_view key, T& out) const
    {
        const nlohmann::json* node = findNode(docName, key);
        if (!node) {
            return false;
        }

        try {
            out = node->get<T>();
            return true;
        }
        catch (const nlohmann::json::exception&) {
            return false;
        }
    }

    template <typename T>
    T getOr(const std::string& docName, std::string_view key, const T& fallback) const
    {
        T value = fallback;
        if (tryGet(docName, key, value)) {
            return value;
        }
        return fallback;
    }

    template <typename T>
    void set(const std::string& docName, std::string_view key, const T& value)
    {
        nlohmann::json* node = ensureNode(docName, key);
        if (!node) {
            return;
        }

        nlohmann::json next = value;
        if (*node == next) {
            return;
        }

        *node = std::move(next);
        if (auto* doc = findDocument(docName)) {
            doc->dirty = true;
        }
    }

    bool flushDocument(const std::string& docName);

  private:
    std::unordered_map<std::string, Document> _documents;
    bool                                      _initialized = false;

    [[nodiscard]] const nlohmann::json* findNode(const std::string& docName, std::string_view key) const;
    nlohmann::json*                     ensureNode(const std::string& docName, std::string_view key);
    static nlohmann::json::json_pointer toJsonPointer(std::string_view key);
};

} // namespace ya
