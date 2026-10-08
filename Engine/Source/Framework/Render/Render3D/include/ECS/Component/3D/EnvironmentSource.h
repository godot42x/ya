#pragma once

#include "Core/Reflection/Reflection.h"
#include "RHI/Core/TextureCreateInfo.h"

#include <array>
#include <string>

namespace ya
{

// Out-of-line members live in the render-3d DLL, so these types need YA_RENDER_3D_API.
struct YA_RENDER_3D_API CubemapSource
{
    YA_REFLECT_BEGIN(CubemapSource)
    YA_REFLECT_FIELD(files)
    YA_REFLECT_FIELD(flipVertical)
    YA_REFLECT_END()

    std::array<std::string, CubeFace_Count> files{};
    bool                                    flipVertical = false;

    bool hasAllFaces() const;
};

struct YA_RENDER_3D_API CylindricalSource
{
    YA_REFLECT_BEGIN(CylindricalSource)
    YA_REFLECT_FIELD(filepath)
    YA_REFLECT_FIELD(flipVertical)
    YA_REFLECT_END()

    std::string filepath;
    bool        flipVertical = false;

    bool hasFile() const;
};

} // namespace ya
