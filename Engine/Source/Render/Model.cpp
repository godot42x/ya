#include "Model.h"

#include "Render/Model/ModelDecodeInternal.h"

#include "Core/Log.h"

namespace ya
{

DecodedModelData DecodedModelData::decode(const std::string& filepath)
{
    if (model_decode::isGltfPath(filepath)) {
        return model_decode::decodeWithTinyGltf(filepath);
    }

    return model_decode::decodeWithAssimp(filepath);
}

std::shared_ptr<Model> DecodedModelData::createModel() const
{
    auto model      = makeShared<Model>();
    model->filepath = filepath;
    model->setDirectory(directory);

    for (const auto& decodedMesh : meshes) {
        MeshData meshDataCopy = decodedMesh.data;
        auto     gpuMesh      = meshDataCopy.createMesh(decodedMesh.name, decodedMesh.coordSystem);
        model->meshes.push_back(std::move(gpuMesh));
    }

    model->embeddedMaterials   = materials;
    model->meshMaterialIndices = meshMaterialIndices;
    model->setIsLoaded(true);

    YA_CORE_INFO("DecodedModelData::createModel: '{}' -> {} GPU meshes",
                 filepath,
                 model->meshes.size());

    return model;
}

} // namespace ya
