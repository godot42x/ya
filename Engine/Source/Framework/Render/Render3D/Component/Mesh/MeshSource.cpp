#include "MeshSource.h"

#include "Resource/Model.h"
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

    // Priority 0: Mesh from Model by path and index. This is also how engine
    // gizmo meshes are sourced: a declaration points at an engine content
    // asset, and nothing about that path reaches scene data.
    if (!_sourceModelPath.empty()) {
        Model* model = nullptr;
        auto   ft    = AssetManager::get()->loadModel(AssetManager::ModelLoadRequest{
            .filepath = _sourceModelPath,
        });
        if (ft.isReady()) {
            model = ft.get();
        }
        else {
            // Not resolvable yet, and the two reasons have to read differently:
            // a decode still in flight is normal (retry next frame, silently)
            // while a load that already failed will never become ready, so
            // staying silent there would hide a broken asset forever.
            if (!AssetManager::get()->isModelLoadPending(_sourceModelPath)) {
                YA_CORE_WARN("MeshSource: Model '{}' failed to load; mesh[{}] stays unresolved",
                             _sourceModelPath,
                             _meshIndex);
            }
            return false;
        }

        if (model && _meshIndex < model->getMeshCount()) {
            _cachedMesh = model->getMesh(_meshIndex).get();
            _ownerModel = ft.getShared();
            _bResolved  = true;
            return true;
        }
        YA_CORE_WARN("MeshSource: Failed to get mesh[{}] from model '{}'",
                     _meshIndex,
                     _sourceModelPath);
        return false;
    }

    // Priority 1: Built-in primitive geometry
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
