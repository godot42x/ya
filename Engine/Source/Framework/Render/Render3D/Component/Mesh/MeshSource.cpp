#include "ECS/Component/Mesh/MeshSource.h"

#include "Resource/Model.h"
#include "Resource/AssetManager.h"
#include "Resource/Mesh/PrimitiveMeshCache.h"

namespace ya
{

bool MeshSource::resolve()
{
    // Priority 0: Mesh from Model by path and index. This is also how engine
    // gizmo meshes are sourced: a declaration points at an engine content
    // asset, and nothing about that path reaches scene data.
    if (!_sourceModelPath.empty()) {
        if (!_modelHandle) {
            _modelHandle = AssetManager::get()->loadModel(AssetManager::ModelLoadRequest{
                .filepath = _sourceModelPath,
            });
        }
        if (!_modelHandle) {
            // No resource layer can load models; terminal until rebound.
            _cachedMesh = nullptr;
            _bResolved  = false;
            return false;
        }

        const Model* model = _modelHandle->resource.get();
        if (_modelHandle->state == EAssetSlotState::Ready && model) {
            if (_meshIndex < model->getMeshCount()) {
                _cachedMesh = model->getMesh(_meshIndex).get();
                _bResolved  = _cachedMesh != nullptr;
                return _bResolved;
            }
            // Ready model without that mesh index is terminal: one warning,
            // no per-frame retry (rebinding the path is what re-attempts).
            YA_CORE_WARN("MeshSource: Model '{}' has no mesh[{}]",
                         _sourceModelPath,
                         _meshIndex);
            _cachedMesh = nullptr;
            _bResolved  = false;
            return false;
        }

        if (_modelHandle->state == EAssetSlotState::Loading) {
            // A decode is in flight; the processor holds a slot subscription
            // whose fill re-enqueues this component, so no polling here.
            _cachedMesh = nullptr;
            _bResolved  = false;
            return false;
        }

        // Failed: terminal until the path is rebound or the asset reloaded;
        // staying silent would hide a broken asset forever.
        YA_CORE_WARN("MeshSource: Model '{}' failed to load; mesh[{}] stays unresolved",
                     _sourceModelPath,
                     _meshIndex);
        _cachedMesh = nullptr;
        _bResolved  = false;
        return false;
    }

    // Priority 1: Built-in primitive geometry
    if (_primitiveGeometry != EPrimitiveGeometry::None) {
        if (_bResolved) {
            // Primitives never reload; the cache entry is enough.
            return true;
        }
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
