// Whole-value copy contract (YA_REFLECT_COPIES_AS_VALUE): a class that
// declares it is assigned with its C++ copy-assign in one step, so derived
// state the copy constructor maintains (the bound, shared asset handle)
// survives a reflection copy. Classes without the declaration keep the
// field-by-field recursion.

#include "Core/Common/AssetRef.h"
#include "Core/Common/TextureSlot.h"
#include "Core/Reflection/ReflectionCopier.h"
#include "Core/Reflection/DeferredInitializer.h"
#include "Core/System/VirtualFileSystem.h"

#include <gtest/gtest.h>

namespace ya
{
namespace
{

TEST(ReflectionValueCopyTest, DeclaredValueTypesCopyTheirBindingInOneStep)
{
    reflection::DeferredInitializerQueue::instance().executeAll();
    if (!VirtualFileSystem::get()) {
        VirtualFileSystem::init();
    }

    TextureSlot src("Content/Textures/__value_copy_missing.png");
    ASSERT_NE(src.textureRef._handle, nullptr);

    // The slot type declares value-copy: the whole value is assigned, and the
    // copy shares the source's bound slot without any re-resolve step.
    TextureSlot dst;
    ASSERT_TRUE(ReflectionCopier::copyByRuntimeReflection(
        &dst, &src, type_index_v<TextureSlot>, "TextureSlot"));
    EXPECT_EQ(dst.textureRef._handle, src.textureRef._handle);
    EXPECT_EQ(dst.textureRef.getPath(), src.textureRef.getPath());
    EXPECT_EQ(dst.uvScale, src.uvScale);
}

TEST(ReflectionValueCopyTest, UndeclaredClassesStillCopyFieldByField)
{
    reflection::DeferredInitializerQueue::instance().executeAll();

    // SamplerConfig carries no declaration: it has no derived state, and the
    // generic field recursion remains its copy path (not the one-step hook).
    auto* cls = ClassRegistry::instance().getClass(type_index_v<SamplerConfig>);
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->copyAssign, nullptr);

    SamplerConfig src;
    src.filterMode = EFilter::Nearest;
    SamplerConfig dst;
    ASSERT_TRUE(ReflectionCopier::copyByRuntimeReflection(
        &dst, &src, type_index_v<SamplerConfig>, "SamplerConfig"));
    EXPECT_EQ(dst.filterMode, EFilter::Nearest);
}

} // namespace
} // namespace ya
