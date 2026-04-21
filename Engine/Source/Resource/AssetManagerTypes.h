#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Render/Core/Texture.h"
#include "Render/Model.h"

namespace ya
{

using AssetTextureBatchMemoryHandle = uint64_t;

enum class AssetTextureColorSpace
{
    SRGB,
    Linear,
};

enum class AssetTextureSourceKind
{
    Auto,
    LDR,
    HDR,
    Data,
    Compressed,
};

enum class AssetTexturePayloadType
{
    None,
    U8,
    F16,
    F32,
    CompressedBytes,
};

enum class AssetTextureDecodePrecision
{
    Auto,
    U8,
    F16,
    F32,
};

enum class AssetTextureChannelPolicy
{
    Preserve,
    ForceRGBA,
};

enum class AssetTextureUploadStrategy
{
    Direct,
    TranscodeKtx2,
    Placeholder,
};

enum class AssetTextureTranscodeTarget
{
    None,
    BC7,
    BC3,
    ETC2_RGBA,
    RGBA32,
};

struct AssetTextureFormatTraits
{
    AssetTexturePayloadType     payloadType;
    AssetTextureDecodePrecision decodePrecision;
    uint32_t                    channels;
};

struct AssetTextureSourceInfo
{
    std::string            filepath;
    std::string            extension;
    uint32_t               width            = 0;
    uint32_t               height           = 0;
    uint32_t               detectedChannels = 0;
    AssetTextureSourceKind detectedKind     = AssetTextureSourceKind::LDR;
    bool                   bIsHDR           = false;
    bool                   bIsCompressed    = false;

    [[nodiscard]] bool isValid() const
    {
        return width > 0 && height > 0;
    }
};

struct AssetResolvedTextureImportSettings
{
    std::string            requestedFilepath;
    AssetTextureSourceInfo sourceInfo;
    AssetTextureColorSpace colorSpace = AssetTextureColorSpace::SRGB;
    AssetTextureSourceKind sourceKind = AssetTextureSourceKind::LDR;
    AssetTextureDecodePrecision decodePrecision = AssetTextureDecodePrecision::U8;
    AssetTextureChannelPolicy channelPolicy = AssetTextureChannelPolicy::ForceRGBA;
    AssetTexturePayloadType payloadType = AssetTexturePayloadType::U8;
    EFormat::T sourceFormat = EFormat::Undefined;
    bool sourceNeedsTranscoding = false;
    EFormat::T resolvedFormat = EFormat::R8G8B8A8_UNORM;
    uint32_t resolvedChannels = 4;
    AssetTextureUploadStrategy uploadStrategy = AssetTextureUploadStrategy::Direct;
    AssetTextureTranscodeTarget transcodeTarget = AssetTextureTranscodeTarget::None;
    std::string diagnostic;

    [[nodiscard]] bool isHDR() const
    {
        return sourceKind == AssetTextureSourceKind::HDR;
    }
};

struct AssetTextureMemoryBlock
{
    std::string                        filepath;
    uint32_t                           width       = 0;
    uint32_t                           height      = 0;
    uint32_t                           channels    = 4;
    uint32_t                           mipLevels   = 1;
    EFormat::T                         format      = EFormat::R8G8B8A8_UNORM;
    AssetTexturePayloadType            payloadType = AssetTexturePayloadType::None;
    AssetResolvedTextureImportSettings importSettings;
    bool                               hardFailure = false;
    std::string                        error;
    std::vector<uint8_t>               bytes;

    [[nodiscard]] bool isValid() const
    {
        return !bytes.empty() && width > 0 && height > 0;
    }

    [[nodiscard]] size_t dataSize() const
    {
        return bytes.size();
    }

    [[nodiscard]] const void* data() const
    {
        return bytes.empty() ? nullptr : bytes.data();
    }
};

struct AssetTextureBatchMemory
{
    std::vector<AssetTextureMemoryBlock> textures;

    [[nodiscard]] bool isValid() const
    {
        if (textures.empty()) {
            return false;
        }

        for (const auto& texture : textures) {
            if (!texture.isValid()) {
                return false;
            }
        }

        return true;
    }
};

using AssetTextureReadyCallback = std::function<void(const std::shared_ptr<Texture>&)>;
using AssetModelReadyCallback = std::function<void(const std::shared_ptr<Model>&)>;
using AssetTextureBatchReadyCallback = std::function<void(const std::vector<std::shared_ptr<Texture>>&)>;

struct AssetTextureLoadRequest
{
    std::string               filepath;
    std::string               name;
    AssetTextureReadyCallback onReady;
    AssetTextureColorSpace    colorSpace = AssetTextureColorSpace::SRGB;
    std::optional<FName>      textureSemantic;
};

struct AssetTextureBatchLoadRequest
{
    std::vector<std::string>     filepaths;
    AssetTextureBatchReadyCallback onDone;
    AssetTextureColorSpace         colorSpace = AssetTextureColorSpace::SRGB;
};

struct AssetTextureBatchMemoryLoadRequest
{
    std::vector<std::string> filepaths;
    AssetTextureColorSpace   colorSpace = AssetTextureColorSpace::SRGB;
};

struct AssetModelLoadRequest
{
    std::string             filepath;
    std::string             name;
    AssetModelReadyCallback onReady;
};

} // namespace ya