#include "Render/Model/ModelDecodeInternal.h"

#include "Core/System/VirtualFileSystem.h"

#include "assimp/Importer.hpp"
#include "assimp/material.h"
#include "assimp/mesh.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include <array>
#include <cstring>
#include <functional>
#include <optional>

namespace ya::model_decode
{
namespace
{

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

    int upAxis = 0;
    int upAxisSign = 1;
    int frontAxis = 0;
    int frontAxisSign = 1;
    int coordAxis = 0;
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

    const auto heuristic = inferCoordSystemFromExtensionHeuristic(filepath);
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
    setTextureAlias(matData, MatTexture::Diffuse, MatTexture::Albedo, diffusePath);
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

        if (containsInsensitive(key, "unlit")) {
            isUnlitHint = true;
        }

        if (containsInsensitive(key, "metallicfactor")) {
            float metallic = 0.0f;
            if (tryReadFloatProperty(prop, metallic)) {
                matData.setParam(MatParam::Metallic, metallic);
                isPBRHint = true;
            }
            continue;
        }

        if (containsInsensitive(key, "roughnessfactor")) {
            float roughness = 0.0f;
            if (tryReadFloatProperty(prop, roughness)) {
                matData.setParam(MatParam::Roughness, roughness);
                isPBRHint = true;
            }
            continue;
        }

        if (containsInsensitive(key, "basecolorfactor")) {
            glm::vec4 baseColor{1.0f};
            if (tryReadColor4Property(prop, baseColor)) {
                matData.setParam(MatParam::BaseColor, baseColor);
            }
            continue;
        }

        if (containsInsensitive(key, "alphacutoff")) {
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

        if (containsInsensitive(key, "alphamode")) {
            matData.setParam(MatParam::AlphaMode, path);
            continue;
        }

        if (containsInsensitive(key, "basecolor")) {
            setTextureAlias(matData, MatTexture::Diffuse, MatTexture::Albedo, path);
            continue;
        }
        if (containsInsensitive(key, "normal")) {
            matData.setTexturePath(MatTexture::Normal, path);
            continue;
        }
        if (containsInsensitive(key, "occlusion")) {
            matData.setTexturePath(MatTexture::AO, path);
            continue;
        }
        if (containsInsensitive(key, "metallicroughness")) {
            matData.setTexturePath(MatTexture::MetallicRoughness, path);
            isPBRHint = true;
            continue;
        }
        if (containsInsensitive(key, "metallic")) {
            matData.setTexturePath(MatTexture::Metallic, path);
            isPBRHint = true;
            continue;
        }
        if (containsInsensitive(key, "roughness")) {
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

} // namespace

ImportedModelData decodeWithAssimp(const std::string& filepath)
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

    constexpr unsigned int assimpFlags =
        aiProcess_Triangulate |
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

    const CoordinateSystem coordSystem = inferAssimpCoordSystem(scene, filepath);

    // materials
    for (uint32_t i = 0; i < scene->mNumMaterials; ++i) {
        aiMaterial*  mat = scene->mMaterials[i];
        MaterialData matData;
        matData.directory = result.directory;

        if (mat) {
            aiString matName;
            if (mat->Get(AI_MATKEY_NAME, matName) == AI_SUCCESS) {
                matData.name = matName.C_Str();
            }

            importAssimpCommonParams(mat, matData);
            importAssimpCommonTextures(mat, matData);
            importRawMaterialHints(mat, matData);
        }

        result.materials.push_back(std::move(matData));
    }



    auto processBone = [&result](aiBone* bone, uint32_t meshIndex)
    {
        result.boneNames.push_back(bone->mName.C_Str());
        for (uint32_t weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {
            const aiVertexWeight& weight         = bone->mWeights[weightIndex];
            uint32_t              globalVertexID = result.meshBaseVertexIndex[meshIndex] + weight.mVertexId;
            YA_CORE_ASSERT(globalVertexID < result.vertex2BoneData.size(), "Global vertex ID is out of bounds");

            result.vertex2BoneData[globalVertexID].boneIDs.push_back(result.boneNames.size() - 1);
            result.vertex2BoneData[globalVertexID].weights.push_back(weight.mWeight);
        }
    };


    auto processMesh = [&result, coordSystem, &scene, &processBone](const aiMesh* mesh, uint32_t meshIndex)
    {
        std::string meshName = mesh->mName.length > 0
                                 ? mesh->mName.C_Str()
                                 : "unnamed_mesh";

        if (mesh->mNumVertices < 3 || mesh->mNumFaces == 0) {
            YA_CORE_WARN("Skipping mesh '{}': insufficient data", meshName);
            result.meshMaterialIndices.push_back(-1);
            return;
        }
        if (!(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE)) {
            YA_CORE_WARN("Skipping mesh '{}': not triangulated", meshName);
            result.meshMaterialIndices.push_back(-1);
            return;
        }

        ImportedMeshData importedMesh;
        importedMesh.name              = meshName;
        importedMesh.sourceCoordSystem = coordSystem;

        // vertices
        importedMesh.vertices.reserve(mesh->mNumVertices);
        for (uint32_t vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex) {
            ModelVertex vertex;
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

        // faces -> triangle indices (if triangulate)
        for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
            const aiFace& face = mesh->mFaces[faceIndex];
            for (uint32_t index = 0; index < face.mNumIndices; ++index) {
                importedMesh.indices.push_back(face.mIndices[index]);
            }
        }

        // material linkage
        importedMesh.materialIndex = static_cast<int32_t>(mesh->mMaterialIndex);
        result.meshes.push_back(std::move(importedMesh));
        if (mesh->mMaterialIndex < scene->mNumMaterials) {
            result.meshMaterialIndices.push_back(static_cast<int32_t>(mesh->mMaterialIndex));
        }
        else {
            result.meshMaterialIndices.push_back(-1);
        }

        // bones
        if (mesh->HasBones()) {
            for (uint32_t i = 0; i < mesh->mNumBones; ++i) {
                auto bone = mesh->mBones[i];
                processBone(bone, meshIndex);
            }
        }
    };

    // mesh and bone entry
    result.meshBaseVertexIndex.resize(scene->mNumMeshes);
    uint32_t totalVertices = 0;
    for (uint32_t i = 0; i < scene->mNumMeshes; ++i) {
        const auto* mesh = scene->mMeshes[i];

        result.meshBaseVertexIndex[i] = totalVertices;
        totalVertices += mesh->mNumVertices;
        result.vertex2BoneData.resize(totalVertices); //

        processMesh(mesh, i);
    }

    // animation
    for (unsigned int animationIndex = 0; animationIndex < scene->mNumAnimations; ++animationIndex) {
        aiAnimation*   rawAnim = scene->mAnimations[animationIndex];
        ModelAnimation anim;
        anim.duration = rawAnim->mDuration;

        (void)rawAnim->mMeshChannels;
    }


    std::function<void(aiNode*)> processNode = [&](aiNode* node)
    {
        for (uint32_t childIndex = 0; childIndex < node->mNumChildren; ++childIndex) {
            processNode(node->mChildren[childIndex]);
        }
    };
    processNode(scene->mRootNode);


    YA_CORE_INFO("ImportedModelData::decode: '{}' -> {} meshes, {} materials",
                 filepath,
                 result.meshes.size(),
                 result.materials.size());

    return result;
}

} // namespace ya::model_decode
