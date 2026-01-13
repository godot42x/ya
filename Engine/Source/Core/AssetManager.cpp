#include "AssetManager.h"


#include "Core/Debug/Instrumentor.h"
#include "assimp/Importer.hpp"
#include "assimp/ObjMaterial.h"
#include "assimp/mesh.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"


#include "Core/Log.h"
#include "Core/System/FileSystem.h"


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

void AssetManager::cleanup()
{
    YA_PROFILE_FUNCTION_LOG();
    modelCache.clear();
    _textureViews.clear();
    if (_importer) {
        delete _importer;
        _importer = nullptr;
    }
}

AssetManager::AssetManager()
{
    _importer = new Assimp::Importer();
}

std::shared_ptr<Model> AssetManager::loadModel(const std::string &filepath)
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

        // Get mesh name
        std::string meshName = mesh->mName.length > 0 ? mesh->mName.C_Str() : "unnamed_mesh";

        // Infer coordinate system from file format
        CoordinateSystem sourceCoordSystem = inferAssimpCoordinateSystem(filepath, scene);

        // Temporary storage for vertex data
        std::vector<ModelVertex> modelVertices;
        std::vector<uint32_t>    indices;

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

            modelVertices.push_back(std::move(vertex));
        }

        // Process indices
        for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
            aiFace face = mesh->mFaces[j];
            for (unsigned int k = 0; k < face.mNumIndices; k++) {
                indices.push_back(face.mIndices[k]);
            }
        }

        // Convert ModelVertex to engine Vertex format
        std::vector<ya::Vertex> vertices;
        for (const auto &v : modelVertices) {
            ya::Vertex vertex;
            vertex.position  = v.position;
            vertex.normal    = v.normal;
            vertex.texCoord0 = v.texCoord;
            vertices.push_back(vertex);
        }

        // Create Mesh GPU resource directly
        auto newMesh = makeShared<Mesh>(vertices, indices, meshName, sourceCoordSystem);
        model->getMeshes().push_back(newMesh);
    }

    model->setIsLoaded(true);
    model->setDirectory(directory);

    // Cache the model
    modelCache[filepath] = model;

    return model;
}

std::shared_ptr<Model> AssetManager::loadModel(const std::string &filepath, CoordinateSystem coordSystem)
{
    // Check if the model is already loaded
    if (isModelLoaded(filepath)) {
        return modelCache[filepath];
    }

    // Create a new model
    auto model = makeShared<Model>();

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

        // Get mesh name
        std::string meshName = mesh->mName.length > 0 ? mesh->mName.C_Str() : "unnamed_mesh";

        YA_CORE_INFO("Loading mesh '{}' with explicit coordinate system: {}",
                     meshName,
                     coordSystem == CoordinateSystem::LeftHanded ? "LeftHanded" : "RightHanded");

        // Temporary storage
        std::vector<ModelVertex> modelVertices;
        std::vector<uint32_t>    indices;

        // Process vertices
        for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
            ModelVertex vertex;
            vertex.position = {mesh->mVertices[j].x, mesh->mVertices[j].y, mesh->mVertices[j].z};

            // Check if the mesh has normals
            if (mesh->HasNormals()) {
                vertex.normal = {mesh->mNormals[j].x, mesh->mNormals[j].y, mesh->mNormals[j].z};
            }

            // Check if the mesh has texture coordinates
            if (mesh->mTextureCoords[0]) {
                vertex.texCoord = {mesh->mTextureCoords[0][j].x, mesh->mTextureCoords[0][j].y};
            }

            modelVertices.push_back(vertex);
        }

        // Process indices
        for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
            aiFace face = mesh->mFaces[j];
            for (unsigned int k = 0; k < face.mNumIndices; k++) {
                indices.push_back(face.mIndices[k]);
            }
        }

        // Convert to engine Vertex format
        std::vector<ya::Vertex> vertices;
        for (const auto &v : modelVertices) {
            ya::Vertex vertex;
            vertex.position  = v.position;
            vertex.normal    = v.normal;
            vertex.texCoord0 = v.texCoord;
            vertices.push_back(vertex);
        }

        // Create Mesh with explicit coordinate system
        auto newMesh = makeShared<Mesh>(vertices, indices, meshName, coordSystem);
        model->getMeshes().push_back(newMesh);
    }

    model->setIsLoaded(true);
    model->setDirectory(directory);

    // Cache the model
    modelCache[filepath] = model;

    return model;
}

std::shared_ptr<Model> AssetManager::loadModel(const std::string &name, const std::string &filepath)
{
    auto model = loadModel(filepath);
    if (model) {
        _name2path[name] = filepath;
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

    auto texture = std::make_shared<Texture>(filepath);
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

    auto texture = std::make_shared<Texture>(filepath);
    texture->setLabel(name);
    if (!texture) {
        YA_CORE_WARN("Failed to create texture: {}", filepath);
    }
    else {
        _textureViews[name] = texture;
    }
    return texture;
}

} // namespace ya