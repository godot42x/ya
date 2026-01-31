#include "AssetManager.h"

#include "Core/Debug/Instrumentor.h"
#include "assimp/IOStream.hpp"
#include "assimp/IOSystem.hpp"
#include "assimp/Importer.hpp"
#include "assimp/ObjMaterial.h"
#include "assimp/mesh.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"


#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>

namespace ya
{

namespace
{

// ============================================================================
// Custom Assimp IOSystem to bridge VirtualFileSystem
// ============================================================================

/**
 * @brief Custom IOStream implementation for VirtualFileSystem
 *        Wraps a string buffer for read-only access
 */
class VFSIOStream : public Assimp::IOStream
{
  public:
    VFSIOStream(const std::string &path, std::string content)
        : _path(path), _content(std::move(content)), _position(0)
    {
    }

    ~VFSIOStream() override = default;

    // Read operations
    size_t Read(void *pvBuffer, size_t pSize, size_t pCount) override
    {
        size_t bytesToRead = pSize * pCount;
        size_t available   = _content.size() - _position;
        size_t actualRead  = (bytesToRead < available) ? bytesToRead : available;

        if (actualRead > 0) {
            std::memcpy(pvBuffer, _content.data() + _position, actualRead);
            _position += actualRead;
        }

        return actualRead / pSize; // Return number of elements read
    }

    // Write operations (not supported for read-only stream)
    size_t Write(const void *pvBuffer, size_t pSize, size_t pCount) override
    {
        // Read-only stream
        return 0;
    }

    // Seek operations
    aiReturn Seek(size_t pOffset, aiOrigin pOrigin) override
    {
        size_t newPos = _position;

        switch (pOrigin) {
        case aiOrigin_SET:
            newPos = pOffset;
            break;
        case aiOrigin_CUR:
            newPos = _position + pOffset;
            break;
        case aiOrigin_END:
            newPos = _content.size() + pOffset;
            break;
        default:
            return aiReturn_FAILURE;
        }

        if (newPos > _content.size()) {
            return aiReturn_FAILURE;
        }

        _position = newPos;
        return aiReturn_SUCCESS;
    }

    size_t Tell() const override { return _position; }

    size_t FileSize() const override { return _content.size(); }

    void Flush() override
    {
        // No-op for read-only stream
    }

  private:
    std::string _path;
    std::string _content;
    size_t      _position;
};

/**
 * @brief Custom IOSystem implementation for VirtualFileSystem
 *        Bridges Assimp's file I/O to VirtualFileSystem
 */
class VFSIOSystem : public Assimp::IOSystem
{
  public:
    explicit VFSIOSystem(const std::string &baseDir) : _baseDir(baseDir)
    {
        // Normalize base directory path
        if (!_baseDir.empty() && _baseDir.back() != '/' && _baseDir.back() != '\\') {
            _baseDir += '/';
        }
        std::replace(_baseDir.begin(), _baseDir.end(), '\\', '/');
    }

    ~VFSIOSystem() override = default;

    /**
     * @brief Check if a file exists in VirtualFileSystem
     */
    bool Exists(const char *pFile) const override
    {
        if (!pFile) {
            return false;
        }

        std::string fullPath = resolvePath(pFile);
        return VirtualFileSystem::get()->isFileExists(fullPath);
    }

    /**
     * @brief Get the OS-specific path separator
     */
    char getOsSeparator() const override { return '/'; }

    /**
     * @brief Open a file through VirtualFileSystem
     */
    Assimp::IOStream *Open(const char *pFile, const char *pMode = "rb") override
    {
        if (!pFile) {
            YA_CORE_ERROR("VFSIOSystem: Attempted to open null file path");
            return nullptr;
        }

        std::string fullPath = resolvePath(pFile);

        // Read file content through VFS
        std::string content;
        if (!VirtualFileSystem::get()->readFileToString(fullPath, content)) {
            YA_CORE_ERROR("VFSIOSystem: Failed to read file: {}", fullPath);
            return nullptr;
        }

        YA_CORE_TRACE("VFSIOSystem: Opened file: {} (size: {} bytes)", fullPath, content.size());

        return new VFSIOStream(fullPath, std::move(content));
    }

    /**
     * @brief Close an open IOStream
     */
    void Close(Assimp::IOStream *pFile) override
    {
        if (pFile) {
            delete pFile;
        }
    }

