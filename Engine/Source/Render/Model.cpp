#include "Model.h"
#include "Resource/Model/TinyGLTFSupport.h"

#include "Core/Log.h"
#include "Core/System/PathUtils.h"
#include "Core/System/VirtualFileSystem.h"

#include "assimp/Importer.hpp"
#include "assimp/material.h"
#include "assimp/mesh.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <string_view>


namespace ya
{

namespace
{

bool             containsInsensitive(std::string_view text, std::string_view token);
CoordinateSystem inferCoordSystem(const std::string& filepath);
void             setTextureAlias(MaterialData& matData, const FName& primary, const FName& alias, const std::string& path);

bool ignoreTinyGltfImageData(tinygltf::Image* image,
                             int              imageIndex,
                             std::string*     err,
                             std::string*     warn,
                             int              reqWidth,
                             int              reqHeight,
                             const unsigned char* bytes,
                             int              size,
                             void*            userData)
{
    (void)image;
    (void)imageIndex;
    (void)err;
    (void)warn;
    (void)reqWidth;
    (void)reqHeight;
    (void)bytes;
    (void)size;
    (void)userData;
    return true;
}

bool isGltfPath(const std::string& filepath)
{
    const std::string extension = path_utils::pathToUtf8String(std::filesystem::path(filepath).extension());
    return containsInsensitive(extension, ".gltf") || containsInsensitive(extension, ".glb");
}

template <typename T>
const T* getBufferDataPtr(const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t extraOffset = 0)
{
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size())) {
        return nullptr;
    }

    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
    if (bufferView.buffer < 0 || bufferView.buffer >= static_cast<int>(model.buffers.size())) {
        return nullptr;
    }

    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
    const size_t            offset = bufferView.byteOffset + accessor.byteOffset + extraOffset;
    if (offset + sizeof(T) > buffer.data.size()) {
        return nullptr;
    }

    return reinterpret_cast<const T*>(buffer.data.data() + offset);
}

size_t componentByteSize(int componentType)
{
    switch (componentType) {
    case TINYGLTF_COMPONENT_TYPE_BYTE:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        return 1;
    case TINYGLTF_COMPONENT_TYPE_SHORT:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        return 2;
    case TINYGLTF_COMPONENT_TYPE_INT:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
    case TINYGLTF_COMPONENT_TYPE_FLOAT:
        return 4;
    case TINYGLTF_COMPONENT_TYPE_DOUBLE:
        return 8;
    default:
        return 0;
    }
}

int typeComponentCount(int type)
{
    switch (type) {
    case TINYGLTF_TYPE_SCALAR:
        return 1;
    case TINYGLTF_TYPE_VEC2:
        return 2;
    case TINYGLTF_TYPE_VEC3:
        return 3;
    case TINYGLTF_TYPE_VEC4:
    case TINYGLTF_TYPE_MAT2:
        return 4;
    case TINYGLTF_TYPE_MAT3:
        return 9;
    case TINYGLTF_TYPE_MAT4:
        return 16;
    default:
        return 0;
    }
}

double normalizeSignedValue(double value, double maxMagnitude)
{
    return std::max(-1.0, value / maxMagnitude);
}

