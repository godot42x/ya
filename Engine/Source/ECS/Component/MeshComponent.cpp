#include "MeshComponent.h"

#include "Core/AssetManager.h"
#include "Render/PrimitiveMeshCache.h"

namespace ya
{

bool MeshComponent::resolve()
{
    if (_bResolved) {
        return true;
    }

    _cachedMeshes.clear();

    // Priority 1: Built-in primitive geometry
    if (_primitiveGeometry != EPrimitiveGeometry::None) {
        auto mesh = PrimitiveMeshCache::get().getMesh(_primitiveGeometry);
        if (mesh) {
            _cachedMeshes.push_back(mesh.get());
            _bResolved = true;
            return true;
        }
        else {
            YA_CORE_ERROR("MeshComponent: Failed to get primitive mesh from cache");
            return false;
        }
    }

    // Priority 2: External model file
    if (_modelRef.hasPath()) {
        if (!_modelRef.isLoaded()) {
            if (!_modelRef.resolve()) {
                YA_CORE_WARN("MeshComponent: Failed to resolve model '{}'", _modelRef.getPath());
                return false;
            }
        }

        Model *model = _modelRef.get();
        if (model) {
            for (const auto &mesh : model->getMeshes()) {
                _cachedMeshes.push_back(mesh.get());
            }
            _bResolved = true;
            return true;
        }
    }

    YA_CORE_WARN("MeshComponent: No geometry source specified");
    return false;
}

} // namespace ya
