#include "Resource/Texture/AssetTextureInternal.h"

#include "Core/Log.h"
#include "Core/System/PathUtils.h"
#include "Core/System/VirtualFileSystem.h"
#include "Runtime/App/App.h"
#include "ktx.h"
#include "stb/stb_image.h"

#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <memory>

namespace ya::asset_manager_texture_detail
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

bool fileExistsInVfs(const std::string& filepath)
{
    if (filepath.empty()) {
        return false;
    }

    if (auto* vfs = VirtualFileSystem::get()) {
        return vfs->isFileExists(filepath);
    }

    return std::filesystem::exists(filepath);
}

bool isAstcFormat(EFormat::T format)
{
    switch (format) {
    case EFormat::ASTC_4x4_UNORM_BLOCK:
    case EFormat::ASTC_4x4_SRGB_BLOCK:
    case EFormat::ASTC_5x5_UNORM_BLOCK:
    case EFormat::ASTC_5x5_SRGB_BLOCK:
    case EFormat::ASTC_6x6_UNORM_BLOCK:
    case EFormat::ASTC_6x6_SRGB_BLOCK:
    case EFormat::ASTC_8x8_UNORM_BLOCK:
    case EFormat::ASTC_8x8_SRGB_BLOCK:
    case EFormat::ASTC_10x10_UNORM_BLOCK:
    case EFormat::ASTC_10x10_SRGB_BLOCK:
        return true;
    default:
        return false;
    }
}

bool supportsTextureUploadFormat(EFormat::T format)
{
    if (format == EFormat::Undefined) {
        return false;
    }

    auto* app = App::get();
    if (!app || !app->getRender()) {
        return true;
    }

    return app->getRender()->isTextureFormatSupported(
        format,
        static_cast<EImageUsage::T>(EImageUsage::Sampled | EImageUsage::TransferDst));
}

EFormat::T transcodeTargetToFormat(AssetManager::ETextureTranscodeTarget target,
                                   AssetManager::ETextureColorSpace       colorSpace)
{
    switch (target) {
    case AssetManager::ETextureTranscodeTarget::BC7:
        return colorSpace == AssetManager::ETextureColorSpace::SRGB ? EFormat::BC7_SRGB_BLOCK
                                                                    : EFormat::BC7_UNORM_BLOCK;
    case AssetManager::ETextureTranscodeTarget::BC3:
        return colorSpace == AssetManager::ETextureColorSpace::SRGB ? EFormat::BC3_SRGB_BLOCK
                                                                    : EFormat::BC3_UNORM_BLOCK;
    case AssetManager::ETextureTranscodeTarget::ETC2_RGBA:
        return colorSpace == AssetManager::ETextureColorSpace::SRGB ? EFormat::ETC2_R8G8B8A8_SRGB_BLOCK
                                                                    : EFormat::ETC2_R8G8B8A8_UNORM_BLOCK;
    case AssetManager::ETextureTranscodeTarget::RGBA32:
        return colorSpace == AssetManager::ETextureColorSpace::SRGB ? EFormat::R8G8B8A8_SRGB
                                                                    : EFormat::R8G8B8A8_UNORM;
    case AssetManager::ETextureTranscodeTarget::None:
    default:
        return EFormat::Undefined;
    }
}

ktx_transcode_fmt_e toKtxTranscodeFormat(AssetManager::ETextureTranscodeTarget target)
{
    switch (target) {
    case AssetManager::ETextureTranscodeTarget::BC7:
        return KTX_TTF_BC7_RGBA;
    case AssetManager::ETextureTranscodeTarget::BC3:
        return KTX_TTF_BC3_RGBA;
    case AssetManager::ETextureTranscodeTarget::ETC2_RGBA:
        return KTX_TTF_ETC2_RGBA;
    case AssetManager::ETextureTranscodeTarget::RGBA32:
        return KTX_TTF_RGBA32;
    case AssetManager::ETextureTranscodeTarget::None:
    default:
        return KTX_TTF_NOSELECTION;
    }
}