double readNumericComponent(const unsigned char* data, int componentType, bool normalized)
{
    switch (componentType) {
    case TINYGLTF_COMPONENT_TYPE_FLOAT:
        return *reinterpret_cast<const float*>(data);
    case TINYGLTF_COMPONENT_TYPE_DOUBLE:
        return *reinterpret_cast<const double*>(data);
    case TINYGLTF_COMPONENT_TYPE_BYTE:
    {
        const auto value = static_cast<double>(*reinterpret_cast<const int8_t*>(data));
        return normalized ? normalizeSignedValue(value, 127.0) : value;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
    {
        const auto value = static_cast<double>(*reinterpret_cast<const uint8_t*>(data));
        return normalized ? value / 255.0 : value;
    }
    case TINYGLTF_COMPONENT_TYPE_SHORT:
    {
        const auto value = static_cast<double>(*reinterpret_cast<const int16_t*>(data));
        return normalized ? normalizeSignedValue(value, 32767.0) : value;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
    {
        const auto value = static_cast<double>(*reinterpret_cast<const uint16_t*>(data));
        return normalized ? value / 65535.0 : value;
    }
    case TINYGLTF_COMPONENT_TYPE_INT:
    {
        const auto value = static_cast<double>(*reinterpret_cast<const int32_t*>(data));
        return normalized ? normalizeSignedValue(value, 2147483647.0) : value;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
    {
        const auto value = static_cast<double>(*reinterpret_cast<const uint32_t*>(data));
        return normalized ? value / 4294967295.0 : value;
    }
    default:
        return 0.0;
    }
}

std::optional<std::vector<double>> readAccessorElements(const tinygltf::Model& model, const tinygltf::Accessor& accessor)
{
    if (accessor.bufferView < 0 || accessor.count <= 0) {
        return std::nullopt;
    }

    const tinygltf::BufferView& bufferView     = model.bufferViews[accessor.bufferView];
    const int                   componentCount = typeComponentCount(accessor.type);
    const size_t                componentSize  = componentByteSize(accessor.componentType);
    if (componentCount <= 0 || componentSize == 0) {
        return std::nullopt;
    }

    const auto           strideValue = accessor.ByteStride(bufferView);
    const size_t         stride      = strideValue > 0 ? static_cast<size_t>(strideValue)
                                                       : componentSize * static_cast<size_t>(componentCount);
    const unsigned char* basePtr     = getBufferDataPtr<unsigned char>(model, accessor);
    if (!basePtr) {
        return std::nullopt;
    }

    std::vector<double> values;
    values.resize(static_cast<size_t>(accessor.count) * static_cast<size_t>(componentCount));

    for (size_t elementIndex = 0; elementIndex < static_cast<size_t>(accessor.count); ++elementIndex) {
        const unsigned char* elementPtr = basePtr + elementIndex * stride;
        for (int componentIndex = 0; componentIndex < componentCount; ++componentIndex) {
            const unsigned char* componentPtr = elementPtr + static_cast<size_t>(componentIndex) * componentSize;
            values[elementIndex * static_cast<size_t>(componentCount) + static_cast<size_t>(componentIndex)] =
                readNumericComponent(componentPtr, accessor.componentType, accessor.normalized);
        }
    }

    return values;
}

std::vector<uint32_t> readIndexAccessor(const tinygltf::Model& model, const tinygltf::Accessor& accessor)
{
    std::vector<uint32_t> indices;
    if (accessor.type != TINYGLTF_TYPE_SCALAR) {
        return indices;
    }

    const auto values = readAccessorElements(model, accessor);
    if (!values) {
        return indices;
    }

    indices.reserve(values->size());
    for (double value : *values) {
        if (value < 0.0 || value > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
            return {};
        }
        indices.push_back(static_cast<uint32_t>(value));
    }
    return indices;
}

std::string resolveAssetUri(const std::string& directory, const std::string& uri)
{
    if (uri.empty() || uri.rfind("data:", 0) == 0) {
        return {};
    }

    const std::filesystem::path assetPath(uri);
    if (assetPath.is_absolute()) {
        return path_utils::pathToGenericUtf8String(assetPath);
    }

    return path_utils::pathToGenericUtf8String((std::filesystem::path(directory) / assetPath).lexically_normal());
}

std::string resolveGltfTexturePath(const tinygltf::Model& model, const MaterialData& matData, int textureIndex)
{
    if (textureIndex < 0 || textureIndex >= static_cast<int>(model.textures.size())) {
        return {};
    }

    const tinygltf::Texture& texture = model.textures[textureIndex];
    if (texture.source < 0 || texture.source >= static_cast<int>(model.images.size())) {
        return {};
    }

    const tinygltf::Image& image = model.images[texture.source];
    return resolveAssetUri(matData.directory, image.uri);
}

void importGltfMaterial(const tinygltf::Model& gltfModel, const tinygltf::Material& gltfMaterial, MaterialData& matData)
{
    matData.name = gltfMaterial.name;
    matData.type = "pbr";

    if (gltfMaterial.values.contains("baseColorFactor")) {
        const auto& factor = gltfMaterial.values.at("baseColorFactor").ColorFactor();
        if (factor.size() == 4) {
            matData.setParam(MatParam::BaseColor, glm::vec4(static_cast<float>(factor[0]), static_cast<float>(factor[1]), static_cast<float>(factor[2]), static_cast<float>(factor[3])));
        }
    }

    if (gltfMaterial.values.contains("metallicFactor")) {
        matData.setParam(MatParam::Metallic, static_cast<float>(gltfMaterial.values.at("metallicFactor").Factor()));
    }
    if (gltfMaterial.values.contains("roughnessFactor")) {
        matData.setParam(MatParam::Roughness, static_cast<float>(gltfMaterial.values.at("roughnessFactor").Factor()));
    }

    if (!gltfMaterial.emissiveFactor.empty()) {
        matData.setParam(MatParam::Emissive, glm::vec3(static_cast<float>(gltfMaterial.emissiveFactor[0]), static_cast<float>(gltfMaterial.emissiveFactor[1]), static_cast<float>(gltfMaterial.emissiveFactor[2])));
    }

    if (!gltfMaterial.alphaMode.empty()) {
        matData.setParam(MatParam::AlphaMode, gltfMaterial.alphaMode);
    }
    matData.setParam(MatParam::AlphaCutoff, static_cast<float>(gltfMaterial.alphaCutoff));
    matData.setParam(MatParam::DoubleSided, gltfMaterial.doubleSided);

    if (const std::string baseColorPath = resolveGltfTexturePath(gltfModel, matData, gltfMaterial.pbrMetallicRoughness.baseColorTexture.index);
        !baseColorPath.empty()) {
        setTextureAlias(matData, MatTexture::Diffuse, MatTexture::Albedo, baseColorPath);
    }

    if (const std::string metallicRoughnessPath = resolveGltfTexturePath(gltfModel, matData, gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index);
        !metallicRoughnessPath.empty()) {
        matData.setTexturePath(MatTexture::MetallicRoughness, metallicRoughnessPath);
    }

    if (const std::string normalPath = resolveGltfTexturePath(gltfModel, matData, gltfMaterial.normalTexture.index);
        !normalPath.empty()) {
        matData.setTexturePath(MatTexture::Normal, normalPath);
    }

    if (const std::string occlusionPath = resolveGltfTexturePath(gltfModel, matData, gltfMaterial.occlusionTexture.index);
        !occlusionPath.empty()) {
        matData.setTexturePath(MatTexture::AO, occlusionPath);
    }

    if (const std::string emissivePath = resolveGltfTexturePath(gltfModel, matData, gltfMaterial.emissiveTexture.index);
        !emissivePath.empty()) {
        matData.setTexturePath(MatTexture::Emissive, emissivePath);
    }

    if (gltfMaterial.extensions.contains("KHR_materials_unlit")) {
        matData.type = "unlit";
    }
}

bool appendGltfPrimitive(const tinygltf::Model&     gltfModel,
                         const tinygltf::Primitive& primitive,
                         const CoordinateSystem     coordSystem,
                         const std::string&         fallbackName,
                         DecodedModelData&          result)
{
    if (primitive.mode != TINYGLTF_MODE_TRIANGLES) {
        YA_CORE_WARN("Skipping glTF primitive '{}': only triangle lists are supported", fallbackName);
        return false;
    }

    const auto positionIt = primitive.attributes.find("POSITION");
    if (positionIt == primitive.attributes.end()) {
        YA_CORE_WARN("Skipping glTF primitive '{}': missing POSITION", fallbackName);
        return false;
    }

    if (positionIt->second < 0 || positionIt->second >= static_cast<int>(gltfModel.accessors.size())) {
        YA_CORE_WARN("Skipping glTF primitive '{}': invalid POSITION accessor", fallbackName);
        return false;
    }

    const tinygltf::Accessor& positionAccessor = gltfModel.accessors[positionIt->second];
    if (positionAccessor.type != TINYGLTF_TYPE_VEC3) {
        YA_CORE_WARN("Skipping glTF primitive '{}': POSITION accessor is not vec3", fallbackName);
        return false;
    }

    const auto positions = readAccessorElements(gltfModel, positionAccessor);
    if (!positions) {
        YA_CORE_WARN("Skipping glTF primitive '{}': failed to decode POSITION data", fallbackName);
        return false;
    }

    const size_t                  vertexCount = static_cast<size_t>(positionAccessor.count);
    DecodedModelData::DecodedMesh decodedMesh;
    decodedMesh.name        = fallbackName;
    decodedMesh.coordSystem = coordSystem;
    decodedMesh.data.vertices.resize(vertexCount);

    auto readAttribute = [&](const char* semantic, int expectedType) -> std::optional<std::vector<double>>
    {
        const auto it = primitive.attributes.find(semantic);
        if (it == primitive.attributes.end()) {
            return std::nullopt;
        }
        if (it->second < 0 || it->second >= static_cast<int>(gltfModel.accessors.size())) {
            return std::nullopt;
        }

        const tinygltf::Accessor& accessor = gltfModel.accessors[it->second];
        if (accessor.type != expectedType || static_cast<size_t>(accessor.count) != vertexCount) {
            return std::nullopt;
        }
        return readAccessorElements(gltfModel, accessor);
    };

    const auto normals   = readAttribute("NORMAL", TINYGLTF_TYPE_VEC3);
    const auto texcoords = readAttribute("TEXCOORD_0", TINYGLTF_TYPE_VEC2);
    const auto tangents  = readAttribute("TANGENT", TINYGLTF_TYPE_VEC4);
    const auto colors    = readAttribute("COLOR_0", TINYGLTF_TYPE_VEC4);

    for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
        ModelVertex& vertex = decodedMesh.data.vertices[vertexIndex];
        vertex.position     = {
            static_cast<float>((*positions)[vertexIndex * 3 + 0]),
            static_cast<float>((*positions)[vertexIndex * 3 + 1]),
            static_cast<float>((*positions)[vertexIndex * 3 + 2])};

        if (normals) {
            vertex.normal = {
                static_cast<float>((*normals)[vertexIndex * 3 + 0]),
                static_cast<float>((*normals)[vertexIndex * 3 + 1]),
                static_cast<float>((*normals)[vertexIndex * 3 + 2])};
        }
        if (texcoords) {
            vertex.texCoord = {
                static_cast<float>((*texcoords)[vertexIndex * 2 + 0]),
                static_cast<float>((*texcoords)[vertexIndex * 2 + 1])};
        }
        if (tangents) {
            vertex.tangent = {
                static_cast<float>((*tangents)[vertexIndex * 4 + 0]),
                static_cast<float>((*tangents)[vertexIndex * 4 + 1]),
                static_cast<float>((*tangents)[vertexIndex * 4 + 2])};
        }
        if (colors) {
            vertex.color = {
                static_cast<float>((*colors)[vertexIndex * 4 + 0]),
                static_cast<float>((*colors)[vertexIndex * 4 + 1]),
                static_cast<float>((*colors)[vertexIndex * 4 + 2]),
                static_cast<float>((*colors)[vertexIndex * 4 + 3])};
        }
    }

    if (primitive.indices >= 0) {
        if (primitive.indices >= static_cast<int>(gltfModel.accessors.size())) {
            YA_CORE_WARN("Skipping glTF primitive '{}': invalid index accessor", fallbackName);
            return false;
        }

        decodedMesh.data.indices = readIndexAccessor(gltfModel, gltfModel.accessors[primitive.indices]);
        if (decodedMesh.data.indices.empty()) {
            YA_CORE_WARN("Skipping glTF primitive '{}': failed to decode index buffer", fallbackName);
            return false;
        }
    }
    else {
        decodedMesh.data.indices.resize(vertexCount);
        for (size_t index = 0; index < vertexCount; ++index) {
            decodedMesh.data.indices[index] = static_cast<uint32_t>(index);
        }
    }

    result.meshes.push_back(std::move(decodedMesh));
    result.meshMaterialIndices.push_back(primitive.material >= 0 ? primitive.material : -1);
    return true;
}

DecodedModelData decodeWithTinyGltf(const std::string& filepath)
{
    DecodedModelData result;
    result.filepath  = filepath;
    result.directory = path_utils::pathToGenericUtf8String(std::filesystem::path(filepath).parent_path());
    if (!result.directory.empty() && result.directory.back() != '/') {
        result.directory += '/';
    }

    tinygltf::TinyGLTF loader;
    tinygltf::Model    model;
    std::string        warn;
    std::string        err;

    loader.SetImageLoader(ignoreTinyGltfImageData, nullptr);

    const bool loaded = containsInsensitive(path_utils::pathToUtf8String(std::filesystem::path(filepath).extension()), ".glb")
                          ? loader.LoadBinaryFromFile(&model, &err, &warn, filepath)
                          : loader.LoadASCIIFromFile(&model, &err, &warn, filepath);

    if (!warn.empty()) {
        YA_CORE_WARN("TinyGLTF warning for '{}': {}", filepath, warn);
    }
    if (!err.empty()) {
        YA_CORE_WARN("TinyGLTF detail for '{}': {}", filepath, err);
    }
    if (!loaded) {
        YA_CORE_ERROR("DecodedModelData::decode: TinyGLTF failed for '{}'", filepath);
        return result;
    }

    result.materials.reserve(model.materials.size());
    for (const tinygltf::Material& gltfMaterial : model.materials) {
        MaterialData matData;
        matData.directory = result.directory;
        importGltfMaterial(model, gltfMaterial, matData);
        result.materials.push_back(std::move(matData));
    }

    if (result.materials.empty()) {
        MaterialData defaultMaterial;
        defaultMaterial.directory = result.directory;
        defaultMaterial.type      = "pbr";
        result.materials.push_back(std::move(defaultMaterial));
    }

    const CoordinateSystem coordSystem = inferCoordSystem(filepath);
    for (size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex) {
        const tinygltf::Mesh& mesh = model.meshes[meshIndex];
        for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex) {
            const std::string meshName = !mesh.name.empty()
                                           ? mesh.name + "_" + std::to_string(primitiveIndex)
                                           : "gltf_mesh_" + std::to_string(meshIndex) + "_" + std::to_string(primitiveIndex);
            appendGltfPrimitive(model, mesh.primitives[primitiveIndex], coordSystem, meshName, result);
        }
    }

    YA_CORE_INFO("DecodedModelData::decode: '{}' via TinyGLTF -> {} meshes, {} materials",
                 filepath,
                 result.meshes.size(),
                 result.materials.size());

    return result;
}

bool containsInsensitive(std::string_view text, std::string_view token)
{
    return std::search(
               text.begin(), text.end(), token.begin(), token.end(), [](char lhs, char rhs)
               { return static_cast<char>(std::tolower(static_cast<unsigned char>(lhs))) ==
                        static_cast<char>(std::tolower(static_cast<unsigned char>(rhs))); }) != text.end();
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

void setTextureAlias(MaterialData& matData, const FName& primary, const FName& alias, const std::string& path)
{
    if (path.empty()) {
        return;
    }

    matData.setTexturePath(primary, path);
    matData.setTexturePath(alias, path);
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

CoordinateSystem inferCoordSystem(const std::string& filepath)
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

} // namespace

// ============================================================================
// DecodedModelData::decode — CPU-only Assimp processing (thread-safe)
// ============================================================================

DecodedModelData DecodedModelData::decode(const std::string& filepath)
{
    if (isGltfPath(filepath)) {
        return decodeWithTinyGltf(filepath);
    }

    DecodedModelData result;
    result.filepath  = filepath;
    result.directory = std::filesystem::path(filepath).parent_path().generic_string();
    if (!result.directory.empty() && result.directory.back() != '/') {
        result.directory += '/';
    }

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

    // ── Process meshes (recursive node traversal) ───────────────────────
    std::function<void(aiNode*)> processNode = [&](aiNode* node)
    {
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
                    v.color = {mesh->mColors[0][j].r, mesh->mColors[0][j].g, mesh->mColors[0][j].b, mesh->mColors[0][j].a};

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
                 filepath,
                 result.meshes.size(),
                 result.materials.size());

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
                 filepath,
                 model->meshes.size());

    return model;
}

} // namespace ya
