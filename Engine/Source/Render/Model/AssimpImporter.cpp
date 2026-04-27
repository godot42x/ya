#include "Render/Model/AssimpImporter.h"
#include "Render/Model/ModelImporterCommon.h"

#include "Core/System/VirtualFileSystem.h"

#include "assimp/Importer.hpp"
#include "assimp/material.h"
#include "assimp/mesh.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <numeric>
#include <optional>
#include <unordered_set>

namespace ya::model_importer
{
namespace
{

struct AssimpImportContext
{
    ImportedModelData&   result;
    ImportedSkeletonData skeleton;
    CoordinateSystem     coordSystem = ENGINE_COORDINATE_SYSTEM;
};

struct DisjointSet
{
    std::vector<uint32_t> parent;
    std::vector<uint32_t> rank;

    explicit DisjointSet(uint32_t size)
        : parent(size), rank(size, 0)
    {
        std::iota(parent.begin(), parent.end(), 0);
    }

    uint32_t find(uint32_t value)
    {
        if (parent[value] != value) {
            parent[value] = find(parent[value]);
        }
        return parent[value];
    }

    void unite(uint32_t lhs, uint32_t rhs)
    {
        lhs = find(lhs);
        rhs = find(rhs);
        if (lhs == rhs) {
            return;
        }

        if (rank[lhs] < rank[rhs]) {
            std::swap(lhs, rhs);
        }
        parent[rhs] = lhs;
        if (rank[lhs] == rank[rhs]) {
            ++rank[lhs];
        }
    }
};

const char* coordSystemToString(CoordinateSystem coordSystem)
{
    switch (coordSystem) {
    case CoordinateSystem::LeftHanded:
        return "LeftHanded";
    case CoordinateSystem::RightHanded:
        return "RightHanded";
    default:
        return "Unknown";
    }
}

std::array<int, 3> axisVector(int axis, int sign)
{
    std::array<int, 3> axisVector{0, 0, 0};
    if (axis >= 0 && axis < static_cast<int>(axisVector.size())) {
        axisVector[axis] = sign;
    }
    return axisVector;
}

int determinant(const std::array<int, 3>& a, const std::array<int, 3>& b, const std::array<int, 3>& c)
{
    return a[0] * (b[1] * c[2] - b[2] * c[1]) -
           a[1] * (b[0] * c[2] - b[2] * c[0]) +
           a[2] * (b[0] * c[1] - b[1] * c[0]);
}

std::optional<CoordinateSystem> inferCoordSystemFromAssimpMetadata(const aiScene* scene)
{
    if (!scene || !scene->mMetaData) {
        return std::nullopt;
    }

    int upAxis        = 0;
    int upAxisSign    = 1;
    int frontAxis     = 0;
    int frontAxisSign = 1;
    int coordAxis     = 0;
    int coordAxisSign = 1;

    if (!scene->mMetaData->Get("UpAxis", upAxis) ||
        !scene->mMetaData->Get("UpAxisSign", upAxisSign) ||
        !scene->mMetaData->Get("FrontAxis", frontAxis) ||
        !scene->mMetaData->Get("FrontAxisSign", frontAxisSign) ||
        !scene->mMetaData->Get("CoordAxis", coordAxis) ||
        !scene->mMetaData->Get("CoordAxisSign", coordAxisSign)) {
        return std::nullopt;
    }

    const auto right   = axisVector(coordAxis, coordAxisSign);
    const auto up      = axisVector(upAxis, upAxisSign);
    const auto forward = axisVector(frontAxis, frontAxisSign);
    const int  det     = determinant(right, up, forward);
    if (det == 0) {
        return std::nullopt;
    }

    return det > 0 ? CoordinateSystem::RightHanded : CoordinateSystem::LeftHanded;
}

CoordinateSystem inferAssimpCoordSystem(const aiScene* scene, const std::string& filepath)
{
    if (const auto coordSystem = inferCoordSystemFromAssimpMetadata(scene); coordSystem.has_value()) {
        YA_CORE_INFO("Assimp metadata inferred '{}' coordinate system for '{}'",
                     coordSystemToString(*coordSystem),
                     filepath);
        return *coordSystem;
    }

    const auto heuristic = detail::inferCoordSystemFromExtensionHeuristic(filepath);
    YA_CORE_WARN("Assimp metadata missing handedness for '{}', fallback to {} heuristic",
                 filepath,
                 coordSystemToString(heuristic));
    return heuristic;
}

bool tryReadFloatProperty(const aiMaterialProperty* prop, float& value)
{
    if (!prop || prop->mType != aiPTI_Float || prop->mDataLength < sizeof(float)) {
        return false;
    }

    std::memcpy(&value, prop->mData, sizeof(float));
    return true;
}

bool tryReadColor4Property(const aiMaterialProperty* prop, glm::vec4& value)
{
    if (!prop || prop->mType != aiPTI_Float || prop->mDataLength < sizeof(float) * 4) {
        return false;
    }

    float raw[4]{};
    std::memcpy(raw, prop->mData, sizeof(raw));
    value = glm::vec4(raw[0], raw[1], raw[2], raw[3]);
    return true;
}

std::string getFirstTexturePath(const aiMaterial* mat, aiTextureType type)
{
    if (!mat || mat->GetTextureCount(type) == 0) {
        return "";
    }

    aiString path;
    if (mat->GetTexture(type, 0, &path) == AI_SUCCESS) {
        return path.C_Str();
    }

    return "";
}

glm::mat4 toGlmMat4(const aiMatrix4x4& matrix)
{
    return {
        {matrix.a1, matrix.b1, matrix.c1, matrix.d1},
        {matrix.a2, matrix.b2, matrix.c2, matrix.d2},
        {matrix.a3, matrix.b3, matrix.c3, matrix.d3},
        {matrix.a4, matrix.b4, matrix.c4, matrix.d4},
    };
}

void importAssimpCommonParams(const aiMaterial* mat, MaterialData& matData)
{
    if (!mat) {
        return;
    }

    aiColor4D color;
    if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
        matData.setParam(MatParam::BaseColor, glm::vec4(color.r, color.g, color.b, color.a));
    }
    if (mat->Get(AI_MATKEY_COLOR_AMBIENT, color) == AI_SUCCESS) {
        matData.setParam(MatParam::Ambient, glm::vec3(color.r, color.g, color.b));
    }
    if (mat->Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS) {
        matData.setParam(MatParam::Specular, glm::vec3(color.r, color.g, color.b));
    }
    if (mat->Get(AI_MATKEY_COLOR_EMISSIVE, color) == AI_SUCCESS) {
        matData.setParam(MatParam::Emissive, glm::vec3(color.r, color.g, color.b));
    }

