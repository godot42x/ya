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

    AssetHandle<Model> acquireModel(const std::string& path) const override
    {
        return AssetManager::get()->loadModel(AssetManager::ModelLoadRequest{
            .filepath = path,
        });
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
