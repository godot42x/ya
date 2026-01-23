#include "AssetManager.h"


#include "Core/Debug/Instrumentor.h"
#include "assimp/Importer.hpp"
#include "assimp/ObjMaterial.h"
#include "assimp/mesh.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"


#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"


#include <algorithm>
#include <cctype>

namespace ya
{

namespace
{

/**
 * @brief Infer coordinate system from file path and scene metadata
 * @param filepath Path to the model file
 * @param scene Assimp scene (may contain metadata)
 * @return Inferred coordinate system
 */
static CoordinateSystem inferAssimpCoordinateSystem(const std::string &filepath, const aiScene *scene)
{
    // Extract file extension
    std::string ext = filepath.substr(filepath.find_last_of('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // Check scene metadata for coordinate system hints
    if (scene && scene->mMetaData) {
        int upAxis = 1; // Default Y-up
        if (scene->mMetaData->Get("UpAxis", upAxis)) {
            // 0=X, 1=Y, 2=Z
            // Most right-handed systems are Y-up or Z-up
        }

        int upAxisSign = 1;
        scene->mMetaData->Get("UpAxisSign", upAxisSign);

        int frontAxis = 2;
        scene->mMetaData->Get("FrontAxis", frontAxis);

        int coordAxis = 0;
        scene->mMetaData->Get("CoordAxis", coordAxis);

        // Some formats explicitly store handedness
        int coordAxisSign = 1;
        if (scene->mMetaData->Get("CoordAxisSign", coordAxisSign)) {
            // CoordAxisSign can indicate handedness
        }
    }

    // Format-based heuristics (ordered by reliability)
    // Note: Assimp often converts to right-handed Y-up during import

    if (ext == "obj") {
        // Wavefront OBJ: Right-handed, vendor-dependent up axis
        // Blender OBJ: Right-handed, Z-up
        // Most tools: Right-handed
        return CoordinateSystem::RightHanded;
    }
    else if (ext == "fbx") {
        // FBX is complex: can be both left or right handed
        // Unity FBX: Left-handed Y-up
        // Maya/Blender FBX: Right-handed Y-up
        // Default to right-handed (Assimp default conversion)
        return CoordinateSystem::RightHanded;
    }
    else if (ext == "gltf" || ext == "glb") {
        // glTF 2.0 spec: Right-handed, Y-up
        return CoordinateSystem::RightHanded;
    }
    else if (ext == "dae" || ext == "collada") {
        // COLLADA: Right-handed, Y-up (default)
        return CoordinateSystem::RightHanded;
    }
    else if (ext == "blend") {
        // Blender native: Right-handed, Z-up
        return CoordinateSystem::RightHanded;
    }
    else if (ext == "3ds" || ext == "max") {
        // 3ds Max: Right-handed, Z-up
        return CoordinateSystem::RightHanded;
    }
    else if (ext == "stl") {
        // STL has no inherent coordinate system, assume right-handed
        return CoordinateSystem::RightHanded;
    }

    // Unknown format: assume right-handed (most common)
    // Log a warning for manual verification
    YA_CORE_WARN("Unknown model format '{}', assuming RightHanded coordinate system. "
                 "Manually set MeshData.sourceCoordSystem if incorrect.",
                 ext);
    return CoordinateSystem::RightHanded;
}

} // anonymous namespace



AssetManager *AssetManager::get()
{
    static AssetManager instance;
    return &instance;
}

void AssetManager::clearCache()
{
    YA_PROFILE_FUNCTION_LOG();
    modelCache.clear();
    _textureViews.clear();

    YA_CORE_INFO("AssetManager cleared");
}

AssetManager::AssetManager()
{
}

std::shared_ptr<Model> AssetManager::loadModel(const std::string &filepath)
{
    return loadModelImpl(filepath, "");
}



std::shared_ptr<Model> AssetManager::loadModel(const std::string &name, const std::string &filepath)
{
    auto model = loadModelImpl(filepath, name);
    if (model) {
        _modalName2Path[name] = filepath;
    }
    return model;
}



bool AssetManager::isModelLoaded(const std::string &filepath) const
{
    return modelCache.find(filepath) != modelCache.end();
}

std::shared_ptr<Model> AssetManager::getModel(const std::string &filepath) const
{
    if (isModelLoaded(filepath)) {
        return modelCache.at(filepath);
    }
    return nullptr;
}

std::shared_ptr<Texture> AssetManager::loadTexture(const std::string &filepath)
{
    if (isTextureLoaded(filepath)) {
        return _textureViews.find(filepath)->second;
    }

    auto texture = makeShared<Texture>(filepath);
    if (!texture) {
        YA_CORE_WARN("Failed to create texture: {}", filepath);
    }
    else {
        _textureViews[filepath] = texture;
    }
    return texture;
}

std::shared_ptr<Texture> AssetManager::loadTexture(const std::string &name, const std::string &filepath)
{
    if (isTextureLoaded(name)) {
        return _textureViews.find(name)->second;
    }

    auto texture = makeShared<Texture>(filepath);
    texture->setLabel(name);
    if (!texture) {
        YA_CORE_WARN("Failed to create texture: {}", filepath);
    }

    _textureViews[filepath] = texture;
    _textureName2Path[name] = filepath;
    return texture;
}

std::shared_ptr<Model> AssetManager::loadModelImpl(const std::string &filepath, const std::string &identifier)
{
    // Check if the model is already loaded
    if (isModelLoaded(filepath)) {
        return modelCache[filepath];
    }

    // Create a new model
    auto model      = makeShared<Model>();
    model->filepath = filepath;

    Assimp::Importer importer;

    // Get directory path for texture loading
    size_t      lastSlash = filepath.find_last_of("/\\");
    std::string directory = (lastSlash != std::string::npos) ? filepath.substr(0, lastSlash + 1) : "";

    // Check if file exists using VirtualFileSystem
    if (!VirtualFileSystem::get()->isFileExists(filepath)) {
        YA_CORE_ERROR("Model file does not exist: {}", filepath);
        return nullptr;
    }

    // Read the file using VirtualFileSystem
    std::string fileContent;
    if (!VirtualFileSystem::get()->readFileToString(filepath, fileContent)) {
        YA_CORE_ERROR("Failed to read model file: {}", filepath);
        return nullptr;
    }

    // Load the model using Assimp
    const aiScene *scene = importer.ReadFileFromMemory(
        fileContent.data(),
        fileContent.size(),
        aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);


    // Check for errors
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        YA_CORE_ERROR("Assimp error: {}", importer.GetErrorString());
        return nullptr;
    }

    // Storage for processed data
    std::vector<stdptr<Mesh>> meshes;

    // Material data per mesh (indexed by mesh index)
    std::vector<EmbeddedMaterial> embeddedMaterials;
    std::vector<int32_t>          meshMaterialIndices;

    // Process all materials first
    auto processMaterial = [&directory](const aiScene *scene, aiMaterial *material) -> EmbeddedMaterial {
        EmbeddedMaterial embeddedMat;

        if (!material) {
            return embeddedMat; // Return default material
        }

        // Get material name
        aiString matName;
        if (material->Get(AI_MATKEY_NAME, matName) == AI_SUCCESS) {
            embeddedMat.name = matName.C_Str();
        }

        // Get colors
        aiColor4D color;
        if (material->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
            embeddedMat.baseColor = glm::vec4(color.r, color.g, color.b, color.a);
        }
        if (material->Get(AI_MATKEY_COLOR_AMBIENT, color) == AI_SUCCESS) {
            embeddedMat.ambient = glm::vec3(color.r, color.g, color.b);
        }
        if (material->Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS) {
            embeddedMat.specular = glm::vec3(color.r, color.g, color.b);
        }
        if (material->Get(AI_MATKEY_COLOR_EMISSIVE, color) == AI_SUCCESS) {
            embeddedMat.emissive = glm::vec3(color.r, color.g, color.b);
        }

        // Get shininess
        float shininess;
        if (material->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS) {
            embeddedMat.shininess = shininess;
        }

        // Get opacity
        float opacity;
        if (material->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) {
            embeddedMat.opacity = opacity;
        }

        // Get textures (store relative paths)
        auto getTexturePath = [](aiMaterial *mat, aiTextureType type) -> std::string {
            if (mat->GetTextureCount(type) > 0) {
                aiString path;
                if (mat->GetTexture(type, 0, &path) == AI_SUCCESS) {
                    return path.C_Str();
                }
            }
            return "";
        };

        // TODO: refactor
        embeddedMat.diffuseTexturePath  = getTexturePath(material, aiTextureType_DIFFUSE);
        embeddedMat.specularTexturePath = getTexturePath(material, aiTextureType_SPECULAR);
        embeddedMat.normalTexturePath   = getTexturePath(material, aiTextureType_NORMALS);
        embeddedMat.emissiveTexturePath = getTexturePath(material, aiTextureType_EMISSIVE);

        return embeddedMat;
    };

    // Process all materials in the scene
    for (uint32_t i = 0; i < scene->mNumMaterials; ++i) {
        embeddedMaterials.push_back(processMaterial(scene, scene->mMaterials[i]));
    }

    auto processMesh = [&filepath, &meshes, &meshMaterialIndices](const aiScene *scene, aiMesh *mesh) {
        // Get mesh name
        std::string meshName = mesh->mName.length > 0 ? mesh->mName.C_Str() : "unnamed_mesh";

        // Infer coordinate system from file format
        CoordinateSystem sourceCoordSystem = inferAssimpCoordinateSystem(filepath, scene);

        // Temporary storage for vertex data
        MeshData meshData;

        // Process vertices
        for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
            ModelVertex vertex;

            // Position
            vertex.position.x = mesh->mVertices[j].x;
            vertex.position.y = mesh->mVertices[j].y;
            vertex.position.z = mesh->mVertices[j].z;

            // Normal
            if (mesh->HasNormals()) {
                vertex.normal.x = mesh->mNormals[j].x;
                vertex.normal.y = mesh->mNormals[j].y;
                vertex.normal.z = mesh->mNormals[j].z;
            }

            // Texture coordinates
            if (mesh->mTextureCoords[0]) {
                vertex.texCoord.x = mesh->mTextureCoords[0][j].x;
                vertex.texCoord.y = mesh->mTextureCoords[0][j].y;
            }
            else {
                vertex.texCoord = glm::vec2(0.0f, 0.0f);
            }

            // Colors - default white if no colors are available
            if (mesh->HasVertexColors(0)) {
                vertex.color.r = mesh->mColors[0][j].r;
                vertex.color.g = mesh->mColors[0][j].g;
                vertex.color.b = mesh->mColors[0][j].b;
                vertex.color.a = mesh->mColors[0][j].a;
            }
            else {
                vertex.color = glm::vec4(1.0f);
            }

            // if(mesh.texture)

            meshData.vertices.push_back(std::move(vertex));
        }

        // Process indices
        for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
            aiFace face = mesh->mFaces[j];
            for (unsigned int k = 0; k < face.mNumIndices; k++) {
                meshData.indices.push_back(face.mIndices[k]);
            }
        }