    float value = 0.0f;
    if (mat->Get(AI_MATKEY_SHININESS, value) == AI_SUCCESS) {
        matData.setParam(MatParam::Shininess, value);
    }
    if (mat->Get(AI_MATKEY_OPACITY, value) == AI_SUCCESS) {
        matData.setParam(MatParam::Opacity, value);
    }
    if (mat->Get(AI_MATKEY_REFRACTI, value) == AI_SUCCESS) {
        matData.setParam(MatParam::RefractIndex, value);
    }
    if (mat->Get(AI_MATKEY_REFLECTIVITY, value) == AI_SUCCESS) {
        matData.setParam(MatParam::Reflection, value);
    }

    int twoSided = 0;
    if (mat->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS) {
        matData.setParam(MatParam::DoubleSided, twoSided != 0);
    }
}

void importAssimpCommonTextures(const aiMaterial* mat, MaterialData& matData)
{
    if (!mat) {
        return;
    }

    const std::string diffusePath = getFirstTexturePath(mat, aiTextureType_DIFFUSE);
    detail::setTextureAlias(matData, MatTexture::Diffuse, MatTexture::Albedo, diffusePath);
    matData.setTexturePath(MatTexture::Specular, getFirstTexturePath(mat, aiTextureType_SPECULAR));
    matData.setTexturePath(MatTexture::Emissive, getFirstTexturePath(mat, aiTextureType_EMISSIVE));
    matData.setTexturePath(MatTexture::AO, getFirstTexturePath(mat, aiTextureType_LIGHTMAP));

    const std::string normalPath = [&]()
    {
        const std::string normals = getFirstTexturePath(mat, aiTextureType_NORMALS);
        if (!normals.empty()) {
            return normals;
        }
        return getFirstTexturePath(mat, aiTextureType_HEIGHT);
    }();
    matData.setTexturePath(MatTexture::Normal, normalPath);
}

void importRawMaterialHints(const aiMaterial* mat, MaterialData& matData)
{
    if (!mat) {
        return;
    }

    bool isPBRHint   = false;
    bool isUnlitHint = false;

    int shadingModel = 0;
    if (mat->Get(AI_MATKEY_SHADING_MODEL, shadingModel) == AI_SUCCESS) {
        if (shadingModel == aiShadingMode_CookTorrance) {
            isPBRHint = true;
        }
        else if (shadingModel == aiShadingMode_NoShading) {
            isUnlitHint = true;
        }
    }

    for (unsigned int propIndex = 0; propIndex < mat->mNumProperties; ++propIndex) {
        const aiMaterialProperty* prop = mat->mProperties[propIndex];
        if (!prop) {
            continue;
        }

        const std::string key = prop->mKey.C_Str();
        if (key.empty()) {
            continue;
        }

        if (detail::containsInsensitive(key, "unlit")) {
            isUnlitHint = true;
        }
        if (detail::containsInsensitive(key, "metallicfactor")) {
            float metallic = 0.0f;
            if (tryReadFloatProperty(prop, metallic)) {
                matData.setParam(MatParam::Metallic, metallic);
                isPBRHint = true;
            }
            continue;
        }
        if (detail::containsInsensitive(key, "roughnessfactor")) {
            float roughness = 0.0f;
            if (tryReadFloatProperty(prop, roughness)) {
                matData.setParam(MatParam::Roughness, roughness);
                isPBRHint = true;
            }
            continue;
        }
        if (detail::containsInsensitive(key, "basecolorfactor")) {
            glm::vec4 baseColor{1.0f};
            if (tryReadColor4Property(prop, baseColor)) {
                matData.setParam(MatParam::BaseColor, baseColor);
            }
            continue;
        }
        if (detail::containsInsensitive(key, "alphacutoff")) {
            float alphaCutoff = 0.0f;
            if (tryReadFloatProperty(prop, alphaCutoff)) {
                matData.setParam(MatParam::AlphaCutoff, alphaCutoff);
            }
            continue;
        }

        if (prop->mType != aiPTI_String) {
            continue;
        }

        aiString value;
        if (mat->Get(key.c_str(), prop->mSemantic, prop->mIndex, value) != AI_SUCCESS) {
            continue;
        }

        const std::string path = value.C_Str();
        if (path.empty()) {
            continue;
        }

        if (detail::containsInsensitive(key, "alphamode")) {
            matData.setParam(MatParam::AlphaMode, path);
            continue;
        }
        if (detail::containsInsensitive(key, "basecolor")) {
            detail::setTextureAlias(matData, MatTexture::Diffuse, MatTexture::Albedo, path);
            continue;
        }
        if (detail::containsInsensitive(key, "normal")) {
            matData.setTexturePath(MatTexture::Normal, path);
            continue;
        }
        if (detail::containsInsensitive(key, "occlusion")) {
            matData.setTexturePath(MatTexture::AO, path);
            continue;
        }
        if (detail::containsInsensitive(key, "metallicroughness")) {
            matData.setTexturePath(MatTexture::MetallicRoughness, path);
            isPBRHint = true;
            continue;
        }
        if (detail::containsInsensitive(key, "metallic")) {
            matData.setTexturePath(MatTexture::Metallic, path);
            isPBRHint = true;
            continue;
        }
        if (detail::containsInsensitive(key, "roughness")) {
            matData.setTexturePath(MatTexture::Roughness, path);
            isPBRHint = true;
            continue;
        }
    }

    if (isUnlitHint) {
        matData.type = "unlit";
    }
    else if (isPBRHint) {
        matData.type = "pbr";
    }
}

void buildSkeletonNodeHierarchy(const aiNode*         node,
                                uint32_t              parentIndex,
                                const glm::mat4&      parentGlobal,
                                ImportedSkeletonData& skeleton)
{
    const uint32_t  nodeIndex = static_cast<uint32_t>(skeleton.nodes.size());
    const FName     nodeName(node->mName.C_Str());
    const glm::mat4 localTransform  = toGlmMat4(node->mTransformation);
    const glm::mat4 globalTransform = parentGlobal * localTransform;

    skeleton.nodes.push_back(ImportedSkeletonNode{
        .name            = nodeName,
        .parentIndex     = parentIndex,
        .localTransform  = localTransform,
        .globalTransform = globalTransform,
        .children        = {},
    });
    skeleton.nameToNodeIndex[nodeName] = nodeIndex;

    if (parentIndex == INVALID_SKELETON_NODE_INDEX) {
        skeleton.rootNodeIndex = nodeIndex;
    }
    else if (parentIndex < skeleton.nodes.size()) {
        skeleton.nodes[parentIndex].children.push_back(nodeIndex);
    }

    for (uint32_t childIndex = 0; childIndex < node->mNumChildren; ++childIndex) {
        buildSkeletonNodeHierarchy(node->mChildren[childIndex], nodeIndex, globalTransform, skeleton);
    }
}

ImportedSkeletonBoneInfo& getOrCreateBone(ImportedSkeletonData& skeleton, const aiBone* bone)
{
    const FName boneName(bone->mName.C_Str());
    if (const auto it = skeleton.boneNameToIndex.find(boneName); it != skeleton.boneNameToIndex.end()) {
        return skeleton.bones[it->second];
    }

    ImportedSkeletonBoneInfo boneInfo;
    boneInfo.name         = boneName;
    boneInfo.boneIndex    = static_cast<uint32_t>(skeleton.bones.size());
    boneInfo.offsetMatrix = toGlmMat4(bone->mOffsetMatrix);

    if (const auto nodeIt = skeleton.nameToNodeIndex.find(boneName); nodeIt != skeleton.nameToNodeIndex.end()) {
        boneInfo.nodeIndex = nodeIt->second;
    }

    skeleton.boneNameToIndex[boneName] = boneInfo.boneIndex;
    skeleton.bones.push_back(boneInfo);
    return skeleton.bones.back();
}


ImportedMeshData buildImportedMesh(const aiMesh* mesh, CoordinateSystem coordSystem)
{
    ImportedMeshData importedMesh;
    importedMesh.name              = mesh->mName.length > 0 ? mesh->mName.C_Str() : "unnamed_mesh";
    importedMesh.sourceCoordSystem = coordSystem;
    importedMesh.materialIndex     = static_cast<int32_t>(mesh->mMaterialIndex);

    importedMesh.vertices.reserve(mesh->mNumVertices);
    for (uint32_t vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex) {
        ImportedModelVertex vertex;
        vertex.position = {mesh->mVertices[vertexIndex].x, mesh->mVertices[vertexIndex].y, mesh->mVertices[vertexIndex].z};
        if (mesh->HasNormals()) {
            vertex.normal = {mesh->mNormals[vertexIndex].x, mesh->mNormals[vertexIndex].y, mesh->mNormals[vertexIndex].z};
        }
        if (mesh->mTextureCoords[0]) {
            vertex.texCoord = {mesh->mTextureCoords[0][vertexIndex].x, mesh->mTextureCoords[0][vertexIndex].y};
        }
        if (mesh->HasVertexColors(0)) {
            vertex.color = {mesh->mColors[0][vertexIndex].r, mesh->mColors[0][vertexIndex].g, mesh->mColors[0][vertexIndex].b, mesh->mColors[0][vertexIndex].a};
        }
        if (mesh->HasTangentsAndBitangents()) {
            vertex.tangent = {mesh->mTangents[vertexIndex].x, mesh->mTangents[vertexIndex].y, mesh->mTangents[vertexIndex].z};
        }
        importedMesh.vertices.push_back(vertex);
    }

    for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
        const aiFace& face = mesh->mFaces[faceIndex];
        for (uint32_t index = 0; index < face.mNumIndices; ++index) {
            importedMesh.indices.push_back(face.mIndices[index]);
        }
    }

