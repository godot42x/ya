#include "AssetManager.h"

#include "Core/Debug/Instrumentor.h"
#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"
#include "Resource/AssetMeta.h"
#include "Resource/DeferredDeletionQueue.h"
#include "Resource/TextureLibrary.h"
#include "Runtime/App/App.h"
#include "ktx.h"
#include "stb/stb_image.h"
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>

namespace ya
{

namespace
{
struct KtxTextureHandleDeleter
{
    void operator()(ktxTexture* texture) const
    {
        if (texture) {
            ktxTexture_Destroy(texture);
        }
    }
};

using KtxTextureHandle = std::unique_ptr<ktxTexture, KtxTextureHandleDeleter>;

std::string toLowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string textureExtension(const std::string& filepath)
{
    return toLowerCopy(std::filesystem::path(filepath).extension().string());
}

bool isKtxPath(const std::string& filepath)
{
    const std::string extension = textureExtension(filepath);
    return extension == ".ktx" || extension == ".ktx2";
}

uint32_t textureChannelCount(EFormat::T format)
{
    switch (format) {
    case EFormat::R8_UNORM:
    case EFormat::R32_SFLOAT:
    case EFormat::BC4_UNORM_BLOCK:
    case EFormat::BC4_SNORM_BLOCK:
        return 1;
    case EFormat::R8G8_UNORM:
    case EFormat::BC5_UNORM_BLOCK:
    case EFormat::BC5_SNORM_BLOCK:
        return 2;
    case EFormat::BC1_RGB_UNORM_BLOCK:
    case EFormat::BC1_RGB_SRGB_BLOCK:
    case EFormat::ETC2_R8G8B8_UNORM_BLOCK:
    case EFormat::ETC2_R8G8B8_SRGB_BLOCK:
        return 3;
    default:
        return 4;
    }
}

EFormat::T vkFormatToTextureFormat(uint32_t vkFormat)
{
    switch (vkFormat) {
    case VK_FORMAT_R8_UNORM:
        return EFormat::R8_UNORM;
    case VK_FORMAT_R8G8_UNORM:
        return EFormat::R8G8_UNORM;
    case VK_FORMAT_R8G8B8A8_UNORM:
        return EFormat::R8G8B8A8_UNORM;
    case VK_FORMAT_R8G8B8A8_SRGB:
        return EFormat::R8G8B8A8_SRGB;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return EFormat::R16G16B16A16_SFLOAT;
    case VK_FORMAT_R32_SFLOAT:
        return EFormat::R32_SFLOAT;
    case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        return EFormat::BC1_RGB_UNORM_BLOCK;
    case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        return EFormat::BC1_RGB_SRGB_BLOCK;
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        return EFormat::BC1_RGBA_UNORM_BLOCK;
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        return EFormat::BC1_RGBA_SRGB_BLOCK;
    case VK_FORMAT_BC3_UNORM_BLOCK:
        return EFormat::BC3_UNORM_BLOCK;
    case VK_FORMAT_BC3_SRGB_BLOCK:
        return EFormat::BC3_SRGB_BLOCK;
    case VK_FORMAT_BC4_UNORM_BLOCK:
        return EFormat::BC4_UNORM_BLOCK;
    case VK_FORMAT_BC4_SNORM_BLOCK:
        return EFormat::BC4_SNORM_BLOCK;
    case VK_FORMAT_BC5_UNORM_BLOCK:
        return EFormat::BC5_UNORM_BLOCK;
    case VK_FORMAT_BC5_SNORM_BLOCK:
        return EFormat::BC5_SNORM_BLOCK;
    case VK_FORMAT_BC7_UNORM_BLOCK:
        return EFormat::BC7_UNORM_BLOCK;
    case VK_FORMAT_BC7_SRGB_BLOCK:
        return EFormat::BC7_SRGB_BLOCK;
    case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
        return EFormat::ETC2_R8G8B8_UNORM_BLOCK;
    case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
        return EFormat::ETC2_R8G8B8_SRGB_BLOCK;
    case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
        return EFormat::ETC2_R8G8B8A1_UNORM_BLOCK;
    case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
        return EFormat::ETC2_R8G8B8A1_SRGB_BLOCK;
    case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
        return EFormat::ETC2_R8G8B8A8_UNORM_BLOCK;
    case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
        return EFormat::ETC2_R8G8B8A8_SRGB_BLOCK;
    case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
        return EFormat::ASTC_4x4_UNORM_BLOCK;
    case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
        return EFormat::ASTC_4x4_SRGB_BLOCK;
    case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
        return EFormat::ASTC_5x5_UNORM_BLOCK;
    case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
        return EFormat::ASTC_5x5_SRGB_BLOCK;
    case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
        return EFormat::ASTC_6x6_UNORM_BLOCK;
    case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
        return EFormat::ASTC_6x6_SRGB_BLOCK;
    case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
        return EFormat::ASTC_8x8_UNORM_BLOCK;
    case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
        return EFormat::ASTC_8x8_SRGB_BLOCK;
    case VK_FORMAT_ASTC_10x10_UNORM_BLOCK:
        return EFormat::ASTC_10x10_UNORM_BLOCK;
    case VK_FORMAT_ASTC_10x10_SRGB_BLOCK:
        return EFormat::ASTC_10x10_SRGB_BLOCK;
    default:
        return EFormat::Undefined;
    }
}

EFormat::T glInternalFormatToTextureFormat(uint32_t glInternalFormat)
{
    switch (glInternalFormat) {
    case 0x8229:
        return EFormat::R8_UNORM;
    case 0x822B:
        return EFormat::R8G8_UNORM;
    case 0x8058:
        return EFormat::R8G8B8A8_UNORM;
    case 0x8C43:
        return EFormat::R8G8B8A8_SRGB;
    case 0x881A:
        return EFormat::R16G16B16A16_SFLOAT;
    case 0x822E:
        return EFormat::R32_SFLOAT;
    case 0x83F0:
        return EFormat::BC1_RGB_UNORM_BLOCK;
    case 0x8C4C:
        return EFormat::BC1_RGB_SRGB_BLOCK;
    case 0x83F1:
        return EFormat::BC1_RGBA_UNORM_BLOCK;
    case 0x8C4D:
        return EFormat::BC1_RGBA_SRGB_BLOCK;
    case 0x83F3:
        return EFormat::BC3_UNORM_BLOCK;
    case 0x8C4F:
        return EFormat::BC3_SRGB_BLOCK;
    case 0x8DBB:
        return EFormat::BC4_UNORM_BLOCK;
    case 0x8DBC:
        return EFormat::BC4_SNORM_BLOCK;
    case 0x8DBD:
        return EFormat::BC5_UNORM_BLOCK;
    case 0x8DBE:
        return EFormat::BC5_SNORM_BLOCK;
    case 0x8E8C:
        return EFormat::BC7_UNORM_BLOCK;
    case 0x8E8D:
        return EFormat::BC7_SRGB_BLOCK;
    case 0x9274:
        return EFormat::ETC2_R8G8B8_UNORM_BLOCK;
    case 0x9275:
        return EFormat::ETC2_R8G8B8_SRGB_BLOCK;
    case 0x9276:
        return EFormat::ETC2_R8G8B8A1_UNORM_BLOCK;
    case 0x9277:
        return EFormat::ETC2_R8G8B8A1_SRGB_BLOCK;
    case 0x9278:
        return EFormat::ETC2_R8G8B8A8_UNORM_BLOCK;
    case 0x9279:
        return EFormat::ETC2_R8G8B8A8_SRGB_BLOCK;
    case 0x93B0:
        return EFormat::ASTC_4x4_UNORM_BLOCK;
    case 0x93D0:
        return EFormat::ASTC_4x4_SRGB_BLOCK;
    case 0x93B2:
        return EFormat::ASTC_5x5_UNORM_BLOCK;
    case 0x93D2:
        return EFormat::ASTC_5x5_SRGB_BLOCK;
    case 0x93B4:
        return EFormat::ASTC_6x6_UNORM_BLOCK;
    case 0x93D4:
        return EFormat::ASTC_6x6_SRGB_BLOCK;
    case 0x93B7:
        return EFormat::ASTC_8x8_UNORM_BLOCK;
    case 0x93D7:
        return EFormat::ASTC_8x8_SRGB_BLOCK;
    case 0x93BB:
        return EFormat::ASTC_10x10_UNORM_BLOCK;
    case 0x93DB:
        return EFormat::ASTC_10x10_SRGB_BLOCK;
    default:
        return EFormat::Undefined;
    }
}

EFormat::T resolveKtxTextureFormat(const ktxTexture* texture)
{
    if (!texture) {
        return EFormat::Undefined;
    }

    switch (texture->classId) {
    case ktxTexture1_c: {
        auto* texture1 = reinterpret_cast<const ktxTexture1*>(texture);
        return glInternalFormatToTextureFormat(texture1->glInternalformat);
    }
    case ktxTexture2_c: {
        auto* texture2 = reinterpret_cast<const ktxTexture2*>(texture);
        return vkFormatToTextureFormat(texture2->vkFormat);
    }
    default:
        return EFormat::Undefined;
    }
}

KtxTextureHandle loadKtxTexture(const std::string& filepath, ktxTextureCreateFlags createFlags)
{
    ktxTexture* rawTexture = nullptr;
    const auto  result = ktxTexture_CreateFromNamedFile(filepath.c_str(), createFlags, &rawTexture);
    if (result != KTX_SUCCESS || rawTexture == nullptr) {
        YA_CORE_ERROR("KTX load failed for '{}': {}", filepath, ktxErrorString(result));
        return {};
    }

    return KtxTextureHandle(rawTexture);
}

bool applyKtxResolvedSettings(const std::string& filepath,
                              AssetManager::ResolvedTextureImportSettings& settings)
{
    auto texture = loadKtxTexture(filepath, KTX_TEXTURE_CREATE_NO_FLAGS);
    if (!texture) {
        return false;
    }

    const auto format = resolveKtxTextureFormat(texture.get());
    if (format == EFormat::Undefined) {
        if (texture->classId == ktxTexture1_c) {
            auto* texture1 = reinterpret_cast<ktxTexture1*>(texture.get());
            YA_CORE_ERROR("Unsupported KTX1 glInternalformat {} for '{}'", texture1->glInternalformat, filepath);
        }
        else if (texture->classId == ktxTexture2_c) {
            auto* texture2 = reinterpret_cast<ktxTexture2*>(texture.get());
            YA_CORE_ERROR("Unsupported KTX vkFormat {} for '{}'", texture2->vkFormat, filepath);
        }
        return false;
    }

    settings.sourceKind                  = texture->isCompressed ? AssetManager::ETextureSourceKind::Compressed
                                                                 : AssetManager::ETextureSourceKind::Data;
    settings.sourceInfo.width            = texture->baseWidth;
    settings.sourceInfo.height           = texture->baseHeight;
    settings.sourceInfo.detectedChannels = textureChannelCount(format);
    settings.sourceInfo.bIsCompressed    = texture->isCompressed;
    settings.sourceInfo.detectedKind     = settings.sourceKind;
    settings.resolvedFormat              = format;
    settings.resolvedChannels            = textureChannelCount(format);
    settings.colorSpace                  = EFormat::isSRGB(format)
                                            ? AssetManager::ETextureColorSpace::SRGB
                                            : AssetManager::ETextureColorSpace::Linear;

    if (texture->isCompressed) {
        settings.payloadType     = AssetManager::ETexturePayloadType::CompressedBytes;
        settings.decodePrecision = AssetManager::ETextureDecodePrecision::Auto;
        return true;
    }

    if (const auto* traits = AssetManager::getFormatTraits(format)) {
        settings.payloadType     = traits->payloadType;
        settings.decodePrecision = traits->decodePrecision;
        return true;
    }

    YA_CORE_ERROR("Unsupported uncompressed KTX format for '{}'", filepath);
    return false;
}

uint16_t floatToHalf(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));

