#include "GUI/Compose/GUIRenderSurface.h"

#include <gtest/gtest.h>

namespace ya
{
namespace
{

TEST(GUIRenderSurfaceTest, DisplayComposeLayoutIsPresentSrc)
{
    EXPECT_TRUE(GUIRenderSurface::isDisplayComposeFinalLayout(EImageLayout::PresentSrcKHR));
    EXPECT_FALSE(GUIRenderSurface::isDisplayComposeFinalLayout(EImageLayout::ShaderReadOnlyOptimal));
    EXPECT_FALSE(GUIRenderSurface::isDisplayComposeFinalLayout(EImageLayout::ColorAttachmentOptimal));
}

} // namespace
} // namespace ya