AssetManager::ETextureTranscodeTarget selectTranscodeTarget(AssetManager::ETextureColorSpace colorSpace)
{
    const auto bc7Format  = transcodeTargetToFormat(AssetManager::ETextureTranscodeTarget::BC7, colorSpace);
    const auto bc3Format  = transcodeTargetToFormat(AssetManager::ETextureTranscodeTarget::BC3, colorSpace);
    const auto etc2Format = transcodeTargetToFormat(AssetManager::ETextureTranscodeTarget::ETC2_RGBA, colorSpace);
    const auto rgbaFormat = transcodeTargetToFormat(AssetManager::ETextureTranscodeTarget::RGBA32, colorSpace);

    if (supportsTextureUploadFormat(bc7Format)) {
        return AssetManager::ETextureTranscodeTarget::BC7;
    }
    if (supportsTextureUploadFormat(bc3Format)) {
        return AssetManager::ETextureTranscodeTarget::BC3;
    }
    if (supportsTextureUploadFormat(etc2Format)) {
        return AssetManager::ETextureTranscodeTarget::ETC2_RGBA;
    }
    if (supportsTextureUploadFormat(rgbaFormat)) {
        return AssetManager::ETextureTranscodeTarget::RGBA32;
    }

    return AssetManager::ETextureTranscodeTarget::None;
}

void finalizeKtxImportSettings(AssetManager::ResolvedTextureImportSettings& settings)
{
    settings.payloadType = AssetManager::ETexturePayloadType::CompressedBytes;

    if (settings.sourceNeedsTranscoding) {
        const auto target = selectTranscodeTarget(settings.colorSpace);
        if (target == AssetManager::ETextureTranscodeTarget::None) {
            settings.uploadStrategy = AssetManager::ETextureUploadStrategy::Placeholder;
            settings.transcodeTarget = AssetManager::ETextureTranscodeTarget::None;
            settings.resolvedFormat = EFormat::Undefined;
            settings.diagnostic = "No supported runtime transcode target for this device";
            return;
        }

        settings.uploadStrategy  = AssetManager::ETextureUploadStrategy::TranscodeKtx2;
        settings.transcodeTarget = target;
        settings.resolvedFormat  = transcodeTargetToFormat(target, settings.colorSpace);
        settings.resolvedChannels = textureChannelCount(settings.resolvedFormat);
        settings.diagnostic = "Runtime transcode KTX2 for current device";
        return;
    }

    if (settings.sourceFormat != EFormat::Undefined && supportsTextureUploadFormat(settings.sourceFormat)) {
        settings.uploadStrategy   = AssetManager::ETextureUploadStrategy::Direct;
        settings.transcodeTarget  = AssetManager::ETextureTranscodeTarget::None;
        settings.resolvedFormat   = settings.sourceFormat;
        settings.resolvedChannels = textureChannelCount(settings.sourceFormat);
        settings.diagnostic       = "Direct texture upload";
        return;
    }

    settings.uploadStrategy  = AssetManager::ETextureUploadStrategy::Placeholder;
    settings.transcodeTarget = AssetManager::ETextureTranscodeTarget::None;
    settings.resolvedFormat  = EFormat::Undefined;
    settings.diagnostic = isAstcFormat(settings.sourceFormat)
                            ? "ASTC texture is unsupported on current device and no transcode path is available"
                            : "Texture format is unsupported on current device";
}

