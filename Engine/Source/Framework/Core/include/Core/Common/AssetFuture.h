#pragma once

#include <memory>

namespace ya
{

// Forward declarations
struct Texture;
struct Model;

/**
 * @brief Lightweight "future" returned by async AssetManager::loadTexture() / loadModel().
 *
 * INTENTIONALLY has no implicit conversion to T* or shared_ptr<T>.
 * You MUST call isReady() before get(), or use getOr(fallback).
 * This prevents the crash pattern:  loadTexture("x.png")->getImageView()  // BOOM if async
 *
 * Usage:
 *   auto future = am->loadTexture("diffuse.png");
 *
 *   // Option 1: Check explicitly
 *   if (future.isReady()) {
 *       auto* tex = future.get();  // safe
 *   }
 *
 *   // Option 2: Use fallback
 *   auto* tex = future.getOr(placeholderTexture);  // always safe
 *
 *   // Option 3: In TAssetRef::resolve() — just retry each frame until ready
 *
 * For sync loading that must succeed immediately, use loadTextureSync() which
 * returns shared_ptr<T> directly — no wrapper, no ambiguity.
 */
template <typename T>
class AssetFuture
{
  public:
    AssetFuture() = default;
    explicit AssetFuture(std::shared_ptr<T> resource)
        : _resource(std::move(resource)) {}

    /// Is the real resource available?
    [[nodiscard]] bool isReady() const { return _resource != nullptr; }

    /// Explicit check — for `if (future)` pattern.
    [[nodiscard]] explicit operator bool() const { return isReady(); }

    /// Get the resource. Returns nullptr if not ready.
    [[nodiscard]] T* get() const { return _resource.get(); }

    /// Get shared_ptr. Returns nullptr shared_ptr if not ready.
    [[nodiscard]] std::shared_ptr<T> getShared() const { return _resource; }

    /// Get the resource, or a fallback if not yet ready.
    [[nodiscard]] T* getOr(T* fallback) const { return _resource ? _resource.get() : fallback; }

    /// Get shared_ptr, or fallback.
    [[nodiscard]] std::shared_ptr<T> getSharedOr(std::shared_ptr<T> fallback) const
    {
        return _resource ? _resource : fallback;
    }

  private:
    std::shared_ptr<T> _resource;
};

// Convenience aliases
using TextureFuture = AssetFuture<Texture>;
using ModelFuture   = AssetFuture<Model>;

} // namespace ya