        auto newMesh = meshData.createMesh(meshName, sourceCoordSystem);
        meshes.push_back(std::move(newMesh));

        // Record material index for this mesh
        if (mesh->mMaterialIndex >= 0 && mesh->mMaterialIndex < static_cast<int32_t>(scene->mNumMaterials)) {
            meshMaterialIndices.push_back(static_cast<int32_t>(mesh->mMaterialIndex));
        }
        else {
            meshMaterialIndices.push_back(-1); // No material
        }
    };

    std::function<void(const aiScene *, aiNode *)> processNode;
    processNode = [&processMesh, &processNode](const aiScene *scene, aiNode *node) {
        for (unsigned int i = 0; i < node->mNumMeshes; i++) {
            aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
            processMesh(scene, mesh);
        }
        for (unsigned int i = 0; i < node->mNumChildren; i++) {
            processNode(scene, node->mChildren[i]);
        }
    };

    processNode(scene, scene->mRootNode);

    // Assign processed data to model
    model->meshes              = std::move(meshes);
    model->embeddedMaterials   = std::move(embeddedMaterials);
    model->meshMaterialIndices = std::move(meshMaterialIndices);

    YA_CORE_INFO("Loaded model '{}': {} meshes, {} materials",
                 filepath,
                 model->meshes.size(),
                 model->embeddedMaterials.size());

    model->setIsLoaded(true);
    model->setDirectory(directory);

    // Cache the model
    modelCache[filepath] = model;

    return model;
}

} // namespace ya