#include "LitMaterialComponent.h"

#include "Core/AssetManager.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/PrimitiveMeshCache.h"
#include "Render/TextureLibrary.h"

namespace ya
{

bool LitMaterialComponent::resolve()
{
    if (_bResolved) {
        return true;
    }

    bool success = true;

    // 1. Resolve model reference
    if (_modelRef.hasPath() && !_modelRef.isLoaded()) {
        if (!_modelRef.resolve()) {
            YA_CORE_WARN("LitMaterialComponent: Failed to resolve model '{}'", _modelRef.getPath());
            success = false;
        }
    }

    // 2. Resolve texture slots
    if (_diffuseSlot.textureRef.hasPath() && !_diffuseSlot.isLoaded()) {
        if (!_diffuseSlot.resolve()) {
            YA_CORE_WARN("LitMaterialComponent: Failed to resolve diffuse texture '{}'",
                         _diffuseSlot.textureRef.getPath());
            // Not fatal - can use default texture
        }
    }

    if (_specularSlot.textureRef.hasPath() && !_specularSlot.isLoaded()) {
        if (!_specularSlot.resolve()) {
            YA_CORE_WARN("LitMaterialComponent: Failed to resolve specular texture '{}'",
                         _specularSlot.textureRef.getPath());
            // Not fatal - can use default texture
        }
    }

    // 3. Create runtime material and populate meshes
    bool hasMeshSource = (_primitiveGeometry != EPrimitiveGeometry::None) || _modelRef.isLoaded();

    if (hasMeshSource) {
        // Create a new material instance for this component
        std::string matLabel = "LitMat_" + std::to_string(reinterpret_cast<uintptr_t>(this));
        _runtimeMaterial     = MaterialFactory::get()->createMaterial<LitMaterial>(matLabel);

        if (_runtimeMaterial) {
            // Sync params to runtime material
            syncParamsToRuntime();

            // Sync textures to runtime material
            syncTexturesToRuntime(TextureLibrary::get().getDefaultSampler().get());

            // Populate meshes based on source type
            if (_primitiveGeometry != EPrimitiveGeometry::None) {
                // Get shared primitive mesh from cache (no allocation if already cached)
                _primitiveMesh = PrimitiveMeshCache::get().getMesh(_primitiveGeometry);
                if (_primitiveMesh) {
                    addMesh(_primitiveMesh.get(), _runtimeMaterial);
                }
                else {
                    YA_CORE_ERROR("LitMaterialComponent: Failed to get primitive mesh from cache");
                    success = false;
                }
            }
            else if (_modelRef.isLoaded()) {
                // Use model meshes
                Model *model = _modelRef.get();
                for (const auto &mesh : model->getMeshes()) {
                    addMesh(mesh.get(), _runtimeMaterial);
                }
            }
        }
        else {
            YA_CORE_ERROR("LitMaterialComponent: Failed to create runtime material");
            success = false;
        }
    }

    _bResolved = success;
    return success;
}

void LitMaterialComponent::syncParamsToRuntime()
{
    if (!_runtimeMaterial) {
        return;
    }

    _runtimeMaterial->getParamsMut() = _params;
    _runtimeMaterial->setParamDirty();
}

void LitMaterialComponent::syncTexturesToRuntime(Sampler *defaultSampler)
{
    if (!_runtimeMaterial || !defaultSampler) {
        return;
    }

    // Sync diffuse texture
    if (_diffuseSlot.isLoaded()) {
        TextureView tv = _diffuseSlot.toTextureView(defaultSampler);
        _runtimeMaterial->setTextureView(LitMaterial::EResource::DiffuseTexture, tv);
    }

    // Sync specular texture
    if (_specularSlot.isLoaded()) {
        TextureView tv = _specularSlot.toTextureView(defaultSampler);
        _runtimeMaterial->setTextureView(LitMaterial::EResource::SpecularTexture, tv);
    }
}

} // namespace ya
