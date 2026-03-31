#include "Model.h"

#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"

#include "assimp/Importer.hpp"
#include "assimp/mesh.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include <algorithm>
#include <cctype>
#include <functional>

namespace ya
{

// ============================================================================
// Coordinate system inference (same logic as AssetManager, factored out)
// ============================================================================

static CoordinateSystem inferCoordSystem(const std::string& filepath)
{
    std::string ext = filepath.substr(filepath.find_last_of('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // Most formats are right-handed after Assimp import
    if (ext == "obj" || ext == "fbx" || ext == "gltf" || ext == "glb" ||
        ext == "dae" || ext == "collada" || ext == "blend" || ext == "3ds" ||
        ext == "max" || ext == "stl") {
        return CoordinateSystem::RightHanded;
    }

    YA_CORE_WARN("Unknown model format '{}', assuming RightHanded", ext);
    return CoordinateSystem::RightHanded;
}

// ============================================================================
// DecodedModelData::decode — CPU-only Assimp processing (thread-safe)
// ============================================================================

DecodedModelData DecodedModelData::decode(const std::string& filepath)
{
    DecodedModelData result;
    result.filepath = filepath;

    // Extract directory
    size_t lastSlash = filepath.find_last_of("/\\");
    result.directory = (lastSlash != std::string::npos) ? filepath.substr(0, lastSlash + 1) : "";

    // Check file existence via VFS
    if (!VirtualFileSystem::get()->isFileExists(filepath)) {
        YA_CORE_ERROR("DecodedModelData::decode: file not found: {}", filepath);
        return result;
    }

    CoordinateSystem coordSystem = inferCoordSystem(filepath);

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
        YA_CORE_ERROR("DecodedModelData::decode: Assimp error for '{}': {}", filepath, importer.GetErrorString());
        return result;
    }

    // ── Process materials ───────────────────────────────────────────────
    for (uint32_t i = 0; i < scene->mNumMaterials; ++i) {
        aiMaterial*  mat = scene->mMaterials[i];
        MaterialData matData;
        matData.type      = "phong";
        matData.directory = result.directory;

        if (mat) {
            aiString matName;
            if (mat->Get(AI_MATKEY_NAME, matName) == AI_SUCCESS) {
                matData.name = matName.C_Str();
            }

            // Colors
            aiColor4D color;
            if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
                matData.setParam(MatParam::BaseColor, glm::vec4(color.r, color.g, color.b, color.a));
            if (mat->Get(AI_MATKEY_COLOR_AMBIENT, color) == AI_SUCCESS)
                matData.setParam(MatParam::Ambient, glm::vec3(color.r, color.g, color.b));
            if (mat->Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS)
                matData.setParam(MatParam::Specular, glm::vec3(color.r, color.g, color.b));
            if (mat->Get(AI_MATKEY_COLOR_EMISSIVE, color) == AI_SUCCESS)
                matData.setParam(MatParam::Emissive, glm::vec3(color.r, color.g, color.b));

            // Scalars
            float val;
            if (mat->Get(AI_MATKEY_SHININESS, val) == AI_SUCCESS)    matData.setParam(MatParam::Shininess, val);
            if (mat->Get(AI_MATKEY_OPACITY, val) == AI_SUCCESS)      matData.setParam(MatParam::Opacity, val);
            if (mat->Get(AI_MATKEY_REFRACTI, val) == AI_SUCCESS)     matData.setParam(MatParam::RefractIndex, val);
            if (mat->Get(AI_MATKEY_REFLECTIVITY, val) == AI_SUCCESS) matData.setParam(MatParam::Reflection, val);

            // Textures
            auto getTexPath = [&](aiTextureType type) -> std::string {
                if (mat->GetTextureCount(type) > 0) {
                    aiString path;
                    if (mat->GetTexture(type, 0, &path) == AI_SUCCESS)
                        return path.C_Str();
                }
                return "";
            };

            matData.setTexturePath(MatTexture::Diffuse,  getTexPath(aiTextureType_DIFFUSE));
            matData.setTexturePath(MatTexture::Specular,  getTexPath(aiTextureType_SPECULAR));
            matData.setTexturePath(MatTexture::Normal,    getTexPath(aiTextureType_HEIGHT)); // map_Bump → HEIGHT
            matData.setTexturePath(MatTexture::Emissive,  getTexPath(aiTextureType_EMISSIVE));
        }

        result.materials.push_back(std::move(matData));
    }

    // ── Process meshes (recursive node traversal) ───────────────────────
    std::function<void(aiNode*)> processNode = [&](aiNode* node) {
        for (uint32_t i = 0; i < node->mNumMeshes; ++i) {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];

            std::string meshName = mesh->mName.length > 0 ? mesh->mName.C_Str() : "unnamed_mesh";

            if (mesh->mNumVertices < 3 || mesh->mNumFaces == 0) {
                YA_CORE_WARN("Skipping mesh '{}': insufficient data", meshName);
                result.meshMaterialIndices.push_back(-1);
                continue;
            }
            if (!(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE)) {
                YA_CORE_WARN("Skipping mesh '{}': not triangulated", meshName);
                result.meshMaterialIndices.push_back(-1);
                continue;
            }

            DecodedMesh dm;
            dm.name        = meshName;
            dm.coordSystem = coordSystem;

            // Vertices
            dm.data.vertices.reserve(mesh->mNumVertices);
            for (uint32_t j = 0; j < mesh->mNumVertices; ++j) {
                ModelVertex v;
                v.position = {mesh->mVertices[j].x, mesh->mVertices[j].y, mesh->mVertices[j].z};

                if (mesh->HasNormals())
                    v.normal = {mesh->mNormals[j].x, mesh->mNormals[j].y, mesh->mNormals[j].z};

                if (mesh->mTextureCoords[0])
                    v.texCoord = {mesh->mTextureCoords[0][j].x, mesh->mTextureCoords[0][j].y};

                if (mesh->HasVertexColors(0))
                    v.color = {mesh->mColors[0][j].r, mesh->mColors[0][j].g,
                               mesh->mColors[0][j].b, mesh->mColors[0][j].a};

                if (mesh->HasTangentsAndBitangents())
                    v.tangent = {mesh->mTangents[j].x, mesh->mTangents[j].y, mesh->mTangents[j].z};

                dm.data.vertices.push_back(v);
            }

            // Indices
            for (uint32_t j = 0; j < mesh->mNumFaces; ++j) {
                const aiFace& face = mesh->mFaces[j];
                for (uint32_t k = 0; k < face.mNumIndices; ++k) {
                    dm.data.indices.push_back(face.mIndices[k]);
                }
            }

            result.meshes.push_back(std::move(dm));

            // Material index
            if (mesh->mMaterialIndex < scene->mNumMaterials) {
                result.meshMaterialIndices.push_back(static_cast<int32_t>(mesh->mMaterialIndex));
            }
            else {
                result.meshMaterialIndices.push_back(-1);
            }
        }

        for (uint32_t i = 0; i < node->mNumChildren; ++i) {
            processNode(node->mChildren[i]);
        }
    };

    processNode(scene->mRootNode);

    YA_CORE_INFO("DecodedModelData::decode: '{}' -> {} meshes, {} materials",
                  filepath, result.meshes.size(), result.materials.size());

    return result;
}

// ============================================================================
// DecodedModelData::createModel — GPU resource creation (main thread only)
// ============================================================================

std::shared_ptr<Model> DecodedModelData::createModel() const
{
    auto model      = makeShared<Model>();
    model->filepath = filepath;
    model->setDirectory(directory);

    // Create GPU Mesh resources from decoded data
    for (const auto& dm : meshes) {
        // MeshData::createMesh needs a mutable copy (it may process indices)
        MeshData meshDataCopy = dm.data;
        auto     gpuMesh      = meshDataCopy.createMesh(dm.name, dm.coordSystem);
        model->meshes.push_back(std::move(gpuMesh));
    }

    model->embeddedMaterials   = materials;
    model->meshMaterialIndices = meshMaterialIndices;
    model->setIsLoaded(true);

    YA_CORE_INFO("DecodedModelData::createModel: '{}' -> {} GPU meshes",
                  filepath, model->meshes.size());

    return model;
}

} // namespace ya