EFormat::T vkFormatToTextureFormat(uint32_t vkFormat)
{
    switch (vkFormat) {
    case VK_FORMAT_R8_UNORM: return EFormat::R8_UNORM;
    case VK_FORMAT_R8G8_UNORM: return EFormat::R8G8_UNORM;
    case VK_FORMAT_R8G8B8A8_UNORM: return EFormat::R8G8B8A8_UNORM;
    case VK_FORMAT_R8G8B8A8_SRGB: return EFormat::R8G8B8A8_SRGB;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return EFormat::R16G16B16A16_SFLOAT;
    case VK_FORMAT_R32_SFLOAT: return EFormat::R32_SFLOAT;
    case VK_FORMAT_BC1_RGB_UNORM_BLOCK: return EFormat::BC1_RGB_UNORM_BLOCK;
    case VK_FORMAT_BC1_RGB_SRGB_BLOCK: return EFormat::BC1_RGB_SRGB_BLOCK;
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return EFormat::BC1_RGBA_UNORM_BLOCK;
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK: return EFormat::BC1_RGBA_SRGB_BLOCK;
    case VK_FORMAT_BC3_UNORM_BLOCK: return EFormat::BC3_UNORM_BLOCK;
    case VK_FORMAT_BC3_SRGB_BLOCK: return EFormat::BC3_SRGB_BLOCK;
    case VK_FORMAT_BC4_UNORM_BLOCK: return EFormat::BC4_UNORM_BLOCK;
    case VK_FORMAT_BC4_SNORM_BLOCK: return EFormat::BC4_SNORM_BLOCK;
    case VK_FORMAT_BC5_UNORM_BLOCK: return EFormat::BC5_UNORM_BLOCK;
    case VK_FORMAT_BC5_SNORM_BLOCK: return EFormat::BC5_SNORM_BLOCK;
    case VK_FORMAT_BC7_UNORM_BLOCK: return EFormat::BC7_UNORM_BLOCK;
    case VK_FORMAT_BC7_SRGB_BLOCK: return EFormat::BC7_SRGB_BLOCK;
    case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK: return EFormat::ETC2_R8G8B8_UNORM_BLOCK;
    case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK: return EFormat::ETC2_R8G8B8_SRGB_BLOCK;
    case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK: return EFormat::ETC2_R8G8B8A1_UNORM_BLOCK;
    case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK: return EFormat::ETC2_R8G8B8A1_SRGB_BLOCK;
    case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK: return EFormat::ETC2_R8G8B8A8_UNORM_BLOCK;
    case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK: return EFormat::ETC2_R8G8B8A8_SRGB_BLOCK;
    case VK_FORMAT_ASTC_4x4_UNORM_BLOCK: return EFormat::ASTC_4x4_UNORM_BLOCK;
    case VK_FORMAT_ASTC_4x4_SRGB_BLOCK: return EFormat::ASTC_4x4_SRGB_BLOCK;
    case VK_FORMAT_ASTC_5x5_UNORM_BLOCK: return EFormat::ASTC_5x5_UNORM_BLOCK;
    case VK_FORMAT_ASTC_5x5_SRGB_BLOCK: return EFormat::ASTC_5x5_SRGB_BLOCK;
    case VK_FORMAT_ASTC_6x6_UNORM_BLOCK: return EFormat::ASTC_6x6_UNORM_BLOCK;
    case VK_FORMAT_ASTC_6x6_SRGB_BLOCK: return EFormat::ASTC_6x6_SRGB_BLOCK;
    case VK_FORMAT_ASTC_8x8_UNORM_BLOCK: return EFormat::ASTC_8x8_UNORM_BLOCK;
    case VK_FORMAT_ASTC_8x8_SRGB_BLOCK: return EFormat::ASTC_8x8_SRGB_BLOCK;
    case VK_FORMAT_ASTC_10x10_UNORM_BLOCK: return EFormat::ASTC_10x10_UNORM_BLOCK;
    case VK_FORMAT_ASTC_10x10_SRGB_BLOCK: return EFormat::ASTC_10x10_SRGB_BLOCK;
    default: return EFormat::Undefined;
    }
}

