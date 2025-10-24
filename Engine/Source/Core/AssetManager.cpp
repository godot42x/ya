#include "AssetManager.h"


#include "assimp/Importer.hpp"
#include "assimp/ObjMaterial.h"
#include "assimp/mesh.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"


#include "Core/FileSystem/FileSystem.h"
#include "Core/Log.h"

namespace ya
{



AssetManager *AssetManager::get()
{
    static AssetManager instance;
    return &instance;
}

void AssetManager::cleanup()
{
    modelCache.clear();
    _textures.clear();
    if (_importer) {
        delete _importer;
        _importer = nullptr;
    }
}

AssetManager::AssetManager()
{
    _importer = new Assimp::Importer();
}

std::shared_ptr<Model> AssetManager::loadModel(const std::string &filepath, std::shared_ptr<CommandBuffer> commandBuffer)
{
    // Check if the model is already loaded
    if (isModelLoaded(filepath)) {
        return modelCache[filepath];
    }

    // Create a new model
    auto             model = std::make_shared<Model>();
    Assimp::Importer importer;

    // Get directory path for texture loading
    size_t      lastSlash = filepath.find_last_of("/\\");
    std::string directory = (lastSlash != std::string::npos) ? filepath.substr(0, lastSlash + 1) : "";

    // Check if file exists using FileSystem
    if (!FileSystem::get()->isFileExists(filepath)) {
        YA_CORE_ERROR("Model file does not exist: {}", filepath);
        return nullptr;
    }

    // Read the file using FileSystem
    std::string fileContent;
    if (!FileSystem::get()->readFileToString(filepath, fileContent)) {
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

    // Process all meshes in the scene
    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        aiMesh *mesh = scene->mMeshes[i];
        Mesh    newMesh;

        // Get mesh name
        newMesh.name = mesh->mName.length > 0 ? mesh->mName.C_Str() : "unnamed_mesh";

        // Process vertices
        for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
            Vertex vertex;

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

            newMesh.vertices.push_back(std::move(vertex));
        }

        // Process indices
        for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
            aiFace face = mesh->mFaces[j];
            for (unsigned int k = 0; k < face.mNumIndices; k++) {
                newMesh.indices.push_back(face.mIndices[k]);
            }
        }

        // Process materials/textures
        // if (mesh->mMaterialIndex >= 0 && commandBuffer) {
        //     aiMaterial *material = scene->mMaterials[mesh->mMaterialIndex];

        //     // Load diffuse texture
        //     if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
        //         aiString texturePath;
        //         if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texturePath) == AI_SUCCESS) {
        //             std::string fullPath = (std::filesystem::path(directory) / texturePath.C_Str()).string();
        //             if (FileSystem::get()->isFileExists(fullPath)) {
        //                 newMesh.diffuseTexture = Texture::CreateFromFile(fullPath, commandBuffer);
        //             }
        //         }
        //     }
        //     // You can add more texture types here (normal maps, specular maps, etc.)
        // }

        model->getMeshes().push_back(newMesh);
    }

    model->setIsLoaded(true);
    model->setDirectory(directory);

    // Cache the model
    modelCache[filepath] = model;

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
        return _textures.find(filepath)->second;
    }

    auto texture = std::make_shared<Texture>(filepath);
    if (!texture) {
        YA_CORE_WARN("Failed to create texture: {}", filepath);
    }
    else {
        _textures[filepath] = texture;
    }
    return texture;
}

std::shared_ptr<Texture> AssetManager::loadTexture(const std::string &name, const std::string &filepath)
{
    if (isTextureLoaded(name)) {
        return _textures.find(name)->second;
    }

    auto texture    = std::make_shared<Texture>(filepath);
    texture->_label = name;
    if (!texture) {
        YA_CORE_WARN("Failed to create texture: {}", filepath);
    }
    else {
        _textures[name] = texture;
    }
    return texture;
}

} // namespace ya