#pragma once

#include "Resource/Handle/ResourceHandle.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace ya
{

/**
 * @brief Bidirectional bridge between asset paths and FResourceHandle<T>.
 *
 * Disk/serialization only ever stores the path (YA_REFLECT_FIELD(_path)); the
 * handle is purely runtime state. This registry is what turns a deserialized
 * path back into a stable handle, and lets the asset manager answer "what path
 * does this handle stand for" without baking strings into the slot table.
 *
 * It does NOT allocate slots itself; the owner (AssetTextureManager etc.) calls
 * ResourceTable::allocate and then bind()s the resulting handle to the path. A
 * path maps to exactly one handle for the lifetime of that asset; when the slot
 * is recycled the owner must unbind() so a future load gets a fresh handle.
 *
 * Thread-safety: same coarse-mutex policy as ResourceTable (worker-thread loads
 * + main-thread callbacks).
 */
template <typename T>
class PathRegistry
{
    std::unordered_map<std::string, FResourceHandle<T>> _pathToHandle;
    std::unordered_map<uint32_t, std::string>           _indexToPath; // keyed by handle index
    mutable std::mutex                                  _mutex;

  public:
    /// Look up the handle bound to a path. Returns an invalid handle if none.
    [[nodiscard]] FResourceHandle<T> find(const std::string &path) const
    {
        std::lock_guard lock(_mutex);
        auto            it = _pathToHandle.find(path);
        return it == _pathToHandle.end() ? FResourceHandle<T>{} : it->second;
    }

    /// Bind a freshly allocated handle to a path (overwrites any prior binding).
    void bind(const std::string &path, FResourceHandle<T> handle)
    {
        std::lock_guard lock(_mutex);
        _pathToHandle[path]          = handle;
        _indexToPath[handle.index]   = path;
    }

    /// Reverse lookup: the path a handle's slot stands for, or "" if unknown.
    [[nodiscard]] std::string pathOf(FResourceHandle<T> handle) const
    {
        std::lock_guard lock(_mutex);
        auto            it = _indexToPath.find(handle.index);
        return it == _indexToPath.end() ? std::string{} : it->second;
    }

    /// Drop a path binding (call when the slot is recycled).
    void unbind(const std::string &path)
    {
        std::lock_guard lock(_mutex);
        auto            it = _pathToHandle.find(path);
        if (it != _pathToHandle.end()) {
            _indexToPath.erase(it->second.index);
            _pathToHandle.erase(it);
        }
    }
};

} // namespace ya