    const uint32_t sign     = (bits >> 16) & 0x8000u;
    const uint32_t exponent = (bits >> 23) & 0xFFu;
    uint32_t       mantissa = bits & 0x7FFFFFu;

    if (exponent == 255) {
        return static_cast<uint16_t>(sign | 0x7C00u | (mantissa ? 0x0200u : 0u));
    }

    int32_t halfExp = static_cast<int32_t>(exponent) - 127 + 15;
    if (halfExp >= 31) {
        return static_cast<uint16_t>(sign | 0x7C00u);
    }

    if (halfExp <= 0) {
        if (halfExp < -10) {
            return static_cast<uint16_t>(sign);
        }

        mantissa = (mantissa | 0x800000u) >> (1 - halfExp);
        return static_cast<uint16_t>(sign | ((mantissa + 0x1000u) >> 13));
    }

    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(halfExp) << 10) | ((mantissa + 0x1000u) >> 13));
}

AssetManager::TextureMemoryBlock decodeTextureToMemory(const AssetManager::ResolvedTextureImportSettings& settings)
{
    AssetManager::TextureMemoryBlock result;
    result.filepath       = settings.sourceInfo.filepath;
    result.width          = settings.sourceInfo.width;
    result.height         = settings.sourceInfo.height;
    result.channels       = settings.resolvedChannels;
    result.mipLevels      = 1;
    result.format         = settings.resolvedFormat;
    result.payloadType    = settings.payloadType;
    result.importSettings = settings;

    const int desiredChannels = static_cast<int>(settings.resolvedChannels);

    int width    = -1;
    int height   = -1;
    int channels = -1;

    if (isKtxPath(settings.sourceInfo.filepath)) {
        auto texture = loadKtxTexture(settings.sourceInfo.filepath, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT);
        if (!texture) {
            return result;
        }

        if (texture->numFaces != 1 || texture->numLayers != 1 || texture->baseDepth > 1 || texture->isCubemap) {
            YA_CORE_ERROR("decodeTextureToMemory: Only 2D KTX textures are supported for '{}'",
                          settings.sourceInfo.filepath);
            return result;
        }

        if (ktxTexture_NeedsTranscoding(texture.get())) {
            YA_CORE_ERROR("decodeTextureToMemory: Basis-compressed KTX transcoding is not implemented for '{}'",
                          settings.sourceInfo.filepath);
            return result;
        }

        const auto size = static_cast<size_t>(ktxTexture_GetDataSize(texture.get()));
        const auto* data = ktxTexture_GetData(texture.get());
        if (size == 0 || data == nullptr) {
            YA_CORE_ERROR("decodeTextureToMemory: KTX image data is empty for '{}'",
                          settings.sourceInfo.filepath);
            return result;
        }

        result.width     = texture->baseWidth;
        result.height    = texture->baseHeight;
        result.channels  = textureChannelCount(settings.resolvedFormat);
        result.mipLevels = texture->numLevels > 0 ? texture->numLevels : 1;
        result.bytes.resize(size);
        std::memcpy(result.bytes.data(), data, size);
        return result;
    }

    if (settings.payloadType == AssetManager::ETexturePayloadType::U8) {
        stbi_uc* raw = stbi_load(settings.sourceInfo.filepath.c_str(), &width, &height, &channels, desiredChannels);
        if (!raw) {
            YA_CORE_ERROR("decodeTextureToMemory: Failed to load '{}'", settings.sourceInfo.filepath);
            return result;
        }

        result.width  = static_cast<uint32_t>(width);
        result.height = static_cast<uint32_t>(height);
        result.bytes.resize(static_cast<size_t>(result.width) * result.height * result.channels);
        std::memcpy(result.bytes.data(), raw, result.bytes.size());
        stbi_image_free(raw);
        return result;
    }

    float* rawFloat = stbi_loadf(settings.sourceInfo.filepath.c_str(), &width, &height, &channels, desiredChannels);
    if (!rawFloat) {
        YA_CORE_ERROR("decodeTextureToMemory: Failed to load HDR texture '{}'", settings.sourceInfo.filepath);
        return result;
    }

    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(desiredChannels);
    result.width            = static_cast<uint32_t>(width);
    result.height           = static_cast<uint32_t>(height);

    if (settings.payloadType == AssetManager::ETexturePayloadType::F16) {
        result.bytes.resize(pixelCount * sizeof(uint16_t));
        auto* dst = reinterpret_cast<uint16_t*>(result.bytes.data());
        for (size_t i = 0; i < pixelCount; ++i) {
            dst[i] = floatToHalf(rawFloat[i]);
        }
    }
    else if (settings.payloadType == AssetManager::ETexturePayloadType::F32) {
        result.bytes.resize(pixelCount * sizeof(float));
        std::memcpy(result.bytes.data(), rawFloat, result.bytes.size());
    }
    else {
        YA_CORE_ERROR("decodeTextureToMemory: Unsupported payload type {} for '{}'",
                      static_cast<int>(settings.payloadType),
                      settings.sourceInfo.filepath);
        stbi_image_free(rawFloat);
        return {};
    }

    stbi_image_free(rawFloat);

    return result;
}

