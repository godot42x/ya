#pragma once

#include "Foundation/RHI/RenderDefines.h"

#include <array>
#include <cstddef>
#include <string>

namespace ya
{

struct TextureDataCreateInfo
{
    std::string label;
    uint32_t    width     = 0;
    uint32_t    height    = 0;
    const void* data      = nullptr;
    size_t      dataSize  = 0;
    EFormat::T  format    = EFormat::R8G8B8A8_UNORM;
    uint32_t    mipLevels = 1;
};

struct RenderTextureCreateInfo
{
    std::string     label;
    uint32_t        width      = 0;
    uint32_t        height     = 0;
    EFormat::T      format     = EFormat::R8G8B8A8_UNORM;
    EImageUsage::T  usage      = EImageUsage::ColorAttachment;
    ESampleCount::T samples    = ESampleCount::Sample_1;
    bool            isDepth    = false;
    uint32_t        layerCount = 1;
    uint32_t        mipLevels  = 1;
};

enum ECubeFace
{
    CubeFace_PosX = 0,
    CubeFace_NegX,
    CubeFace_PosY,
    CubeFace_NegY,
    CubeFace_PosZ,
    CubeFace_NegZ,
    CubeFace_Count
};

struct CubeMapCreateInfo
{
    std::string                             label;
    std::array<std::string, CubeFace_Count> files;
    bool                                    flipVertical = false;
};

} // namespace ya
