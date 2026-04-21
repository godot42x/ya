#pragma once

#include "Resource/AssetManager.h"

namespace ya::asset_manager_texture_detail
{

std::string toLowerCopy(std::string value);
std::string textureExtension(const std::string& filepath);
bool        isKtxPath(const std::string& filepath);
std::string findCompanionKtx2Path(const std::string& filepath);
uint32_t    textureChannelCount(EFormat::T format);

bool applyKtxResolvedSettings(const std::string& filepath,
                              AssetManager::ResolvedTextureImportSettings& settings);

AssetManager::TextureMemoryBlock decodeTextureToMemory(const AssetManager::ResolvedTextureImportSettings& settings);

AssetManager::ETextureColorSpace parseColorSpace(const std::string& raw,
                                                 AssetManager::ETextureColorSpace fallback);
AssetManager::ETextureSourceKind parseSourceKind(const std::string& raw,
                                                 AssetManager::ETextureSourceKind detected);
AssetManager::ETextureChannelPolicy parseChannelPolicy(const std::string& raw);
AssetManager::ETextureDecodePrecision parseDecodePrecision(const std::string& raw);

} // namespace ya::asset_manager_texture_detail