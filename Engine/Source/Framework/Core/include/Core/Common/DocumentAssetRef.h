#pragma once

#include "Core/Common/AssetRef.h"
#include "Core/Common/AssetTypeRegistry.h"

#include <memory>
#include <string>

namespace ya
{

// Shared path + slot for a synchronous document asset. Concrete refs stay
// thin reflected types so scene files keep their type names.
template <typename T>
struct DocumentAssetRef : AssetRefBase
{
    AssetHandle<T> _handle;

    DocumentAssetRef() = default;
    explicit DocumentAssetRef(const std::string& path) : AssetRefBase(path) { rebind(); }

    T*                 get() const { return isLoaded() ? _handle->resource.get() : nullptr; }
    std::shared_ptr<T> getShared() const { return isLoaded() ? _handle->resource : nullptr; }
    bool               isLoaded() const { return _handle && _handle->state == EAssetSlotState::Ready; }

    EAssetResolveState getResolveState() const override
    {
        if (_path.empty()) {
            return EAssetResolveState::Empty;
        }
        if (!_handle) {
            return EAssetResolveState::Failed;
        }
        switch (_handle->state) {
        case EAssetSlotState::Ready:
            return EAssetResolveState::Ready;
        case EAssetSlotState::Loading:
        case EAssetSlotState::Failed:
            break;
        }
        return EAssetResolveState::Failed;
    }

    void rebind() override
    {
        _handle.reset();
        if (_path.empty()) {
            return;
        }
        if (IDocumentAssetStore<T>* documentStore = AssetTypeRegistry::get().store<T>()) {
            _handle = documentStore->acquire(_path);
        }
    }
};

} // namespace ya