    return importedMesh;
}

void appendAnimationClip(const aiAnimation* rawAnimation, ImportedSkeletonData& skeleton)
{
    ImportedSkeletonAnimationClip clip;
    clip.name           = rawAnimation->mName.C_Str();
    clip.duration       = rawAnimation->mDuration;
    clip.ticksPerSecond = rawAnimation->mTicksPerSecond;
    clip.channels.reserve(rawAnimation->mNumChannels);

    for (uint32_t channelIndex = 0; channelIndex < rawAnimation->mNumChannels; ++channelIndex) {
        const aiNodeAnim* channel = rawAnimation->mChannels[channelIndex];
        if (!channel) {
            continue;
        }

        clip.channels.push_back(ImportedSkeletonAnimationChannel{
            .targetName = FName(channel->mNodeName.C_Str()),
        });
    }

    skeleton.animations.push_back(std::move(clip));
    (void)rawAnimation->mMeshChannels;
}

void appendMaterial(const aiMaterial* material, const std::string& directory, MaterialData& outMaterial)
{
    YA_CORE_ASSERT(material, "invalid assimp material");

    outMaterial.directory = directory;

    aiString matName;
    if (material->Get(AI_MATKEY_NAME, matName) == AI_SUCCESS) {
        outMaterial.name = matName.C_Str();
    }

    importAssimpCommonParams(material, outMaterial);
    importAssimpCommonTextures(material, outMaterial);
    importRawMaterialHints(material, outMaterial);
}

