#include "Render/Model/ModelDecodeInternal.h"

#include "Resource/Model/TinyGLTFSupport.h"

#include "Core/System/PathUtils.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>

namespace ya::model_decode
{
namespace
{

bool ignoreTinyGltfImageData(tinygltf::Image*     image,
                             int                  imageIndex,
                             std::string*         err,
                             std::string*         warn,
                             int                  reqWidth,
                             int                  reqHeight,
                             const unsigned char* bytes,
                             int                  size,
                             void*                userData)
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
                         ImportedModelData&         result)
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

    const size_t     vertexCount = static_cast<size_t>(positionAccessor.count);
    ImportedMeshData importedMesh;
    importedMesh.name              = fallbackName;
    importedMesh.sourceCoordSystem = coordSystem;
    importedMesh.vertices.resize(vertexCount);

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
        ModelVertex& vertex = importedMesh.vertices[vertexIndex];
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

        importedMesh.indices = readIndexAccessor(gltfModel, gltfModel.accessors[primitive.indices]);
        if (importedMesh.indices.empty()) {
            YA_CORE_WARN("Skipping glTF primitive '{}': failed to decode index buffer", fallbackName);
            return false;
        }
    }
    else {
        importedMesh.indices.resize(vertexCount);
        for (size_t index = 0; index < vertexCount; ++index) {
            importedMesh.indices[index] = static_cast<uint32_t>(index);
        }
    }

    importedMesh.materialIndex = primitive.material >= 0 ? primitive.material : -1;
    result.meshes.push_back(std::move(importedMesh));
    result.meshMaterialIndices.push_back(primitive.material >= 0 ? primitive.material : -1);
    return true;
}

} // namespace

ImportedModelData decodeWithTinyGltf(const std::string& filepath)
{
    ImportedModelData result;
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
        YA_CORE_ERROR("ImportedModelData::decode: TinyGLTF failed for '{}'", filepath);
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

    constexpr CoordinateSystem coordSystem = CoordinateSystem::RightHanded;
    for (size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex) {
        const tinygltf::Mesh& mesh = model.meshes[meshIndex];
        for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex) {
            const std::string meshName = !mesh.name.empty()
                                           ? mesh.name + "_" + std::to_string(primitiveIndex)
                                           : "gltf_mesh_" + std::to_string(meshIndex) + "_" + std::to_string(primitiveIndex);
            appendGltfPrimitive(model, mesh.primitives[primitiveIndex], coordSystem, meshName, result);
        }
    }

    YA_CORE_INFO("ImportedModelData::decode: '{}' via TinyGLTF -> {} meshes, {} materials",
                 filepath,
                 result.meshes.size(),
                 result.materials.size());

    return result;
}

} // namespace ya::model_decode
