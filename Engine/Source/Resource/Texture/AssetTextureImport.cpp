#include "Resource/Texture/AssetTextureInternal.h"

#include "Core/Log.h"
#include "stb/stb_image.h"

namespace ya
{
using namespace asset_manager_texture_detail;

AssetManager::ETextureColorSpace AssetManager::inferTextureColorSpace(const FName& textureSemantic)
{
    if (textureSemantic == MatTexture::Diffuse ||
        textureSemantic == MatTexture::Albedo ||
        textureSemantic == MatTexture::Specular ||
        textureSemantic == MatTexture::Emissive) {
        return ETextureColorSpace::SRGB;
    }

    if (textureSemantic == MatTexture::Normal ||
        textureSemantic == MatTexture::Metallic ||
        textureSemantic == MatTexture::Roughness ||
        textureSemantic == MatTexture::MetallicRoughness ||
        textureSemantic == MatTexture::AO) {
        return ETextureColorSpace::Linear;
    }

    return ETextureColorSpace::SRGB;
}

const AssetManager::TextureFormatTraits* AssetManager::getFormatTraits(EFormat::T format)
{
    static const std::unordered_map<EFormat::T, TextureFormatTraits> kTraits = {
        {EFormat::R8_UNORM, {.payloadType = ETexturePayloadType::U8, .decodePrecision = ETextureDecodePrecision::U8, .channels = 1}},
        {EFormat::R8G8_UNORM, {.payloadType = ETexturePayloadType::U8, .decodePrecision = ETextureDecodePrecision::U8, .channels = 2}},
        {EFormat::R8G8B8A8_UNORM, {.payloadType = ETexturePayloadType::U8, .decodePrecision = ETextureDecodePrecision::U8, .channels = 4}},
        {EFormat::R8G8B8A8_SRGB, {.payloadType = ETexturePayloadType::U8, .decodePrecision = ETextureDecodePrecision::U8, .channels = 4}},
        {EFormat::R16G16B16A16_SFLOAT, {.payloadType = ETexturePayloadType::F16, .decodePrecision = ETextureDecodePrecision::F16, .channels = 4}},
        {EFormat::R32_SFLOAT, {.payloadType = ETexturePayloadType::F32, .decodePrecision = ETextureDecodePrecision::F32, .channels = 1}},
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
        ResolvedTextureImportSettings settings;
        if (!applyKtxResolvedSettings(filepath, settings)) {
            return info;
        }
        return settings.sourceInfo;
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
    settings.requestedFilepath = filepath;

    const AssetMeta& meta = getOrLoadMeta(filepath);
    settings.colorSpace   = parseColorSpace(meta.getString("colorSpace", ""), codeHint);

    std::string resolvedSourcePath = filepath;
    if (const auto companionKtx2 = findCompanionKtx2Path(filepath); !companionKtx2.empty()) {
        resolvedSourcePath = companionKtx2;
    }

    settings.sourceInfo = inspectTextureSource(resolvedSourcePath);
    settings.sourceKind = parseSourceKind(meta.getString("sourceKind", "auto"), settings.sourceInfo.detectedKind);

    if (isKtxPath(resolvedSourcePath)) {
        const bool ok = applyKtxResolvedSettings(resolvedSourcePath, settings) ||
                        (resolvedSourcePath != filepath && applyKtxResolvedSettings(filepath, settings));
        (void)ok;
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
                              preferredUploadFormat,
                              filepath);
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

} // namespace ya
