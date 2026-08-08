#include "Foundation/Core/Common/AssetRef.h"
#include "Framework/Game/Resource/AssetManager.h"
#include "Framework/GUI/Runtime/Resource/TextureLibrary.h"
#include "Foundation/Core/Log.h"
#include "Foundation/Core/TypeIndex.h"
#include "Foundation/RHI/Core/Texture.h"
#include "Framework/Game/Resource/Mesh.h"
#include "Framework/Game/Resource/Model.h"

namespace ya
{

// ============================================================================
// Engine asset-ref resolver.
//
// The concrete ref types (TextureRef/ModelRef/MeshRef) and their vtables live
// in Core; this resolver is installed into Core's registry slot at static-init
// time and owns the resource-layer loading logic (AssetManager / texture
// placeholders). Pure GUI hosts never install a resolver and resolve() simply
// marks refs failed instead of pulling the resource layer in.
// ============================================================================

namespace
{

struct EngineAssetRefResolver final : IAssetRefResolver
{
    bool isAssetRefType(type_index_t typeIndex) const override
    {
        return typeIndex == ya::type_index_v<TextureRef> ||
               typeIndex == ya::type_index_v<ModelRef> ||
               typeIndex == ya::type_index_v<MeshRef>;
    }

    void resolveAssetRef(type_index_t typeIndex, void* assetRefPtr) const override
    {
        if (typeIndex == ya::type_index_v<TextureRef>) {
            resolveTexture(*static_cast<TextureRef*>(assetRefPtr));
        }
        else if (typeIndex == ya::type_index_v<ModelRef>) {
            resolveModel(*static_cast<ModelRef*>(assetRefPtr));
        }
        else if (typeIndex == ya::type_index_v<MeshRef>) {
            resolveMesh(*static_cast<MeshRef*>(assetRefPtr));
        }
        else {
            YA_CORE_WARN("EngineAssetRefResolver: Unknown asset ref type index: {}", typeIndex);
        }
    }

    bool isAssetRefStale(type_index_t typeIndex, const void* assetRefPtr) const override
    {
        if (typeIndex == ya::type_index_v<TextureRef>) {
            return isStale(*static_cast<const TextureRef*>(assetRefPtr));
        }
        if (typeIndex == ya::type_index_v<ModelRef>) {
            return isStale(*static_cast<const ModelRef*>(assetRefPtr));
        }
        if (typeIndex == ya::type_index_v<MeshRef>) {
            return isStale(*static_cast<const MeshRef*>(assetRefPtr));
        }
        return false;
    }

  private:
    static void resolveTexture(TextureRef& ref)
    {
        if (ref.getPath().empty()) {
            ref._resolveState = EAssetResolveState::Empty;
            return;
        }

        const auto currentVersion = AssetManager::get()->getResourceVersion(ref.getPath());

        // Ready: check version to detect reloaded resources
        if (ref._resolveState == EAssetResolveState::Ready && ref._cachedPtr) {
            if (ref._resolvedVersion == currentVersion) {
                return; // Up-to-date, fast path
            }
            // Version changed -> stale pointer, force re-resolve
            ref._cachedPtr.reset();
            ref._resolveState = EAssetResolveState::Dirty;
            YA_CORE_TRACE("TextureRef: version changed for '{}', re-resolving", ref.getPath());
        }

        if (ref._resolveState == EAssetResolveState::Failed && ref._resolvedVersion == currentVersion) {
            return;
        }

        // Loading or Dirty: try to get the real texture from cache
        auto future = AssetManager::get()->loadTexture(AssetManager::TextureLoadRequest{
            .filepath = ref.getPath(),
        });
        if (future.isReady()) {
            ref._cachedPtr       = future.getShared();
            ref._resolveState    = EAssetResolveState::Ready;
            ref._resolvedVersion = currentVersion;
            return;
        }

        if (AssetManager::get()->isTextureLoadFailed(ref.getPath())) {
            ref._resolveState    = EAssetResolveState::Failed;
            ref._resolvedVersion = currentVersion;
            auto placeholder = TextureLibrary::get().getCheckerboardTexture();
            if (placeholder) {
                ref._cachedPtr = placeholder;
            }
            return;
        }

        // Not ready yet — use placeholder, stay in Loading state
        if (ref._resolveState != EAssetResolveState::Loading) {
            ref._resolveState = EAssetResolveState::Loading;
            auto placeholder = TextureLibrary::get().getCheckerboardTexture();
            if (placeholder) {
                ref._cachedPtr = placeholder;
            }
        }
    }

    static void resolveModel(ModelRef& ref)
    {
        if (ref.getPath().empty()) {
            ref._resolveState = EAssetResolveState::Empty;
            return;
        }

        if (ref._resolveState == EAssetResolveState::Ready && ref._cachedPtr) {
            const auto currentVersion = AssetManager::get()->getResourceVersion(ref.getPath());
            if (ref._resolvedVersion == currentVersion) {
                return;
            }
            ref._cachedPtr.reset();
            ref._resolveState = EAssetResolveState::Dirty;
            YA_CORE_TRACE("ModelRef: version changed for '{}', re-resolving", ref.getPath());
        }

        const auto currentVersion = AssetManager::get()->getResourceVersion(ref.getPath());
        auto       future         = AssetManager::get()->loadModel(AssetManager::ModelLoadRequest{
            .filepath = ref.getPath(),
        });
        if (future.isReady()) {
            ref._cachedPtr       = future.getShared();
            ref._resolveState    = EAssetResolveState::Ready;
            ref._resolvedVersion = currentVersion;
            return;
        }

        if (ref._resolveState != EAssetResolveState::Loading) {
            ref._resolveState = EAssetResolveState::Loading;
        }
    }

    static void resolveMesh(MeshRef& ref)
    {
        // Mesh loading not implemented yet
        ref._resolveState = EAssetResolveState::Failed;
        UNIMPLEMENTED();
    }

    template <typename T>
    static bool isStale(const T& ref)
    {
        if (ref._resolveState != EAssetResolveState::Ready || ref.getPath().empty()) {
            return false;
        }
        auto* const assets = AssetManager::get();
        const auto  epoch  = assets->getResourceVersionEpoch();
        if (ref.hasCheckedAt(epoch)) {
            return false;
        }
        ref.markCheckedAt(epoch);
        return ref._resolvedVersion != assets->getResourceVersion(ref.getPath());
    }
};

struct ResolverRegistrar
{
    ResolverRegistrar()
    {
        setAssetRefResolver(&impl);
    }

    EngineAssetRefResolver impl;
};

ResolverRegistrar g_assetRefResolverRegistrar;

} // namespace

} // namespace ya
