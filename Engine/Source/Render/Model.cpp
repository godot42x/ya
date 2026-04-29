#include "Model.h"

#include "Render/EngineGeometryNormalizer.h"
#include "Render/Model/ImportedModelData.h"
#include "Render/Model/ModelImporterRegistry.h"
#include "Render/Skeleton.h"

#include "Core/Log.h"

namespace ya
{

ImportedModelData ImportedModelData::decode(const std::string& filepath)
{
    return model_importer::getImporterForPath(filepath).import(filepath);
}

std::shared_ptr<Model> ImportedModelData::createModel() const
{
    auto model      = makeShared<Model>();
    model->filepath = filepath;
    model->setDirectory(directory);

    for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
        auto engineMeshData = buildEngineMeshData(*this, meshIndex);
        auto mesh           = Mesh::create(engineMeshData);
        model->meshes.push_back(std::move(mesh));
    }

    model->embeddedMaterials   = materials;
    model->meshMaterialIndices = meshMaterialIndices;
    model->meshSkeletonIndices = meshSkeletonIndices;

    model->skeletons.reserve(skeletons.size());
    for (const ImportedSkeletonData& importedSkeleton : skeletons) {
        model->skeletons.push_back(createSkeleton(importedSkeleton));
    }

    model->setIsLoaded(true);

    YA_CORE_INFO("ImportedModelData::createModel: '{}' -> {} GPU meshes, {} skeletons",
                 filepath,
                 model->meshes.size(),
                 model->skeletons.size());

    return model;
}

} // namespace ya
