// DeferredFrameResourceSet::buildLightInstanceBuffer contract: the staging
// struct maps FIELD-BY-FIELD onto the generated GPU element (never a
// reinterpret -- the staging side carries shadow matrices the SSBO element
// does not), and the shadow budget is omni-only with spots skipped.

#include "Render3D/Deferred/DeferredFrameResourceSet.h"
#include "Render3D/RenderFrameData.h"

#include <gtest/gtest.h>

namespace ya
{

namespace
{

RenderFrameData frameWithLights(std::initializer_list<std::pair<uint32_t, float>> kindAndFarPlane)
{
    RenderFrameData frame;
    for (const auto& [kind, farPlane] : kindAndFarPlane) {
        auto& light   = frame.lightInstances.emplace_back();
        light.kind    = kind;
        light.farPlane = farPlane;
        light.position = {1.0f, 2.0f, 3.0f};
        light.color    = {0.5f, 0.6f, 0.7f};
        light.intensity = 4.0f;
        light.spotDir  = {0.0f, 0.0f, -1.0f};
        light.innerCos = 0.9f;
        light.outerCos = 0.8f;
    }
    frame.numLightInstances = static_cast<uint32_t>(frame.lightInstances.size());
    return frame;
}

TEST(DeferredLightBufferTest, OmniInsideBudgetCarriesFarPlaneAndFields)
{
    const RenderFrameData frame = frameWithLights({{0.0f, 25.0f}});
    uint32_t slotsUsed = 99;
    const auto lights = DeferredFrameResourceSet::buildLightInstanceBuffer(frame, 1, &slotsUsed);

    ASSERT_EQ(lights.size(), 1u);
    EXPECT_EQ(lights[0].kind, 0u);
    EXPECT_FLOAT_EQ(lights[0].farPlane, 25.0f);
    EXPECT_FLOAT_EQ(lights[0].pos.x, 1.0f);
    EXPECT_FLOAT_EQ(lights[0].pos.y, 2.0f);
    EXPECT_FLOAT_EQ(lights[0].pos.z, 3.0f);
    EXPECT_FLOAT_EQ(lights[0].color.r, 0.5f);
    EXPECT_FLOAT_EQ(lights[0].intensity, 4.0f);
    EXPECT_EQ(slotsUsed, 1u);
}

TEST(DeferredLightBufferTest, OmniPastBudgetShadesUnshadowed)
{
    const RenderFrameData frame = frameWithLights({{0.0f, 25.0f}, {0.0f, 30.0f}});
    uint32_t slotsUsed = 0;
    const auto lights = DeferredFrameResourceSet::buildLightInstanceBuffer(frame, 1, &slotsUsed);

    ASSERT_EQ(lights.size(), 2u);
    EXPECT_FLOAT_EQ(lights[0].farPlane, 25.0f);
    EXPECT_FLOAT_EQ(lights[1].farPlane, 0.0f);
    EXPECT_EQ(slotsUsed, 1u);
}

TEST(DeferredLightBufferTest, SpotSkipsTheShadowBudget)
{
    // Spot first: it must not consume a slot, so the omni behind it shadows.
    const RenderFrameData frame = frameWithLights({{1.0f, 40.0f}, {0.0f, 30.0f}});
    uint32_t slotsUsed = 0;
    const auto lights = DeferredFrameResourceSet::buildLightInstanceBuffer(frame, 1, &slotsUsed);

    ASSERT_EQ(lights.size(), 2u);
    EXPECT_EQ(lights[0].kind, 1u);
    EXPECT_FLOAT_EQ(lights[0].farPlane, 0.0f);
    EXPECT_FLOAT_EQ(lights[0].spotDir.z, -1.0f);
    EXPECT_FLOAT_EQ(lights[0].innerCos, 0.9f);
    EXPECT_FLOAT_EQ(lights[0].outerCos, 0.8f);
    EXPECT_FLOAT_EQ(lights[1].farPlane, 30.0f);
    EXPECT_EQ(slotsUsed, 1u);
}

TEST(DeferredLightBufferTest, GpuElementIsTheGeneratedLayout)
{
    // The upload writes these elements byte-for-byte into the storage buffer
    // the shader reads: the type must stay the generated one.
    static_assert(std::is_same_v<DeferredFrameResourceSet::LightInstanceGpu,
                                 slang_types::DeferredRender::LightPass::LightInstance>);
    static_assert(sizeof(DeferredFrameResourceSet::LightInstanceGpu) == 72);
}

} // namespace
} // namespace ya
