#include "RenderDefines.h"


namespace EVertexAttributeFormat
{

std::size_t T2Size(T type)
{
    switch (type) {
    case Float:
        return sizeof(float);
    case Float2:
        return sizeof(float) * 2;
    case Float3:
        return sizeof(float) * 3;
    case Float4:
        return sizeof(float) * 4;
    default:
        YA_CORE_ASSERT(false, "Invalid vertex attribute format {}", int(type));
        return 0;
    }
}
}; // namespace EVertexAttributeFormat