#include "Model.h"

#include "Render/EngineGeometryNormalizer.h"
#include "Render/Model/ModelDecodeInternal.h"

#include "Core/Log.h"

namespace ya
{

ImportedModelData ImportedModelData::decode(const std::string& filepath)
{
    if (model_decode::isGltfPath(filepath)) {
        return model_decode::decodeWithTinyGltf(filepath);
    }

    return model_decode::decodeWithAssimp(filepath);
}

std::shared_ptr<Model> ImportedModelData::createModel() const
{
    auto model      = makeShared<Model>();
    model->filepath = filepath;
    model->setDirectory(directory);

    for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
        auto engineMeshData = buildEngineMeshData(*this, meshIndex);
        model->meshes.push_back(Mesh::create(engineMeshData));
    }

    model->embeddedMaterials   = materials;
    model->meshMaterialIndices = meshMaterialIndices;
    model->setIsLoaded(true);

    YA_CORE_INFO("ImportedModelData::createModel: '{}' -> {} GPU meshes",
                 filepath,
                 model->meshes.size());

    return model;
}

} // namespace ya