// ── Enum parse helpers (string from meta JSON → enum) ──────────────────

AssetManager::ETextureColorSpace parseColorSpace(const std::string& raw, AssetManager::ETextureColorSpace fallback)
{
    const std::string s = toLowerCopy(raw);
    if (s == "linear") return AssetManager::ETextureColorSpace::Linear;
    if (s == "srgb")   return AssetManager::ETextureColorSpace::SRGB;
    return fallback;
}

AssetManager::ETextureSourceKind parseSourceKind(const std::string& raw, AssetManager::ETextureSourceKind detected)
{
    const std::string s = toLowerCopy(raw);
    if (s == "hdr")        return AssetManager::ETextureSourceKind::HDR;
    if (s == "ldr")        return AssetManager::ETextureSourceKind::LDR;
    if (s == "data")       return AssetManager::ETextureSourceKind::Data;
    if (s == "compressed") return AssetManager::ETextureSourceKind::Compressed;
    return detected; // "auto" or unrecognized → keep detected kind
}

AssetManager::ETextureChannelPolicy parseChannelPolicy(const std::string& raw)
{
    return (toLowerCopy(raw) == "preserve")
               ? AssetManager::ETextureChannelPolicy::Preserve
               : AssetManager::ETextureChannelPolicy::ForceRGBA;
}

AssetManager::ETextureDecodePrecision parseDecodePrecision(const std::string& raw)
{
    const std::string s = toLowerCopy(raw);
    if (s == "u8")  return AssetManager::ETextureDecodePrecision::U8;
    if (s == "f16") return AssetManager::ETextureDecodePrecision::F16;
    if (s == "f32") return AssetManager::ETextureDecodePrecision::F32;
    return AssetManager::ETextureDecodePrecision::Auto;
}
} // namespace

// ============================================================================
// Singleton
// ============================================================================

AssetManager* AssetManager::get()
{
    static AssetManager instance;
    return &instance;
}

AssetManager::AssetManager()
{
}

// ============================================================================
// IResourceCache
// ============================================================================

void AssetManager::clearCache()
{
    YA_PROFILE_FUNCTION_LOG();
    std::lock_guard lock(_cacheMutex);
    modelCache.clear();
    _textureViews.clear();
    _metaCache.clear();
    _cacheKeyCache.clear();
    _pendingTextureLoads.clear();
    _pendingModelLoads.clear();
    _pendingTextureCallbacks.clear();
    _pendingModelCallbacks.clear();
    _pendingTextureBatchMemoryLoads.clear();
    _readyTextureBatchMemory.clear();

    YA_CORE_INFO("AssetManager cleared");
}

// ============================================================================
// Meta system
// ============================================================================

const AssetMeta& AssetManager::getOrLoadMeta(const std::string& assetPath)
{
    // Check meta cache first
    {
        auto it = _metaCache.find(assetPath);
        if (it != _metaCache.end()) {
            return it->second;
        }
    }

    // Try to load from sidecar file
    std::string metaPath = AssetMeta::metaPathFor(assetPath);

    // Try physical path first, then VFS-translated path
    AssetMeta meta;
    if (std::filesystem::exists(metaPath)) {
        meta = AssetMeta::loadFromFile(metaPath);
    }
    else {
        // Also try VFS-translated path
        auto* vfs            = VirtualFileSystem::get();
        auto  translatedPath = vfs->translatePath(metaPath).string();
        if (std::filesystem::exists(translatedPath)) {
            meta = AssetMeta::loadFromFile(translatedPath);
        }
        else {
            // No meta file — infer defaults from extension
            std::string ext = assetPath.substr(assetPath.find_last_of('.') + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" ||
                ext == "tga" || ext == "hdr" || ext == "dds" || ext == "ktx" || ext == "ktx2") {
                meta = AssetMeta::defaultForTexture();
            }
            else if (ext == "obj" || ext == "fbx" || ext == "gltf" || ext == "glb" ||
                     ext == "dae" || ext == "blend") {
                meta = AssetMeta::defaultForModel();
            }
            else {
                // Generic fallback
                meta.type = "unknown";
            }

            YA_CORE_TRACE("AssetMeta: no sidecar for '{}', using default (type={})", assetPath, meta.type);
        }
    }

    // Cache
    auto [it2, _] = _metaCache.emplace(assetPath, std::move(meta));
    return it2->second;
}

const AssetMeta& AssetManager::reloadMeta(const std::string& assetPath)
{
    _metaCache.erase(assetPath);
    _cacheKeyCache.erase(assetPath); // Force re-compute on next loadTexture
    return getOrLoadMeta(assetPath);
}

// ============================================================================
// Cache key
// ============================================================================

std::string AssetManager::makeCacheKey(const std::string& filepath, const AssetMeta& meta)
{
    return filepath + "|" + std::to_string(meta.propertiesHash());
}

const std::string& AssetManager::getOrBuildCacheKey(const std::string& filepath)
{
    const auto it = _cacheKeyCache.find(filepath);
    if (it != _cacheKeyCache.end())
        return it->second;

    const auto& meta   = getOrLoadMeta(filepath);
    auto [inserted, _] = _cacheKeyCache.emplace(filepath, makeCacheKey(filepath, meta));
    return inserted->second;
}

// ============================================================================
// Synchronous texture loading (meta-driven)
// ============================================================================

AssetManager::ETextureColorSpace AssetManager::inferTextureColorSpace(const FName& textureSemantic)
{
    if (textureSemantic == MatTexture::Diffuse ||
        textureSemantic == MatTexture::Albedo ||
        textureSemantic == MatTexture::Specular ||
        textureSemantic == MatTexture::Emissive)
    {
        return ETextureColorSpace::SRGB;
    }

    if (textureSemantic == MatTexture::Normal ||
        textureSemantic == MatTexture::Metallic ||
        textureSemantic == MatTexture::Roughness ||
        textureSemantic == MatTexture::AO)
    {
        return ETextureColorSpace::Linear;
    }

    return ETextureColorSpace::SRGB;
}