EFormat::T glInternalFormatToTextureFormat(uint32_t glInternalFormat)
{
    switch (glInternalFormat) {
    case 0x8229: return EFormat::R8_UNORM;
    case 0x822B: return EFormat::R8G8_UNORM;
    case 0x8058: return EFormat::R8G8B8A8_UNORM;
    case 0x8C43: return EFormat::R8G8B8A8_SRGB;
    case 0x881A: return EFormat::R16G16B16A16_SFLOAT;
    case 0x822E: return EFormat::R32_SFLOAT;
    case 0x83F0: return EFormat::BC1_RGB_UNORM_BLOCK;
    case 0x8C4C: return EFormat::BC1_RGB_SRGB_BLOCK;
    case 0x83F1: return EFormat::BC1_RGBA_UNORM_BLOCK;
    case 0x8C4D: return EFormat::BC1_RGBA_SRGB_BLOCK;
    case 0x83F3: return EFormat::BC3_UNORM_BLOCK;
    case 0x8C4F: return EFormat::BC3_SRGB_BLOCK;
    case 0x8DBB: return EFormat::BC4_UNORM_BLOCK;
    case 0x8DBC: return EFormat::BC4_SNORM_BLOCK;
    case 0x8DBD: return EFormat::BC5_UNORM_BLOCK;
    case 0x8DBE: return EFormat::BC5_SNORM_BLOCK;
    case 0x8E8C: return EFormat::BC7_UNORM_BLOCK;
    case 0x8E8D: return EFormat::BC7_SRGB_BLOCK;
    case 0x9274: return EFormat::ETC2_R8G8B8_UNORM_BLOCK;
    case 0x9275: return EFormat::ETC2_R8G8B8_SRGB_BLOCK;
    case 0x9276: return EFormat::ETC2_R8G8B8A1_UNORM_BLOCK;
    case 0x9277: return EFormat::ETC2_R8G8B8A1_SRGB_BLOCK;
    case 0x9278: return EFormat::ETC2_R8G8B8A8_UNORM_BLOCK;
    case 0x9279: return EFormat::ETC2_R8G8B8A8_SRGB_BLOCK;
    case 0x93B0: return EFormat::ASTC_4x4_UNORM_BLOCK;
    case 0x93D0: return EFormat::ASTC_4x4_SRGB_BLOCK;
    case 0x93B2: return EFormat::ASTC_5x5_UNORM_BLOCK;
    case 0x93D2: return EFormat::ASTC_5x5_SRGB_BLOCK;
    case 0x93B4: return EFormat::ASTC_6x6_UNORM_BLOCK;
    case 0x93D4: return EFormat::ASTC_6x6_SRGB_BLOCK;
    case 0x93B7: return EFormat::ASTC_8x8_UNORM_BLOCK;
    case 0x93D7: return EFormat::ASTC_8x8_SRGB_BLOCK;
    case 0x93BB: return EFormat::ASTC_10x10_UNORM_BLOCK;
    case 0x93DB: return EFormat::ASTC_10x10_SRGB_BLOCK;
    default: return EFormat::Undefined;
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
} // namespace

std::string toLowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string textureExtension(const std::string& filepath)
{
    return toLowerCopy(path_utils::pathToUtf8String(std::filesystem::path(filepath).extension()));
}

bool isKtxPath(const std::string& filepath)
{
    const std::string extension = textureExtension(filepath);
    return extension == ".ktx" || extension == ".ktx2";
}

std::string findCompanionKtx2Path(const std::string& filepath)
{
    if (textureExtension(filepath) != ".ktx") {
        return {};
    }

    const auto source = std::filesystem::path(filepath);
    auto       candidate = source.parent_path() / "ktx2" / source.stem();
    candidate += ".ktx2";

    const auto candidatePath = path_utils::pathToGenericUtf8String(candidate);
    return fileExistsInVfs(candidatePath) ? candidatePath : std::string{};
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

bool applyKtxResolvedSettings(const std::string& filepath,
                              AssetManager::ResolvedTextureImportSettings& settings)
{
    auto texture = loadKtxTexture(filepath, KTX_TEXTURE_CREATE_NO_FLAGS);
    if (!texture) {
        return false;
    }

    settings.sourceInfo.filepath         = filepath;
    settings.sourceInfo.extension        = textureExtension(filepath);
    settings.sourceInfo.width            = texture->baseWidth;
    settings.sourceInfo.height           = texture->baseHeight;
    settings.sourceInfo.bIsCompressed    = texture->isCompressed;
    settings.sourceKind                  = texture->isCompressed ? AssetManager::ETextureSourceKind::Compressed
                                                                 : AssetManager::ETextureSourceKind::Data;
    settings.sourceInfo.detectedKind     = settings.sourceKind;
    settings.sourceNeedsTranscoding      = false;
    settings.sourceFormat                = resolveKtxTextureFormat(texture.get());

    if (texture->classId == ktxTexture2_c && ktxTexture_NeedsTranscoding(texture.get())) {
        auto* texture2 = reinterpret_cast<ktxTexture2*>(texture.get());
        settings.sourceNeedsTranscoding      = true;
        settings.sourceInfo.detectedChannels = ktxTexture2_GetNumComponents(texture2);
        settings.resolvedChannels            = std::clamp(settings.sourceInfo.detectedChannels, 1u, 4u);
        settings.payloadType                 = AssetManager::ETexturePayloadType::CompressedBytes;
        settings.decodePrecision             = AssetManager::ETextureDecodePrecision::Auto;
        finalizeKtxImportSettings(settings);
        return true;
    }

    const auto format = settings.sourceFormat;
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

    settings.sourceInfo.detectedChannels = textureChannelCount(format);
    settings.resolvedFormat              = format;
    settings.resolvedChannels            = textureChannelCount(format);
    settings.colorSpace                  = EFormat::isSRGB(format)
                                            ? AssetManager::ETextureColorSpace::SRGB
                                            : AssetManager::ETextureColorSpace::Linear;

    if (texture->isCompressed) {
        settings.payloadType     = AssetManager::ETexturePayloadType::CompressedBytes;
        settings.decodePrecision = AssetManager::ETextureDecodePrecision::Auto;
        finalizeKtxImportSettings(settings);
        return true;
    }

    if (const auto* traits = AssetManager::getFormatTraits(format)) {
        settings.payloadType     = traits->payloadType;
        settings.decodePrecision = traits->decodePrecision;
        finalizeKtxImportSettings(settings);
        return true;
    }

    YA_CORE_ERROR("Unsupported uncompressed KTX format for '{}'", filepath);
    return false;
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
        if (settings.uploadStrategy == AssetManager::ETextureUploadStrategy::Placeholder) {
            YA_CORE_WARN("decodeTextureToMemory: Using placeholder path for '{}' ({})",
                         settings.requestedFilepath,
                         settings.diagnostic);
            result.hardFailure = true;
            result.error       = settings.diagnostic.empty() ? "Texture import fell back to placeholder"
                                                             : settings.diagnostic;
            return result;
        }

        auto texture = loadKtxTexture(settings.sourceInfo.filepath, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT);
        if (!texture) {
            result.hardFailure = true;
            result.error       = "Failed to load KTX texture";
            return result;
        }

        if (texture->numFaces != 1 || texture->numLayers != 1 || texture->baseDepth > 1 || texture->isCubemap) {
            YA_CORE_ERROR("decodeTextureToMemory: Only 2D KTX textures are supported for '{}'",
                          settings.sourceInfo.filepath);
            result.hardFailure = true;
            result.error       = "Only 2D KTX textures are supported";
            return result;
        }

        if (ktxTexture_NeedsTranscoding(texture.get())) {
            if (settings.uploadStrategy != AssetManager::ETextureUploadStrategy::TranscodeKtx2 ||
                settings.transcodeTarget == AssetManager::ETextureTranscodeTarget::None ||
                texture->classId != ktxTexture2_c)
            {
                YA_CORE_ERROR("decodeTextureToMemory: No runtime transcode plan for '{}'",
                              settings.sourceInfo.filepath);
                result.hardFailure = true;
                result.error       = "No runtime transcode plan for KTX2 texture";
                return result;
            }

            auto* texture2 = reinterpret_cast<ktxTexture2*>(texture.get());
            const auto transcodeFormat = toKtxTranscodeFormat(settings.transcodeTarget);
            const auto transcodeResult = ktxTexture2_TranscodeBasis(texture2, transcodeFormat, KTX_TF_HIGH_QUALITY);
            if (transcodeResult != KTX_SUCCESS) {
                YA_CORE_ERROR("decodeTextureToMemory: Failed to transcode '{}' to target {}: {}",
                              settings.sourceInfo.filepath,
                              static_cast<int>(settings.transcodeTarget),
                              ktxErrorString(transcodeResult));
                result.hardFailure = true;
                result.error       = "Failed to transcode KTX2 texture";
                return result;
            }

            result.format   = transcodeTargetToFormat(settings.transcodeTarget, settings.colorSpace);
            result.channels = textureChannelCount(result.format);
        }

        const auto size = static_cast<size_t>(ktxTexture_GetDataSize(texture.get()));
        const auto* data = ktxTexture_GetData(texture.get());
        if (size == 0 || data == nullptr) {
            YA_CORE_ERROR("decodeTextureToMemory: KTX image data is empty for '{}'",
                          settings.sourceInfo.filepath);
            result.hardFailure = true;
            result.error       = "KTX image data is empty";
            return result;
        }

        result.width     = texture->baseWidth;
        result.height    = texture->baseHeight;
        result.channels  = textureChannelCount(result.format);
        result.mipLevels = texture->numLevels > 0 ? texture->numLevels : 1;
        result.bytes.resize(size);
        std::memcpy(result.bytes.data(), data, size);
        return result;
    }

    if (settings.payloadType == AssetManager::ETexturePayloadType::U8) {
        stbi_uc* raw = stbi_load(settings.sourceInfo.filepath.c_str(), &width, &height, &channels, desiredChannels);
        if (!raw) {
            YA_CORE_ERROR("decodeTextureToMemory: Failed to load '{}'", settings.sourceInfo.filepath);
            result.hardFailure = true;
            result.error       = "Failed to load texture bytes";
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
        result.hardFailure = true;
        result.error       = "Failed to load HDR texture bytes";
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
        result.hardFailure = true;
        result.error       = "Unsupported payload type";
        return result;
    }

    stbi_image_free(rawFloat);

    return result;
}

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
    return detected;
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

} // namespace ya::asset_manager_texture_detail