uint32_t findParentBoneIndex(const ImportedSkeletonData& skeleton, uint32_t boneIndex)
{
    if (boneIndex >= skeleton.bones.size()) {
        return INVALID_SKELETON_BONE_INDEX;
    }

    uint32_t nodeIndex = skeleton.bones[boneIndex].nodeIndex;
    if (nodeIndex == INVALID_SKELETON_NODE_INDEX || nodeIndex >= skeleton.nodes.size()) {
        return INVALID_SKELETON_BONE_INDEX;
    }

    uint32_t parentNodeIndex = skeleton.nodes[nodeIndex].parentIndex;
    while (parentNodeIndex != INVALID_SKELETON_NODE_INDEX) {
        const FName parentNodeName = skeleton.nodes[parentNodeIndex].name;
        if (const auto boneIt = skeleton.boneNameToIndex.find(parentNodeName); boneIt != skeleton.boneNameToIndex.end()) {
            return boneIt->second;
        }
        parentNodeIndex = skeleton.nodes[parentNodeIndex].parentIndex;
    }

    return INVALID_SKELETON_BONE_INDEX;
}

void splitSkeletonsByConnectivity(ImportedModelData& result)
{
    if (result.skeletons.empty()) {
        return;
    }

    ImportedSkeletonData mergedSkeleton = std::move(result.skeletons.front());
    result.skeletons.clear();

    if (mergedSkeleton.bones.empty()) {
        if (!mergedSkeleton.isEmpty()) {
            result.skeletons.push_back(std::move(mergedSkeleton));
        }
        return;
    }

    DisjointSet dsu(static_cast<uint32_t>(mergedSkeleton.bones.size()));
    for (uint32_t boneIndex = 0; boneIndex < mergedSkeleton.bones.size(); ++boneIndex) {
        const uint32_t parentBoneIndex = findParentBoneIndex(mergedSkeleton, boneIndex);
        if (parentBoneIndex != INVALID_SKELETON_BONE_INDEX) {
            dsu.unite(boneIndex, parentBoneIndex);
        }
    }

    for (size_t meshIndex = 0; meshIndex < result.meshes.size(); ++meshIndex) {
        if (meshIndex >= result.meshBaseVertexIndex.size()) {
            continue;
        }

        std::unordered_set<uint32_t> meshBones;
        const size_t                 baseVertexIndex = result.meshBaseVertexIndex[meshIndex];
        const size_t                 vertexCount     = result.meshes[meshIndex].vertices.size();
        for (size_t localVertexIndex = 0; localVertexIndex < vertexCount; ++localVertexIndex) {
            const size_t globalVertexIndex = baseVertexIndex + localVertexIndex;
            if (globalVertexIndex >= result.vertexBoneData.size()) {
                break;
            }

            for (const auto boneIndex : result.vertexBoneData[globalVertexIndex].boneIDs) {
                if (boneIndex >= 0) {
                    meshBones.insert(static_cast<uint32_t>(boneIndex));
                }
            }
        }

        if (meshBones.size() <= 1) {
            continue;
        }

        auto           boneIt    = meshBones.begin();
        const uint32_t firstBone = *boneIt++;
        for (; boneIt != meshBones.end(); ++boneIt) {
            dsu.unite(firstBone, *boneIt);
        }
    }

    std::unordered_map<uint32_t, uint32_t> componentRootToIndex;
    std::vector<uint32_t>                  boneToComponent(mergedSkeleton.bones.size(), 0);
    for (uint32_t boneIndex = 0; boneIndex < mergedSkeleton.bones.size(); ++boneIndex) {
        const uint32_t componentRoot = dsu.find(boneIndex);
        auto [it, inserted]          = componentRootToIndex.emplace(componentRoot, static_cast<uint32_t>(componentRootToIndex.size()));
        (void)inserted;
        boneToComponent[boneIndex] = it->second;
    }

    const uint32_t componentCount = static_cast<uint32_t>(componentRootToIndex.size());
    result.skeletons.resize(componentCount);
    std::vector<std::unordered_map<uint32_t, uint32_t>> oldBoneToNewBone(componentCount);
    std::vector<std::unordered_set<FName>>              relevantNodeNames(componentCount);
    std::vector<std::unordered_set<FName>>              relevantBoneNames(componentCount);

    for (uint32_t componentIndex = 0; componentIndex < componentCount; ++componentIndex) {
        ImportedSkeletonData& splitSkeleton = result.skeletons[componentIndex];
        splitSkeleton.name                  = componentCount == 1 ? mergedSkeleton.name : mergedSkeleton.name + "_" + std::to_string(componentIndex);
        splitSkeleton.nodes                 = mergedSkeleton.nodes;
        splitSkeleton.nameToNodeIndex       = mergedSkeleton.nameToNodeIndex;
        splitSkeleton.rootNodeIndex         = mergedSkeleton.rootNodeIndex;
    }

    for (uint32_t boneIndex = 0; boneIndex < mergedSkeleton.bones.size(); ++boneIndex) {
        const uint32_t        componentIndex = boneToComponent[boneIndex];
        ImportedSkeletonData& splitSkeleton  = result.skeletons[componentIndex];

        ImportedSkeletonBoneInfo splitBone            = mergedSkeleton.bones[boneIndex];
        splitBone.boneIndex                           = static_cast<uint32_t>(splitSkeleton.bones.size());
        splitSkeleton.boneNameToIndex[splitBone.name] = splitBone.boneIndex;
        oldBoneToNewBone[componentIndex][boneIndex]   = splitBone.boneIndex;
        relevantBoneNames[componentIndex].insert(splitBone.name);

        uint32_t nodeIndex = splitBone.nodeIndex;
        while (nodeIndex != INVALID_SKELETON_NODE_INDEX && nodeIndex < mergedSkeleton.nodes.size()) {
            relevantNodeNames[componentIndex].insert(mergedSkeleton.nodes[nodeIndex].name);
            nodeIndex = mergedSkeleton.nodes[nodeIndex].parentIndex;
        }

        splitSkeleton.bones.push_back(splitBone);
    }

    for (size_t meshIndex = 0; meshIndex < result.meshes.size(); ++meshIndex) {
        if (meshIndex >= result.meshBaseVertexIndex.size()) {
            if (meshIndex < result.meshSkeletonIndices.size()) {
                result.meshSkeletonIndices[meshIndex] = -1;
            }
            continue;
        }

        std::unordered_set<uint32_t> meshComponents;
        const size_t                 baseVertexIndex = result.meshBaseVertexIndex[meshIndex];
        const size_t                 vertexCount     = result.meshes[meshIndex].vertices.size();
        for (size_t localVertexIndex = 0; localVertexIndex < vertexCount; ++localVertexIndex) {
            const size_t globalVertexIndex = baseVertexIndex + localVertexIndex;
            if (globalVertexIndex >= result.vertexBoneData.size()) {
                break;
            }

            auto& boneData = result.vertexBoneData[globalVertexIndex];
            for (auto boneIndex : boneData.boneIDs) {
                if (boneIndex < 0) {
                    continue;
                }
                const uint32_t componentIndex = boneToComponent[static_cast<uint32_t>(boneIndex)];
                meshComponents.insert(componentIndex);
                boneIndex = static_cast<int32_t>(oldBoneToNewBone[componentIndex][static_cast<uint32_t>(boneIndex)]);
            }
        }

        int32_t meshSkeletonIndex = -1;
        if (!meshComponents.empty()) {
            YA_CORE_ASSERT(meshComponents.size() == 1,
                           "Mesh '{}' references multiple skeleton components after split",
                           result.meshes[meshIndex].name);
            meshSkeletonIndex = static_cast<int32_t>(*meshComponents.begin());
            result.skeletons[static_cast<size_t>(meshSkeletonIndex)].meshIndices.push_back(static_cast<uint32_t>(meshIndex));
        }

        if (meshIndex < result.meshSkeletonIndices.size()) {
            result.meshSkeletonIndices[meshIndex] = meshSkeletonIndex;
        }
    }

    for (uint32_t componentIndex = 0; componentIndex < componentCount; ++componentIndex) {
        for (const ImportedSkeletonAnimationClip& importedClip : mergedSkeleton.animations) {
            ImportedSkeletonAnimationClip splitClip;
            splitClip.name           = importedClip.name;
            splitClip.duration       = importedClip.duration;
            splitClip.ticksPerSecond = importedClip.ticksPerSecond;

            for (const ImportedSkeletonAnimationChannel& channel : importedClip.channels) {
                if (relevantBoneNames[componentIndex].contains(channel.targetName) ||
                    relevantNodeNames[componentIndex].contains(channel.targetName)) {
                    splitClip.channels.push_back(channel);
                }
            }

            if (!splitClip.channels.empty()) {
                result.skeletons[componentIndex].animations.push_back(std::move(splitClip));
            }
        }
    }

    if (componentCount > 1) {
        YA_CORE_INFO("Model '{}' split into {} skeletons", result.filepath, componentCount);
    }
}

