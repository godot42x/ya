#include "RHI/Render.h"
#include "RHI/Backend/Vulkan/VulkanRender.h"

#include <gtest/gtest.h>

namespace ya
{

// Vulkan backend build/link closure smoke: this target links ONLY the RHI
// line (foundation + rhi + backend-common + vulkan). It proves the Vulkan
// backend is independently consumable without GUI/Render3D/Host. The checks
// are compile/link-time: creating a live IRender requires a window/device.
TEST(RHIVulkanSmoke, VulkanTypesAreConsumable)
{
    static_assert(std::is_class_v<IRender>, "IRender must be a class type");
    static_assert(std::is_class_v<VulkanRender>, "VulkanRender must be a class type");
    EXPECT_TRUE(true);
}

} // namespace ya