const AssetManager::TextureFormatTraits* AssetManager::getFormatTraits(EFormat::T format)
{
    // Single source of truth for format → (payloadType, decodePrecision, channels).
    // Used by both forward resolution and preferredUploadFormat override.
    static const std::unordered_map<EFormat::T, TextureFormatTraits> kTraits = {
        {EFormat::R8_UNORM,             {ETexturePayloadType::U8,  ETextureDecodePrecision::U8,  1}},
        {EFormat::R8G8_UNORM,           {ETexturePayloadType::U8,  ETextureDecodePrecision::U8,  2}},
        {EFormat::R8G8B8A8_UNORM,       {ETexturePayloadType::U8,  ETextureDecodePrecision::U8,  4}},
        {EFormat::R8G8B8A8_SRGB,        {ETexturePayloadType::U8,  ETextureDecodePrecision::U8,  4}},
        {EFormat::R16G16B16A16_SFLOAT,  {ETexturePayloadType::F16, ETextureDecodePrecision::F16, 4}},
        {EFormat::R32_SFLOAT,           {ETexturePayloadType::F32, ETextureDecodePrecision::F32, 1}},
    };

    const auto it = kTraits.find(format);
    return (it != kTraits.end()) ? &it->second : nullptr;
}

AssetManager::TextureSourceInfo AssetManager::inspectTextureSource(const std::string& filepath) const
{
    TextureSourceInfo info;
    info.filepath  = filepath;
    info.extension = textureExtension(filepath);

    if (isKtxPath(filepath)) {
        auto texture = loadKtxTexture(filepath, KTX_TEXTURE_CREATE_NO_FLAGS);
        if (!texture) {
            return info;
        }

        info.width         = texture->baseWidth;
        info.height        = texture->baseHeight;
        info.bIsCompressed = texture->isCompressed;
        info.detectedKind  = texture->isCompressed ? ETextureSourceKind::Compressed : ETextureSourceKind::Data;
        const auto format  = resolveKtxTextureFormat(texture.get());
        info.detectedChannels = textureChannelCount(format);
        info.bIsHDR           = (format == EFormat::R16G16B16A16_SFLOAT || format == EFormat::R32_SFLOAT);
        return info;
    }

    int width    = -1;
    int height   = -1;
    int channels = -1;
    if (!stbi_info(filepath.c_str(), &width, &height, &channels)) {
        YA_CORE_ERROR("inspectTextureSource: Failed to inspect '{}'", filepath);
        return info;
    }

    info.width            = static_cast<uint32_t>(width);
    info.height           = static_cast<uint32_t>(height);
    info.detectedChannels = static_cast<uint32_t>(std::max(channels, 0));
    info.bIsHDR           = stbi_is_hdr(filepath.c_str()) != 0;
    info.bIsCompressed    = (info.extension == ".dds" || info.extension == ".ktx" || info.extension == ".ktx2");
    info.detectedKind     = info.bIsCompressed ? ETextureSourceKind::Compressed
                                               : (info.bIsHDR ? ETextureSourceKind::HDR : ETextureSourceKind::LDR);
    return info;
}

std::optional<EFormat::T> AssetManager::parseTextureFormatOverride(const std::string& formatName)
{
    if (formatName.empty()) {
        return std::nullopt;
    }

    const std::string                                        key      = toLowerCopy(formatName);
    static const std::unordered_map<std::string, EFormat::T> kFormats = {
        {"r8_unorm", EFormat::R8_UNORM},
        {"r8g8_unorm", EFormat::R8G8_UNORM},
        {"r8g8b8a8_unorm", EFormat::R8G8B8A8_UNORM},
        {"r8g8b8a8_srgb", EFormat::R8G8B8A8_SRGB},
        {"r32_sfloat", EFormat::R32_SFLOAT},
        {"r16g16b16a16_sfloat", EFormat::R16G16B16A16_SFLOAT},
    };

    const auto it = kFormats.find(key);
    if (it == kFormats.end()) {
        return std::nullopt;
    }

    return it->second;
}

const char* AssetManager::textureSourceKindName(ETextureSourceKind kind)
{
    switch (kind) {
    case ETextureSourceKind::Auto:
        return "auto";
    case ETextureSourceKind::LDR:
        return "ldr";
    case ETextureSourceKind::HDR:
        return "hdr";
    case ETextureSourceKind::Data:
        return "data";
    case ETextureSourceKind::Compressed:
        return "compressed";
    default:
        return "unknown";
    }
}

const char* AssetManager::texturePayloadTypeName(ETexturePayloadType type)
{
    switch (type) {
    case ETexturePayloadType::None:
        return "none";
    case ETexturePayloadType::U8:
        return "u8";
    case ETexturePayloadType::F16:
        return "f16";
    case ETexturePayloadType::F32:
        return "f32";
    case ETexturePayloadType::CompressedBytes:
        return "compressed";
    default:
        return "unknown";
    }
}

const char* AssetManager::textureColorSpaceName(ETextureColorSpace colorSpace)
{
    switch (colorSpace) {
    case ETextureColorSpace::SRGB:
        return "srgb";
    case ETextureColorSpace::Linear:
        return "linear";
    default:
        return "unknown";
    }
}

AssetManager::ResolvedTextureImportSettings AssetManager::resolveTextureImportSettings(const std::string& filepath,
                                                                                       ETextureColorSpace codeHint)
{
    ResolvedTextureImportSettings settings;
    settings.sourceInfo = inspectTextureSource(filepath);

    const AssetMeta& meta = getOrLoadMeta(filepath);

    settings.colorSpace = parseColorSpace(meta.getString("colorSpace", ""), codeHint);
    settings.sourceKind = parseSourceKind(meta.getString("sourceKind", "auto"), settings.sourceInfo.detectedKind);

    if (isKtxPath(filepath)) {
        if (!applyKtxResolvedSettings(filepath, settings)) {
            return settings;
        }
        return settings;
    }

    if (settings.sourceKind == ETextureSourceKind::Compressed) {
        YA_CORE_ERROR("resolveTextureImportSettings: Compressed source '{}' is not implemented in runtime importer yet",
                      filepath);
        return settings;
    }

    if (settings.sourceKind == ETextureSourceKind::HDR && !settings.sourceInfo.bIsHDR) {
        YA_CORE_WARN("Texture '{}' is forced to HDR by meta, but the detected source is not HDR-capable", filepath);
    }

    settings.channelPolicy = parseChannelPolicy(meta.getString("channelPolicy", "force_rgba"));

    settings.resolvedChannels = settings.channelPolicy == ETextureChannelPolicy::Preserve
                                  ? std::clamp(settings.sourceInfo.detectedChannels, 1u, 4u)
                                  : 4u;

    settings.decodePrecision = parseDecodePrecision(meta.getString("decodePrecision", "auto"));

    if (settings.sourceKind == ETextureSourceKind::HDR) {
        if (settings.decodePrecision == ETextureDecodePrecision::Auto) {
            settings.decodePrecision = ETextureDecodePrecision::F16;
        }
        if (settings.channelPolicy == ETextureChannelPolicy::Preserve && settings.resolvedChannels != 4) {
            YA_CORE_WARN("HDR texture '{}' requested preserved channels={}, forcing RGBA for current upload path",
                         filepath,
                         settings.resolvedChannels);
            settings.resolvedChannels = 4;
        }
        settings.payloadType    = (settings.decodePrecision == ETextureDecodePrecision::F32)
                                    ? ETexturePayloadType::F32
                                    : ETexturePayloadType::F16;
        settings.resolvedFormat = EFormat::R16G16B16A16_SFLOAT;
    }
    else {
        settings.decodePrecision = ETextureDecodePrecision::U8;
        settings.payloadType     = ETexturePayloadType::U8;

        if (settings.resolvedChannels <= 1) {
            settings.resolvedFormat = EFormat::R8_UNORM;
        }
        else if (settings.resolvedChannels == 2) {
            settings.resolvedFormat = EFormat::R8G8_UNORM;
        }
        else {
            settings.resolvedChannels = 4;
            settings.resolvedFormat   = (settings.colorSpace == ETextureColorSpace::SRGB)
                                          ? EFormat::R8G8B8A8_SRGB
                                          : EFormat::R8G8B8A8_UNORM;
        }
    }

    const std::string preferredUploadFormat = toLowerCopy(meta.getString("preferredUploadFormat", "auto"));
    if (!preferredUploadFormat.empty() && preferredUploadFormat != "auto") {
        const auto overrideFormat = parseTextureFormatOverride(preferredUploadFormat);
        if (!overrideFormat.has_value()) {
            YA_CORE_ERROR("resolveTextureImportSettings: Unsupported preferredUploadFormat '{}' for '{}'",
                          preferredUploadFormat,
                          filepath);
        }
        else {
            const auto* traits = getFormatTraits(*overrideFormat);
            if (!traits) {
                YA_CORE_ERROR("resolveTextureImportSettings: No traits for format override '{}' in '{}'",
                              preferredUploadFormat, filepath);
            }
            else {
                settings.resolvedFormat   = *overrideFormat;
                settings.payloadType      = traits->payloadType;
                settings.decodePrecision  = traits->decodePrecision;
                settings.resolvedChannels = traits->channels;
            }
        }
    }

    if (settings.sourceKind != ETextureSourceKind::HDR &&
        (settings.payloadType == ETexturePayloadType::F16 || settings.payloadType == ETexturePayloadType::F32)) {
        YA_CORE_WARN("Texture '{}' requested float upload format for non-HDR source; decode will still use stb float path",
                     filepath);
    }

    return settings;
}

