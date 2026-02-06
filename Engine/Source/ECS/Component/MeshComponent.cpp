#include "MeshComponent.h"

#include "ECS/Entity.h"
#include "Resource/AssetManager.h"
#include "Resource/PrimitiveMeshCache.h"



namespace ya
{

bool MeshComponent::resolve()
{
    if (_bResolved) {
        return true;
    }

    _cachedMesh = nullptr;

    // Priority 1: Built-in primitive geometry
    if (_primitiveGeometry != EPrimitiveGeometry::None) {
        auto mesh = PrimitiveMeshCache::get().getMesh(_primitiveGeometry);
        if (mesh) {
            _cachedMesh = mesh;
            _bResolved  = true;
            return true;
        }
        else {
            YA_CORE_ERROR("MeshComponent: Failed to get primitive mesh from cache");
            return false;
        }
    }

    // Priority 2: Mesh from Model by path and index
    if (!_sourceModelPath.empty()) {
        auto model = AssetManager::get()->getModel(_sourceModelPath);
        if (!model) {
            // Try to load the model
            model = AssetManager::get()->loadModel(_sourceModelPath);
        }

        if (model && _meshIndex < model->getMeshCount()) {
            _cachedMesh = model->getMesh(_meshIndex).get();
            _bResolved  = true;
            return true;
        }
        else {
            YA_CORE_WARN("MeshComponent: Failed to get mesh[{}] from model '{}'",
                         _meshIndex,
                         _sourceModelPath);
            return false;
        }
    }

    YA_CORE_WARN("MeshComponent: No geometry source specified");
    return false;
}

} // namespace ya