void importSkeletonData(const aiScene* scene, AssimpImportContext& context)
{
    context.skeleton.name = std::filesystem::path(context.result.filepath).stem().string();
    // build tree
    buildSkeletonNodeHierarchy(scene->mRootNode,
                               INVALID_SKELETON_NODE_INDEX,
                               glm::mat4(1.0f),
                               context.skeleton);

    //  allocate bone vertex memory
    context.result.meshBaseVertexIndex.resize(scene->mNumMeshes);
    uint32_t totalVertices = 0;
    for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        const aiMesh* mesh = scene->mMeshes[meshIndex];

        context.result.meshBaseVertexIndex[meshIndex] = totalVertices;
        totalVertices += mesh->mNumVertices;
        context.result.vertexBoneData.resize(totalVertices);
    }

    for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        const aiMesh* mesh = scene->mMeshes[meshIndex];
        if (!mesh->HasBones()) {
            continue;
        }

        // each mesh's each bone
        for (uint32_t boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {

            auto bone = mesh->mBones[boneIndex];

            //  bone can be shared in meshes, identify by name
            auto& boneInfo = getOrCreateBone(context.skeleton, bone);

            // don't know why it's  here, maybe for split skeleton
            if (std::ranges::find(context.skeleton.meshIndices, meshIndex) == context.skeleton.meshIndices.end()) {
                context.skeleton.meshIndices.push_back(meshIndex);
            }

            // a bone affect n vertex
            for (uint32_t weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {
                const aiVertexWeight& weight = bone->mWeights[weightIndex];

                const uint32_t globalVertexID = context.result.meshBaseVertexIndex[meshIndex] + weight.mVertexId;
                YA_CORE_ASSERT(globalVertexID < context.result.vertexBoneData.size(), "Global vertex ID is out of bounds");

                context.result.vertexBoneData[globalVertexID].boneIDs.push_back(boneInfo.boneIndex);
                context.result.vertexBoneData[globalVertexID].weights.push_back(weight.mWeight);
            }
        }
    }

    for (uint32_t animationIndex = 0; animationIndex < scene->mNumAnimations; ++animationIndex) {
        appendAnimationClip(scene->mAnimations[animationIndex], context.skeleton);
    }

    if (context.skeleton.isEmpty()) {
        return;
    }

    context.result.skeletons.push_back(std::move(context.skeleton));
    splitSkeletonsByConnectivity(context.result);
}