TextureFuture AssetManager::loadTexture(const TextureLoadRequest& request)
{
    if (request.filepath.empty()) {
        return TextureFuture();
    }

    if (request.textureSemantic.has_value()) {
        TextureLoadRequest normalized = request;
        normalized.colorSpace         = inferTextureColorSpace(*request.textureSemantic);
        normalized.textureSemantic.reset();
        return loadTexture(normalized);
    }

    const auto& cacheKey = getOrBuildCacheKey(request.filepath);

    // Already loaded? Return immediately — no disk I/O needed.
    {
        const auto it = _textureViews.find(cacheKey);
        if (it != _textureViews.end()) {
            if (!request.name.empty()) {
                _textureName2Path[request.name] = cacheKey;
            }
            if (request.onReady) {
                dispatchToGameThread([onReady = request.onReady, texture = it->second]() mutable {
                    onReady(texture);
                });
            }
            return TextureFuture(it->second);
        }
    }

    // Submit async decode if not already in flight
    if (!isTextureLoadPending(cacheKey)) {
        const auto settings = resolveTextureImportSettings(request.filepath, request.colorSpace);
        submitTextureLoad(request.filepath, cacheKey, settings, request.name);
    }

    if (!request.name.empty()) {
        _textureName2Path[request.name] = cacheKey;
    }

    if (request.onReady) {
        registerTextureCallback(cacheKey, request.onReady);
    }

    // Not ready yet — return empty future. Caller must check isReady().
    return TextureFuture();
}

void AssetManager::loadTextureBatch(const TextureBatchLoadRequest& request)
{
    const auto& filepaths  = request.filepaths;
    auto        onDone     = request.onDone;
    const auto  colorSpace = request.colorSpace;

    // if (!onDone) {
    //     return;
    // }

    if (filepaths.empty()) {
        dispatchToGameThread([onDone = std::move(onDone)]() mutable {
            onDone({});
        });
        return;
    }

    struct BatchState
    {
        std::mutex                            mutex;
        std::vector<std::shared_ptr<Texture>> textures;
        std::atomic<uint32_t>                 remaining = 0;
        TextureBatchReadyCallback             onDone;
    };

    auto batchState = std::make_shared<BatchState>();
    batchState->textures.resize(filepaths.size());
    batchState->remaining.store(static_cast<uint32_t>(filepaths.size()), std::memory_order_release);
    batchState->onDone = std::move(onDone);

    for (size_t index = 0; index < filepaths.size(); ++index) {
        const auto& filepath = filepaths[index];
        loadTexture(TextureLoadRequest{
            .filepath = filepath,
            .onReady  = [batchState, index](const std::shared_ptr<Texture>& texture) {
                {
                    std::lock_guard lock(batchState->mutex);
                    batchState->textures[index] = texture;
                }

                if (batchState->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    auto textures = batchState->textures;
                    auto onDone   = batchState->onDone;
                    if (onDone) {
                        onDone(textures);
                    }
                }
            },
            .colorSpace = colorSpace,
        });
    }
}

AssetManager::TextureBatchMemoryHandle AssetManager::loadTextureBatchIntoMemory(
    const TextureBatchMemoryLoadRequest& request)
{
    const auto&                                filepaths  = request.filepaths;
    const auto                                 colorSpace = request.colorSpace;
    std::vector<ResolvedTextureImportSettings> settingsList;
    settingsList.reserve(filepaths.size());

    for (const auto& filepath : filepaths) {
        settingsList.push_back(resolveTextureImportSettings(filepath, colorSpace));
    }

    TextureBatchMemoryHandle handleId = 0;
    {
        std::lock_guard lock(_cacheMutex);
        handleId = _nextTextureBatchMemoryHandle++;
    }

    if (filepaths.empty()) {
        std::lock_guard lock(_cacheMutex);
        _readyTextureBatchMemory.emplace(handleId, TextureBatchMemory{});
        return handleId;
    }

    auto handle = TaskQueue::get().submitWithCallback(
        [settingsList = std::move(settingsList)]() -> TextureBatchMemory {
            TextureBatchMemory batchMemory;
            batchMemory.textures.reserve(settingsList.size());

            for (const auto& settings : settingsList) {
                auto decoded = decodeTextureToMemory(settings);
                batchMemory.textures.push_back(std::move(decoded));
            }

            return batchMemory;
        },
        [this, handleId](TextureBatchMemory batchMemory) {
            std::lock_guard lock(_cacheMutex);
            _pendingTextureBatchMemoryLoads.erase(handleId);
            _readyTextureBatchMemory[handleId] = std::move(batchMemory);
        });

    {
        std::lock_guard lock(_cacheMutex);
        _pendingTextureBatchMemoryLoads[handleId] = std::move(handle);
    }

    return handleId;
}

bool AssetManager::consumeTextureBatchMemory(TextureBatchMemoryHandle handle,
                                             TextureBatchMemory&      outBatchMemory)
{
    std::lock_guard lock(_cacheMutex);

    auto readyIt = _readyTextureBatchMemory.find(handle);
    if (readyIt != _readyTextureBatchMemory.end()) {
        outBatchMemory = std::move(readyIt->second);
        _readyTextureBatchMemory.erase(readyIt);
        return true;
    }

    return false;
}

// ============================================================================
// Explicit synchronous loading
// ============================================================================

