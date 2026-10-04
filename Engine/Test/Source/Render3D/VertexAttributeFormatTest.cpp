#include "RHI/RenderDefines.h"
#include "Sprite2DWorld.slang.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(VertexAttributeFormat, GeneratedScalarCoversEveryComponentCount)
{
    using Scalar = slang_types::Sprite2DWorld::VertexInputScalar;
    const Scalar scalars[] = {Scalar::Float32, Scalar::Int32, Scalar::Uint32};
    const EVertexAttributeFormat::T expected[3][4] = {
        {EVertexAttributeFormat::Float32, EVertexAttributeFormat::Float32x2, EVertexAttributeFormat::Float32x3, EVertexAttributeFormat::Float32x4},
        {EVertexAttributeFormat::Int32, EVertexAttributeFormat::Int32x2, EVertexAttributeFormat::Int32x3, EVertexAttributeFormat::Int32x4},
        {EVertexAttributeFormat::Uint32, EVertexAttributeFormat::Uint32x2, EVertexAttributeFormat::Uint32x3, EVertexAttributeFormat::Uint32x4},
    };

    for (int scalarIndex = 0; scalarIndex < 3; ++scalarIndex) {
        for (uint32_t components = 1; components <= 4; ++components) {
            EXPECT_EQ(vertexAttributeFormat(scalars[scalarIndex], components), expected[scalarIndex][components - 1]);
        }
    }
}

} // namespace ya
