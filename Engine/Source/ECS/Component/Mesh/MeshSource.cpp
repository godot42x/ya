#include "MeshSource.h"

#include "Render/Model.h"
#include "Resource/AssetManager.h"
#include "Resource/Mesh/PrimitiveMeshCache.h"

namespace ya
{

bool MeshSource::resolve()
{
    if (_bResolved) {
        return true;
    }

    _cachedMesh = nullptr;

    // Priority 1: Mesh from Model by path and index
    if (!_sourceModelPath.empty()) {
        Model* model = nullptr;
        auto   ft    = AssetManager::get()->loadModel(AssetManager::ModelLoadRequest{
            .filepath = _sourceModelPath,
        });
        if (ft.isReady()) {
            model = ft.get();
        }

        if (model && _meshIndex < model->getMeshCount()) {
            _cachedMesh = model->getMesh(_meshIndex).get();
            _bResolved  = true;
            return true;
        }
        YA_CORE_WARN("MeshSource: Failed to get mesh[{}] from model '{}'",
                     _meshIndex,
                     _sourceModelPath);
        return false;
    }

    // Priority 2: Built-in primitive geometry
    if (_primitiveGeometry != EPrimitiveGeometry::None) {
        auto mesh = PrimitiveMeshCache::get().getMesh(_primitiveGeometry);
        if (mesh) {
            _cachedMesh = mesh;
            _bResolved  = true;
            return true;
        }
        YA_CORE_ERROR("MeshSource: Failed to get primitive mesh from cache");
        return false;
    }


    YA_CORE_WARN("MeshSource: No geometry source specified");
    return false;
}

} // namespace ya