std::shared_ptr<Texture> AssetManager::loadTextureSync(const std::string& name,
                                                       const std::string& filepath,
                                                       ETextureColorSpace colorSpace)
{
    const auto& cacheKey = getOrBuildCacheKey(filepath);

    // Check cache first — avoids disk I/O for already-loaded textures
    {
        const auto it = _textureViews.find(cacheKey);
        if (it != _textureViews.end()) {
            if (!name.empty()) {
                _textureName2Path[name] = cacheKey;
            }
            return it->second;
        }
    }

    // Synchronous: inspect source + decode + GPU upload on calling thread
    const auto settings = resolveTextureImportSettings(filepath, colorSpace);
    auto       decoded  = decodeTextureToMemory(settings);
    if (!decoded.isValid()) {
        YA_CORE_WARN("loadTextureSync: Failed to decode texture: {}", filepath);
        return nullptr;
    }

    auto texture = Texture::fromMemory(TextureMemoryCreateInfo{
        .filepath = decoded.filepath,
        .label    = name.empty() ? decoded.filepath : name,
        .memory   = TextureMemoryView{
              .width    = decoded.width,
              .height   = decoded.height,
              .channels = decoded.channels,
              .mipLevels = decoded.mipLevels,
              .format   = decoded.format,
              .data     = decoded.data(),
              .dataSize = decoded.dataSize(),
        },
    });
    if (!texture) {
        YA_CORE_WARN("loadTextureSync: Failed to create texture: {}", filepath);
        return nullptr;
    }

    _textureViews[cacheKey] = texture;
    if (!name.empty()) {
        _textureName2Path[name] = cacheKey;
    }
    return texture;
}

// ============================================================================
// Core async texture pipeline
// ============================================================================

void AssetManager::submitTextureLoad(const std::string&            filepath,
                                     const std::string&            cacheKey,
                                     ResolvedTextureImportSettings settings,
                                     const std::string&            name)
{
    YA_CORE_INFO("submitTextureLoad: async decode '{}' (format={}, payload={})",
                 filepath,
                 static_cast<int>(settings.resolvedFormat),
                 texturePayloadTypeName(settings.payloadType));

    auto handle = TaskQueue::get().submitWithCallback(
        // ── Worker thread: CPU-only decode (stbi_load) ──────────────
        [settings]() -> TextureMemoryBlock {
            return decodeTextureToMemory(settings);
        },
        // ── Main thread callback: GPU upload ────────────────────────
        [this, filepath, cacheKey, name](TextureMemoryBlock decoded) {
            std::vector<TextureReadyCallback> callbacks;
            std::shared_ptr<Texture>          readyTexture;

            // Another load may have raced and filled the cache already
            {
                std::lock_guard lock(_cacheMutex);
                auto            existing = _textureViews.find(cacheKey);
                if (existing != _textureViews.end()) {
                    _pendingTextureLoads.erase(cacheKey);
                    callbacks    = takeTextureCallbacks(cacheKey);
                    readyTexture = existing->second;
                }
            }

            if (readyTexture) {
                dispatchTextureCallbacks(callbacks, readyTexture);
                return;
            }

            if (!decoded.isValid()) {
                YA_CORE_WARN("Async texture decode failed for '{}'", filepath);
                {
                    std::lock_guard lock(_cacheMutex);
                    _pendingTextureLoads.erase(cacheKey);
                    callbacks = takeTextureCallbacks(cacheKey);
                }
                dispatchTextureCallbacks(callbacks, nullptr);
                return;
            }

            // GPU upload on main thread
            auto texture = Texture::fromMemory(TextureMemoryCreateInfo{
                .filepath = decoded.filepath,
                .label    = decoded.filepath,
                .memory   = TextureMemoryView{
                      .width    = decoded.width,
                      .height   = decoded.height,
                      .channels = decoded.channels,
                    .mipLevels = decoded.mipLevels,
                      .format   = decoded.format,
                      .data     = decoded.data(),
                      .dataSize = decoded.dataSize(),
                },
            });
            if (!texture) {
                YA_CORE_WARN("Async GPU upload failed for '{}'", filepath);
                {
                    std::lock_guard lock(_cacheMutex);
                    _pendingTextureLoads.erase(cacheKey);
                    callbacks = takeTextureCallbacks(cacheKey);
                }
                dispatchTextureCallbacks(callbacks, nullptr);
                return;
            }

            // Store in cache
            {
                std::lock_guard lock(_cacheMutex);
                _textureViews[cacheKey] = texture;
                if (!name.empty()) {
                    _textureName2Path[name] = cacheKey;
                }
                _pendingTextureLoads.erase(cacheKey);
                callbacks = takeTextureCallbacks(cacheKey);
            }

            dispatchTextureCallbacks(callbacks, readyTexture ? readyTexture : texture);

            YA_CORE_INFO("Async texture ready: '{}' ({}x{})", filepath, texture->getWidth(), texture->getHeight());
        });

    std::lock_guard lock(_cacheMutex);
    _pendingTextureLoads[cacheKey] = std::move(handle);
}

void AssetManager::dispatchToGameThread(std::function<void()> task)
{
    if (!task) {
        return;
    }

    auto* app = App::get();
    if (!app) {
        task();
        return;
    }

    app->taskManager.registerFrameTask(std::move(task));
}

void AssetManager::registerTextureCallback(const std::string& cacheKey, TextureReadyCallback onReady)
{
    if (!onReady) {
        return;
    }

    std::shared_ptr<Texture> readyTexture;
    {
        std::lock_guard lock(_cacheMutex);
        auto            loadedIt = _textureViews.find(cacheKey);
        if (loadedIt != _textureViews.end()) {
            readyTexture = loadedIt->second;
        }
        else {
            _pendingTextureCallbacks[cacheKey].push_back(std::move(onReady));
            return;
        }
    }

    dispatchToGameThread([onReady = std::move(onReady), readyTexture]() mutable {
        onReady(readyTexture);
    });
}

void AssetManager::registerModelCallback(const std::string& filepath, ModelReadyCallback onReady)
{
    if (!onReady) {
        return;
    }

    std::shared_ptr<Model> readyModel;
    {
        std::lock_guard lock(_cacheMutex);
        auto            loadedIt = modelCache.find(filepath);
        if (loadedIt != modelCache.end()) {
            readyModel = loadedIt->second;
        }
        else {
            _pendingModelCallbacks[filepath].push_back(std::move(onReady));
            return;
        }
    }

    dispatchToGameThread([onReady = std::move(onReady), readyModel]() mutable {
        onReady(readyModel);
    });
}

std::vector<AssetManager::TextureReadyCallback> AssetManager::takeTextureCallbacks(const std::string& cacheKey)
{
    auto it = _pendingTextureCallbacks.find(cacheKey);
    if (it == _pendingTextureCallbacks.end()) {
        return {};
    }

    auto callbacks = std::move(it->second);
    _pendingTextureCallbacks.erase(it);
    return callbacks;
}

std::vector<AssetManager::ModelReadyCallback> AssetManager::takeModelCallbacks(const std::string& filepath)
{
    auto it = _pendingModelCallbacks.find(filepath);
    if (it == _pendingModelCallbacks.end()) {
        return {};
    }

    auto callbacks = std::move(it->second);
    _pendingModelCallbacks.erase(it);
    return callbacks;
}

void AssetManager::dispatchTextureCallbacks(const std::vector<TextureReadyCallback>& callbacks,
                                            const std::shared_ptr<Texture>&          texture)
{
    for (const auto& callback : callbacks) {
        if (!callback) {
            continue;
        }

        dispatchToGameThread([callback, texture]() {
            callback(texture);
        });
    }
}

void AssetManager::dispatchModelCallbacks(const std::vector<ModelReadyCallback>& callbacks,
                                          const std::shared_ptr<Model>&          model)
{
    for (const auto& callback : callbacks) {
        if (!callback) {
            continue;
        }

        dispatchToGameThread([callback, model]() {
            callback(model);
        });
    }
}

// ============================================================================
// Resource versioning
// ============================================================================

