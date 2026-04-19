#pragma once

#include "Core/Trait.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace ya
{


namespace Config
{
struct OpenDocumentOptions
{
    bool bPersistIfMissing = true;
    bool bReadOnly         = false;
};
} // namespace Config

struct ConfigManager : public disable_copy
{
    struct Document
    {
        std::string    path;
        nlohmann::json root      = nlohmann::json::object();
        bool           dirty     = false;
        bool           bReadOnly = false;

        [[nodiscard]] bool isLoaded() const { return !path.empty(); }
    };

    struct Editor : public disable_copy
    {
        ConfigManager* _manager         = nullptr;
        std::string    _docName;
        bool           _bFlushOnDestroy = true;
        bool           _bTouched        = false;

        explicit Editor(std::string docName, ConfigManager& manager = ConfigManager::get())
            : _manager(&manager)
            , _docName(std::move(docName))
        {}

        ~Editor()
        {
            if (_bFlushOnDestroy && _bTouched) {
                flush();
            }
        }

        template <typename T>
        Editor& set(std::string_view key, const T& value)
        {
            if (_manager) {
                _manager->set(_docName, key, value);
                _bTouched = true;
            }
            return *this;
        }

        Editor& removeValue(std::string_view key)
        {
            if (_manager) {
                _manager->removeValue(_docName, key);
                _bTouched = true;
            }
            return *this;
        }

        Editor& pruneEmptyParents(std::string_view key)
        {
            if (_manager) {
                _manager->pruneEmptyParents(_docName, key);
                _bTouched = true;
            }
            return *this;
        }

        Editor& markDirty()
        {
            if (_manager) {
                _manager->markDirty(_docName);
                _bTouched = true;
            }
            return *this;
        }

        Editor& setFlushOnDestroy(bool bFlushOnDestroy)
        {
            _bFlushOnDestroy = bFlushOnDestroy;
            return *this;
        }

        bool flush()
        {
            const bool bFlushed = _manager ? _manager->flushDocument(_docName) : false;
            if (bFlushed) {
                _bTouched = false;
            }
            return bFlushed;
        }
    };


    static ConfigManager& get();

    void init();
    void shutdown();
    void flushAll();
    void markDirty(const std::string& docName);

    Document& openDocument(const std::string& name, const std::string& path, Config::OpenDocumentOptions options = {});
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
