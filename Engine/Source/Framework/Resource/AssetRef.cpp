#include "Core/Common/AssetRef.h"
#include "Resource/AssetManager.h"
#include "Core/Log.h"
#include "Core/TypeIndex.h"
#include "RHI/Core/Texture.h"
#include "Resource/Mesh.h"
#include "Resource/Model.h"

namespace ya
{

// ============================================================================
// Engine asset-ref resolver.
//
// The concrete ref types live in Core; this resolver is installed into
// Core's registry slot at static-init time and hands them resource-layer
// slots (AssetManager). Pure GUI hosts never install a resolver and their
// refs bind to nothing.
// ============================================================================

namespace
{

struct EngineAssetRefResolver final : IAssetRefResolver
{
    AssetHandle<Texture> acquireTexture(const std::string& path) const override
    {
        return AssetManager::get()->loadTexture(AssetManager::TextureLoadRequest{.filepath = path});
    }

    void resolveAssetRef(type_index_t typeIndex, void* assetRefPtr) const override
    {
        if (typeIndex == ya::type_index_v<ModelRef>) {
            resolveModel(*static_cast<ModelRef*>(assetRefPtr));
        }
        else if (typeIndex == ya::type_index_v<MeshRef>) {
            resolveMesh(*static_cast<MeshRef*>(assetRefPtr));
        }
        else {
            YA_CORE_WARN("EngineAssetRefResolver: Unknown asset ref type index: {}", typeIndex);
        }
    }

  private:
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