uint64_t AssetManager::getResourceVersion(const std::string& assetPath) const
{
    auto it = _resourceVersion.find(assetPath);
    return (it != _resourceVersion.end()) ? it->second : 0;
}

void AssetManager::bumpResourceVersion(const std::string& assetPath)
{
    const auto newVersion = ++_resourceVersion[assetPath];
    YA_CORE_TRACE("bumpResourceVersion: '{}' → v{}", assetPath, newVersion);
}

bool AssetManager::isTextureLoadPending(const std::string& cacheKey) const
{
    std::lock_guard lock(_cacheMutex);
    auto            it = _pendingTextureLoads.find(cacheKey);
    if (it == _pendingTextureLoads.end()) return false;
    return !it->second.isReady();
}

// ============================================================================
// Reference counting / auto-release (GPU-safe via DeferredDeletionQueue)
// ============================================================================

/// Helper: get the current frame index tracked by the deferred deletion queue.
static uint64_t getCurrentFrameIdx()
{
    return DeferredDeletionQueue::get().currentFrame();
}

size_t AssetManager::collectUnused()
{
    size_t   released = 0;
    auto&    ddq      = DeferredDeletionQueue::get();
    uint64_t frame    = getCurrentFrameIdx();

    // Textures
    for (auto it = _textureViews.begin(); it != _textureViews.end();) {
        if (it->second && it->second.use_count() == 1) {
            YA_CORE_TRACE("collectUnused: scheduling deferred release for texture '{}'", it->first);
            // Move the shared_ptr into the deferred queue — actual destructor
            // (VulkanImage::~VulkanImage → vkDestroyImage) runs only after
            // the GPU has finished all in-flight frames.
            ddq.enqueueResource(frame, std::move(it->second));
            it = _textureViews.erase(it);
            ++released;
        }
        else {
            ++it;
        }
    }

    // Models (contain GPU Mesh buffers)
    for (auto it = modelCache.begin(); it != modelCache.end();) {
        if (it->second && it->second.use_count() == 1) {
            YA_CORE_TRACE("collectUnused: scheduling deferred release for model '{}'", it->first);
            ddq.enqueueResource(frame, std::move(it->second));
            it = modelCache.erase(it);
            ++released;
        }
        else {
            ++it;
        }
    }

    if (released > 0) {
        YA_CORE_INFO("collectUnused: {} resources scheduled for deferred release", released);
    }

    return released;
}

void AssetManager::unload(const std::string& cacheKey)
{
    auto&    ddq   = DeferredDeletionQueue::get();
    uint64_t frame = getCurrentFrameIdx();

    auto texIt = _textureViews.find(cacheKey);
    if (texIt != _textureViews.end()) {
        ddq.enqueueResource(frame, std::move(texIt->second));
        _textureViews.erase(texIt);
        YA_CORE_INFO("Force-unloaded texture (deferred): {}", cacheKey);
        return;
    }

    auto modelIt = modelCache.find(cacheKey);
    if (modelIt != modelCache.end()) {
        ddq.enqueueResource(frame, std::move(modelIt->second));
        modelCache.erase(modelIt);
        YA_CORE_INFO("Force-unloaded model (deferred): {}", cacheKey);
        return;
    }

    YA_CORE_WARN("unload: cache key '{}' not found", cacheKey);
}

AssetManager::CacheStats AssetManager::getStats() const
{
    CacheStats stats;
    stats.textureCount = _textureViews.size();
    stats.modelCount   = modelCache.size();

    for (auto& [key, tex] : _textureViews) {
        if (tex) {
            stats.textureMemoryEstimate += static_cast<size_t>(tex->getWidth()) *
                                           tex->getHeight() * tex->getChannels();
        }
    }

    return stats;
}

// ============================================================================
// Hot-reload
// ============================================================================

void AssetManager::onMetaFileChanged(const std::string& metaPath)
{
    // Derive asset path from meta path: strip ".ya-meta.json" suffix
    const std::string suffix = ".ya-meta.json";
    if (metaPath.size() <= suffix.size() || !metaPath.ends_with(suffix)) {
        YA_CORE_WARN("onMetaFileChanged: '{}' does not look like a .ya-meta.json file", metaPath);
        return;
    }

    std::string assetPath = metaPath.substr(0, metaPath.size() - suffix.size());

    // Reload meta (copy oldMeta by value — reloadMeta erases from _metaCache)
    AssetMeta        oldMeta = getOrLoadMeta(assetPath);
    const AssetMeta& newMeta = reloadMeta(assetPath);

    if (oldMeta == newMeta) {
        YA_CORE_TRACE("onMetaFileChanged: meta unchanged for '{}'", assetPath);
        return;
    }

    YA_CORE_INFO("onMetaFileChanged: meta changed for '{}', reloading affected resources", assetPath);

    evictCachedAsset(assetPath);

    // Reload with new meta
    if (newMeta.type == "texture") {
        loadTexture(TextureLoadRequest{
            .filepath = assetPath,
        });
    }
    else if (newMeta.type == "model") {
        loadModel(ModelLoadRequest{
            .filepath = assetPath,
        });
    }

    // Bump version so all TAssetRef instances detect the change
    bumpResourceVersion(assetPath);
}

void AssetManager::onAssetFileChanged(const std::string& assetPath)
{
    YA_CORE_INFO("onAssetFileChanged: '{}' changed, reloading", assetPath);

    evictCachedAsset(assetPath);

    // Also invalidate meta cache (in case the file type changed, unlikely but safe)
    _metaCache.erase(assetPath);

    // Reload
    AssetMeta meta = getOrLoadMeta(assetPath);
    if (meta.type == "texture") {
        loadTexture(TextureLoadRequest{
            .filepath = assetPath,
        });
    }
    else if (meta.type == "model") {
        loadModel(ModelLoadRequest{
            .filepath = assetPath,
        });
    }

    bumpResourceVersion(assetPath);
}
// Query methods
// ============================================================================

std::shared_ptr<Texture> AssetManager::getTextureByPath(const std::string& filepath) const
{
    // Try composite key lookup for all cached metas
    auto metaIt = _metaCache.find(filepath);
    if (metaIt != _metaCache.end()) {
        std::string cacheKey = makeCacheKey(filepath, metaIt->second);
        auto        it       = _textureViews.find(cacheKey);
        if (it != _textureViews.end()) return it->second;
    }

    // Fallback: linear scan for path prefix match
    for (auto& [key, tex] : _textureViews) {
        if (key.starts_with(filepath)) {
            return tex;
        }
    }

    return nullptr;
}

std::shared_ptr<Texture> AssetManager::getTextureByName(const std::string& name) const
{
    auto nameIt = _textureName2Path.find(name);
    if (nameIt != _textureName2Path.end()) {
        auto texIt = _textureViews.find(nameIt->second);
        if (texIt != _textureViews.end()) {
            return texIt->second;
        }
    }
    return nullptr;
}

bool AssetManager::isTextureLoaded(const std::string& filepath) const
{
    // Check composite key
    auto metaIt = _metaCache.find(filepath);
    if (metaIt != _metaCache.end()) {
        std::string cacheKey = makeCacheKey(filepath, metaIt->second);
        if (_textureViews.find(cacheKey) != _textureViews.end()) return true;
    }

    // Fallback: check raw filepath key (legacy)
    return _textureViews.find(filepath) != _textureViews.end();
}

bool AssetManager::isTextureLoadedByName(const std::string& name) const
{
    return _textureName2Path.find(name) != _textureName2Path.end();
}

void AssetManager::registerTexture(const std::string& name, const stdptr<Texture>& texture)
{
    auto it = _textureViews.find(name);
    if (it != _textureViews.end()) {
        YA_CORE_WARN("Texture with name '{}' already registered. Overwriting.", name);
    }
    _textureViews.insert({name, texture});
}

