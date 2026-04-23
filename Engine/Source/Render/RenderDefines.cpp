#include "RenderDefines.h"

namespace ya
{

namespace EVertexAttributeFormat
{
namespace
{
std::size_t componentSize(T type)
{
    switch (type) {
    case Uint32:
    case Uint32x2:
    case Uint32x3:
    case Uint32x4:
    case Int32:
    case Int32x2:
    case Int32x3:
    case Int32x4:
    case Float32:
    case Float32x2:
    case Float32x3:
    case Float32x4:
        return 4;
    case Float16:
    case Float16x2:
    case Float16x3:
    case Float16x4:
        return 2;
    default:
        YA_CORE_ASSERT(false, "Invalid vertex attribute format {}", int(type));
        return 0;
    }
}

std::size_t componentCount(T type)
{
    switch (type) {
    case Uint32:
    case Int32:
    case Float16:
    case Float32:
        return 1;
    case Uint32x2:
    case Int32x2:
    case Float16x2:
    case Float32x2:
        return 2;
    case Uint32x3:
    case Int32x3:
    case Float16x3:
    case Float32x3:
        return 3;
    case Uint32x4:
    case Int32x4:
    case Float16x4:
    case Float32x4:
        return 4;
    default:
        YA_CORE_ASSERT(false, "Invalid vertex attribute format {}", int(type));
        return 0;
    }
}
} // namespace

std::size_t T2Size(T type)
{
    return componentSize(type) * componentCount(type);
}
}; // namespace EVertexAttributeFormat
} // namespace ya