    /**
     * @brief Compare two paths (for file system operations)
     */
    bool ComparePaths(const char *first, const char *second) const override
    {
        std::string firstNormalized  = normalizePath(first);
        std::string secondNormalized = normalizePath(second);

        return firstNormalized == secondNormalized;
    }

  private:
    std::string _baseDir;

    /**
     * @brief Resolve a relative path to an absolute path based on base directory
     */
    std::string resolvePath(const char *pFile) const
    {
        if (!pFile) {
            return "";
        }

        std::string path = pFile;

        // If absolute path, use as-is (but normalize separators)
        if (!path.empty() && (path[0] == '/' || (path.size() > 1 && path[1] == ':'))) {
            return normalizePath(path);
        }

        // Relative path: combine with base directory
        if (_baseDir.empty()) {
            return normalizePath(path);
        }

        return normalizePath(_baseDir + path);
    }

    /**
     * @brief Normalize path separators to '/'
     */
    static std::string normalizePath(const std::string &path)
    {
        std::string normalized = path;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        return normalized;
    }
};

// ============================================================================

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
        return nullptr;
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

    // Read the main model file content
    std::string fileContent;
    if (!VirtualFileSystem::get()->readFileToString(filepath, fileContent)) {
        YA_CORE_ERROR("Failed to read model file: {}", filepath);
        return nullptr;
    }

    // Extract file extension as format hint for Assimp
    std::string ext = filepath.substr(filepath.find_last_of('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // Set custom IOSystem to bridge VirtualFileSystem
    // This allows Assimp to load MTL files and textures through VFS
    auto vfsIOSystem = std::make_unique<VFSIOSystem>(directory);
    importer.SetIOHandler(vfsIOSystem.release()); // Transfer ownership to Assimp

    YA_CORE_INFO("Loading model '{}' with VFSIOSystem (base directory: '{}')", filepath, directory);

    // Load the model using Assimp with content and format hint
    // The custom IOSystem will handle loading MTL and textures
    const aiScene *scene = importer.ReadFileFromMemory(
        fileContent.data(),
        fileContent.size(),
        aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            // aiProcess_FlipUVs |
            aiProcess_CalcTangentSpace,
        ext.c_str()); // Format hint (e.g., "obj", "fbx")

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
        // Note: In MTL files, map_Bump is mapped to aiTextureType_HEIGHT, not NORMALS
        auto getTexturePath = [](aiMaterial *mat, aiTextureType type) -> std::string {
            if (mat->GetTextureCount(type) > 0) {
                aiString path;
                if (mat->GetTexture(type, 0, &path) == AI_SUCCESS) {
                    return path.C_Str();
                }
            }
            return "";
        };

        // Debug: Log texture counts for this material
        YA_CORE_TRACE("Material '{}': Diffuse={}, Specular={}, Height={}, Emissive={}",
                      embeddedMat.name,
                      material->GetTextureCount(aiTextureType_DIFFUSE),
                      material->GetTextureCount(aiTextureType_SPECULAR),
                      material->GetTextureCount(aiTextureType_HEIGHT),
                      material->GetTextureCount(aiTextureType_EMISSIVE));

        // TODO: refactor
        embeddedMat.diffuseTexturePath  = getTexturePath(material, aiTextureType_DIFFUSE);
        embeddedMat.specularTexturePath = getTexturePath(material, aiTextureType_SPECULAR);
        embeddedMat.normalTexturePath   = getTexturePath(material, aiTextureType_HEIGHT); // map_Bump -> HEIGHT
        embeddedMat.emissiveTexturePath = getTexturePath(material, aiTextureType_EMISSIVE);

        // Debug: Log loaded texture paths
        if (!embeddedMat.diffuseTexturePath.empty())
            YA_CORE_TRACE("  -> Diffuse: {}", embeddedMat.diffuseTexturePath);
        if (!embeddedMat.specularTexturePath.empty())
            YA_CORE_TRACE("  -> Specular: {}", embeddedMat.specularTexturePath);
        if (!embeddedMat.normalTexturePath.empty())
            YA_CORE_TRACE("  -> Normal: {}", embeddedMat.normalTexturePath);
        if (!embeddedMat.emissiveTexturePath.empty())
            YA_CORE_TRACE("  -> Emissive: {}", embeddedMat.emissiveTexturePath);

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