void AssetManager::invalidate(const std::string& filepath)
{
    evictCachedAsset(filepath);

    // Also try exact match (legacy keys)
    {
        auto&    ddq   = DeferredDeletionQueue::get();
        uint64_t frame = getCurrentFrameIdx();

        auto texIt = _textureViews.find(filepath);
        if (texIt != _textureViews.end()) {
            ddq.enqueueResource(frame, std::move(texIt->second));
            _textureViews.erase(texIt);
        }
        auto modelIt = modelCache.find(filepath);
        if (modelIt != modelCache.end()) {
            ddq.enqueueResource(frame, std::move(modelIt->second));
            modelCache.erase(modelIt);
        }
    }

    // Clear meta + cacheKey caches for this asset
    _metaCache.erase(filepath);
    _cacheKeyCache.erase(filepath);

    bumpResourceVersion(filepath);
}

std::vector<std::string> AssetManager::findCacheKeysForPath(const std::string& filepath) const
{
    std::vector<std::string> result;
    std::string              prefix = filepath + "|";

    for (auto& [key, _] : _textureViews) {
        if (key.starts_with(prefix) || key == filepath) {
            result.push_back(key);
        }
    }
    for (auto& [key, _] : modelCache) {
        if (key.starts_with(prefix) || key == filepath) {
            result.push_back(key);
        }
    }

    return result;
}

void AssetManager::evictCachedAsset(const std::string& assetPath)
{
    auto&    ddq   = DeferredDeletionQueue::get();
    uint64_t frame = getCurrentFrameIdx();

    auto keys = findCacheKeysForPath(assetPath);
    for (auto& key : keys) {
        auto texIt = _textureViews.find(key);
        if (texIt != _textureViews.end()) {
            ddq.enqueueResource(frame, std::move(texIt->second));
            _textureViews.erase(texIt);
        }
        auto modelIt = modelCache.find(key);
        if (modelIt != modelCache.end()) {
            ddq.enqueueResource(frame, std::move(modelIt->second));
            modelCache.erase(modelIt);
        }
    }
}

// ============================================================================
// Model loading — async by default
// ============================================================================

ModelFuture AssetManager::loadModel(const ModelLoadRequest& request)
{
    if (request.filepath.empty()) {
        return ModelFuture();
    }

    // Already loaded?
    if (isModelLoaded(request.filepath)) {
        if (!request.name.empty()) {
            _modalName2Path[request.name] = request.filepath;
        }
        if (request.onReady) {
            dispatchToGameThread([onReady = request.onReady, model = modelCache[request.filepath]]() mutable {
                onReady(model);
            });
        }
        return ModelFuture(modelCache[request.filepath]);
    }

    // Submit async decode if not already in flight
    if (!isModelLoadPending(request.filepath)) {
        submitModelLoad(request.filepath, request.name);
    }

    if (!request.name.empty()) {
        _modalName2Path[request.name] = request.filepath;
    }

    if (request.onReady) {
        registerModelCallback(request.filepath, request.onReady);
    }

    return ModelFuture();
}

// ── Sync model loading ──────────────────────────────────────────────────

std::shared_ptr<Model> AssetManager::loadModelSync(const std::string& filepath)
{
    if (isModelLoaded(filepath)) {
        return modelCache[filepath];
    }

    return loadModelImpl(filepath, "");
}

std::shared_ptr<Model> AssetManager::loadModelSync(const std::string& name, const std::string& filepath)
{
    if (isModelLoaded(filepath)) {
        return modelCache[filepath];
    }

    auto model = loadModelImpl(filepath, name);
    if (model) {
        _modalName2Path[name] = filepath;
    }
    return model;
}

// ── Async model pipeline ────────────────────────────────────────────────

void AssetManager::submitModelLoad(const std::string& filepath, const std::string& name)
{
    YA_CORE_INFO("submitModelLoad: async decode '{}'", filepath);

    auto handle = TaskQueue::get().submitWithCallback(
        // ── Worker thread: CPU-only Assimp decode ───────────────────
        [filepath]() -> DecodedModelData {
            return DecodedModelData::decode(filepath);
        },
        // ── Main thread callback: GPU mesh creation ─────────────────
        [this, filepath, name](DecodedModelData decoded) {
            std::vector<ModelReadyCallback> callbacks;
            std::shared_ptr<Model>          readyModel;

            // Race check
            {
                std::lock_guard lock(_cacheMutex);
                auto            existing = modelCache.find(filepath);
                if (existing != modelCache.end()) {
                    _pendingModelLoads.erase(filepath);
                    callbacks  = takeModelCallbacks(filepath);
                    readyModel = existing->second;
                }
            }

            if (readyModel) {
                dispatchModelCallbacks(callbacks, readyModel);
                return;
            }

            if (!decoded.isValid()) {
                YA_CORE_WARN("Async model decode failed for '{}'", filepath);
                {
                    std::lock_guard lock(_cacheMutex);
                    _pendingModelLoads.erase(filepath);
                    callbacks = takeModelCallbacks(filepath);
                }
                dispatchModelCallbacks(callbacks, nullptr);
                return;
            }

            // GPU mesh creation on main thread
            auto model = decoded.createModel();
            if (!model) {
                YA_CORE_WARN("Async GPU mesh creation failed for '{}'", filepath);
                {
                    std::lock_guard lock(_cacheMutex);
                    _pendingModelLoads.erase(filepath);
                    callbacks = takeModelCallbacks(filepath);
                }
                dispatchModelCallbacks(callbacks, nullptr);
                return;
            }

            {
                std::lock_guard lock(_cacheMutex);
                modelCache[filepath] = model;
                if (!name.empty()) {
                    _modalName2Path[name] = filepath;
                }
                _pendingModelLoads.erase(filepath);
                callbacks = takeModelCallbacks(filepath);
            }

            dispatchModelCallbacks(callbacks, model);

            YA_CORE_INFO("Async model ready: '{}' ({} meshes, {} materials)",
                         filepath,
                         model->meshes.size(),
                         model->embeddedMaterials.size());
        });

    std::lock_guard lock(_cacheMutex);
    _pendingModelLoads[filepath] = std::move(handle);
}

bool AssetManager::isModelLoadPending(const std::string& filepath) const
{
    std::lock_guard lock(_cacheMutex);
    auto            it = _pendingModelLoads.find(filepath);
    if (it == _pendingModelLoads.end()) return false;
    return !it->second.isReady();
}

bool AssetManager::isModelLoaded(const std::string& filepath) const
{
    return modelCache.find(filepath) != modelCache.end();
}

std::shared_ptr<Model> AssetManager::getModel(const std::string& filepath) const
{
    if (isModelLoaded(filepath)) {
        return modelCache.at(filepath);
    }
    return nullptr;
}

std::shared_ptr<Model> AssetManager::loadModelImpl(const std::string& filepath, const std::string& identifier)
{
    // Check if the model is already loaded
    if (isModelLoaded(filepath)) {
        return modelCache[filepath];
    }

    // Synchronous path: decode + GPU mesh creation on calling thread
    auto decoded = DecodedModelData::decode(filepath);
    if (!decoded.isValid()) {
        YA_CORE_ERROR("loadModelImpl: Failed to decode model: {}", filepath);
        return nullptr;
    }

    auto model = decoded.createModel();
    if (!model) {
        YA_CORE_ERROR("loadModelImpl: Failed to create GPU meshes for: {}", filepath);
        return nullptr;
    }

    // Cache the model
    modelCache[filepath] = model;
    return model;
}

} // namespace ya
