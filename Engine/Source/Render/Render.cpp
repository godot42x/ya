#include "Render.h"


namespace EVertexAttributeFormat
{
std::size_t T2Size(T type)
{
    switch (type) {
    case Float2:
        return sizeof(float) * 2;
    case Float3:
        return sizeof(float) * 3;
    case Float4:
        return sizeof(float) * 4;
    default:
        NE_CORE_ASSERT(false, "Invalid vertex attribute format {}", int(type));
        return 0;
    }
}

} // namespace EVertexAttributeFormat

// namespace ETextureFormat
// {
// // Add texture format handling if needed
// } // namespace ETextureFormat