void importMeshData(const aiScene* scene, AssimpImportContext& context)
{
    context.result.meshes.reserve(scene->mNumMeshes);
    context.result.meshMaterialIndices.reserve(scene->mNumMeshes);
    context.result.meshSkeletonIndices.reserve(scene->mNumMeshes);

    for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        const aiMesh* mesh     = scene->mMeshes[meshIndex];
        const char*   meshName = mesh->mName.length > 0 ? mesh->mName.C_Str() : "unnamed_mesh";

        if (mesh->mNumVertices < 3 || mesh->mNumFaces == 0) {
            YA_CORE_WARN("Skipping mesh '{}': insufficient data", meshName);
            context.result.meshMaterialIndices.push_back(-1);
            context.result.meshSkeletonIndices.push_back(-1);
            continue;
        }
        if (!(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE)) {
            YA_CORE_WARN("Skipping mesh '{}': not triangulated", meshName);
            context.result.meshMaterialIndices.push_back(-1);
            context.result.meshSkeletonIndices.push_back(-1);
            continue;
        }

        context.result.meshes.push_back(buildImportedMesh(mesh, context.coordSystem));
        if (mesh->mMaterialIndex < scene->mNumMaterials) {
            context.result.meshMaterialIndices.push_back(static_cast<int32_t>(mesh->mMaterialIndex));
        }
        else {
            context.result.meshMaterialIndices.push_back(-1);
        }
        context.result.meshSkeletonIndices.push_back(-1);
    }
}

