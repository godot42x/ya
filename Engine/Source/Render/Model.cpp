#include "Model.h"

#include "Render/EngineGeometryNormalizer.h"
#include "Render/Model/AssimpImporter.h"
#include "Render/Model/GltfImporter.h"
#include "Render/Model/ImportedModelData.h"
#include "Render/Model/ModelImporterCommon.h"
#include "Render/Model/ModelImporterRegistry.h"
#include "Render/Skeleton.h"

#include "Core/Log.h"

#include <algorithm>
#include <filesystem>

namespace ya
{

ImportedModelData ImportedModelData::decode(const std::string& filepath)
{
    std::string normalizedFilepath = filepath;
    std::replace(normalizedFilepath.begin(), normalizedFilepath.end(), '\\', '/');
    normalizedFilepath = std::filesystem::path(normalizedFilepath).lexically_normal().generic_string();

    if (model_importer::detail::isGltfPath(normalizedFilepath)) {
        return model_importer::getGltfImporter().import(normalizedFilepath);
    }

    return model_importer::getAssimpImporter().import(normalizedFilepath);
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
