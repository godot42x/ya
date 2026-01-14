
#pragma once

#include "Core/Reflection/Reflection.h"

namespace ya
{
// Generic render types
struct Extent2D
{
    uint32_t width{0};
    uint32_t height{0};
};



struct FAssetPath
{
    YA_REFLECT_BEGIN(FAssetPath)
    YA_REFLECT_FIELD(path)
    YA_REFLECT_END()
    std::string path;
};

struct FSoftObjectReference
{
    YA_REFLECT_BEGIN(FSoftObjectReference)
    YA_REFLECT_FIELD(assetPath)
    YA_REFLECT_FIELD(object)
    YA_REFLECT_END()

    FAssetPath assetPath;
    void      *object;
};

} // namespace ya