void importMaterialData(const aiScene* scene, AssimpImportContext& context)
{
    context.result.materials.reserve(scene->mNumMaterials);
    for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {
        MaterialData material;
        appendMaterial(scene->mMaterials[materialIndex], context.result.directory, material);
        context.result.materials.push_back(std::move(material));
    }
}

} // namespace

ImportedModelData AssimpImporter::import(const std::string& filepath) const
{
    ImportedModelData result;
    result.filepath  = filepath;
    result.directory = std::filesystem::path(filepath).parent_path().generic_string();
    if (!result.directory.empty() && result.directory.back() != '/') {
        result.directory += '/';
    }

    if (!VirtualFileSystem::get()->isFileExists(filepath)) {
        YA_CORE_ERROR("ImportedModelData::decode: file not found: {}", filepath);
        return result;
    }

    constexpr unsigned int assimpFlags = aiProcess_Triangulate |
                                         aiProcess_GenSmoothNormals |
                                         aiProcess_CalcTangentSpace |
                                         aiProcess_JoinIdenticalVertices |
                                         aiProcess_ImproveCacheLocality |
                                         aiProcess_FindDegenerates |
                                         aiProcess_SortByPType |
                                         aiProcess_FindInvalidData |
                                         aiProcess_ValidateDataStructure;

    Assimp::Importer importer;
    const aiScene*   scene = importer.ReadFile(filepath, assimpFlags);

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        YA_CORE_ERROR("ImportedModelData::decode: Assimp error for '{}': {}", filepath, importer.GetErrorString());
        return result;
    }

    AssimpImportContext context{
        .result      = result,
        .coordSystem = inferAssimpCoordSystem(scene, filepath),
    };
    context.result.sourceCoordSystem = context.coordSystem;

    importMeshData(scene, context);
    importSkeletonData(scene, context);
    importMaterialData(scene, context);

    YA_CORE_INFO("ImportedModelData::decode: '{}' -> {} meshes, {} materials, {} skeletons",
                 filepath,
                 result.meshes.size(),
                 result.materials.size(),
                 result.skeletons.size());

    return result;
}

std::string_view AssimpImporter::getName() const
{
    return "AssimpImporter";
}

bool AssimpImporter::supports(std::string_view filepath) const
{
    return !detail::isGltfPath(std::string(filepath));
}

const IModelImporter& getAssimpImporter()
{
    static const AssimpImporter importer;
    return importer;
}

} // namespace ya::model_importer
