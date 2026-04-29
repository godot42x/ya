#pragma once

#include "Core/Base.h"
#include "Core/Math/AABB.h"
#include "Render/Mesh.h"
#include "Render/Model/MaterialData.h"
#include "Render/Skeleton.h"

namespace ya
{

struct Model
{
    std::string name;
    std::string filepath;
    std::string directory;

    std::vector<stdptr<Mesh>> meshes;
    AABB                      boundingBox;

    bool isLoaded = false;

    std::vector<MaterialData> embeddedMaterials;
    std::vector<int32_t>      meshMaterialIndices;

    std::vector<std::shared_ptr<Skeleton>> skeletons;
    std::vector<int32_t>                   meshSkeletonIndices;

  public:
    Model()  = default;
    ~Model() = default;

    const std::vector<stdptr<Mesh>>& getMeshes() const { return meshes; }
    std::vector<stdptr<Mesh>>&       getMeshesMut() { return meshes; }

    size_t       getMeshCount() const { return meshes.size(); }
    stdptr<Mesh> getMesh(size_t index) const { return index < meshes.size() ? meshes[index] : nullptr; }

    const std::string& getFilepath() const { return filepath; }
    const std::string& getDirectory() const { return directory; }
    const std::string& getName() const { return name; }

    void setDirectory(const std::string& dir) { directory = dir; }
    void setFilepath(const std::string& path) { filepath = path; }
    void setName(const std::string& modelName) { name = modelName; }

    bool getIsLoaded() const { return isLoaded; }
    void setIsLoaded(bool loaded) { isLoaded = loaded; }

    bool            hasSkeleton() const { return !skeletons.empty(); }
    size_t          getSkeletonCount() const { return skeletons.size(); }
    const Skeleton* getSkeleton(size_t index = 0) const
    {
        return index < skeletons.size() ? skeletons[index].get() : nullptr;
    }
    std::shared_ptr<Skeleton> getSkeletonShared(size_t index = 0) const
    {
        return index < skeletons.size() ? skeletons[index] : nullptr;
    }
    const Skeleton* getSkeletonForMesh(size_t meshIndex) const
    {
        if (meshIndex >= meshSkeletonIndices.size()) {
            return nullptr;
        }
        const int32_t skeletonIndex = meshSkeletonIndices[meshIndex];
        if (skeletonIndex < 0 || skeletonIndex >= static_cast<int32_t>(skeletons.size())) {
            return nullptr;
        }
        return skeletons[static_cast<size_t>(skeletonIndex)].get();
    }

    const MaterialData* getMaterialForMesh(size_t meshIndex) const
    {
        if (meshIndex >= meshMaterialIndices.size()) {
            return nullptr;
        }
        int32_t matIndex = meshMaterialIndices[meshIndex];
        if (matIndex < 0 || matIndex >= static_cast<int32_t>(embeddedMaterials.size())) {
            return nullptr;
        }
        return &embeddedMaterials[matIndex];
    }

    const std::vector<MaterialData>& getEmbeddedMaterials() const { return embeddedMaterials; }

    int32_t getMaterialIndex(size_t meshIndex) const
    {
        return meshIndex < meshMaterialIndices.size() ? meshMaterialIndices[meshIndex] : -1;
    }

    int32_t getMeshSkeletonIndex(size_t meshIndex) const
    {
        return meshIndex < meshSkeletonIndices.size() ? meshSkeletonIndices[meshIndex] : -1;
    }
};


} // namespace ya
