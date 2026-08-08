#include "Model.h"

#include "Framework/Game/Resource/EngineGeometryNormalizer.h"
#include "Framework/Game/Resource/Model/AssimpImporter.h"
#include "Framework/Game/Resource/Model/GltfImporter.h"
#include "Framework/Game/Resource/Model/ImportedModelData.h"
#include "Framework/Game/Resource/Model/ModelImporterCommon.h"
#include "Framework/Game/Resource/Model/ModelImporterRegistry.h"
#include "Framework/Game/Resource/Skeleton.h"

#include "Foundation/Core/Log.h"

#include <algorithm>
#include <filesystem>

namespace ya
{

ImportedModelData ImportedModelData::decode(const std::string& filepath)
{
    const std::string normalizedFilepath = model_importer::detail::normalizeImportedAssetPath(filepath);

    if (model_importer::detail::isGltfPath(normalizedFilepath)) {
        return model_importer::getGltfImporter().import(normalizedFilepath);
    }

    return model_importer::getAssimpImporter().import(normalizedFilepath);
}

std::shared_ptr<Model> ImportedModelData::createModel(IRender& render) const
{
    auto model      = makeShared<Model>();
    model->filepath = filepath;
    model->setDirectory(directory);

    for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
        auto engineMeshData = buildEngineMeshData(*this, meshIndex);
        auto mesh           = Mesh::create(render, engineMeshData);
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
