#include "RHI/NativeWindow.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace ya
{
namespace
{

struct RestoreProcessWindowIcon
{
    std::string previous = processWindowIconPath();
    ~RestoreProcessWindowIcon() { setProcessWindowIconPath(previous); }
};

} // namespace

TEST(NativeWindowIconTest, ProcessPathDefaultsToEngineBranding)
{
    RestoreProcessWindowIcon restore;
    setProcessWindowIconPath({});
    EXPECT_EQ(processWindowIconPath(), std::string(kDefaultWindowIconPath));
    EXPECT_TRUE(std::filesystem::is_regular_file(kDefaultWindowIconPath));
}

TEST(NativeWindowIconTest, ProcessPathEmptyRestoresDefault)
{
    RestoreProcessWindowIcon restore;
    setProcessWindowIconPath("Content/AppIcon.png");
    EXPECT_EQ(processWindowIconPath(), "Content/AppIcon.png");
    setProcessWindowIconPath({});
    EXPECT_EQ(processWindowIconPath(), std::string(kDefaultWindowIconPath));
}

TEST(NativeWindowIconTest, DefaultBrandingIconIsPng)
{
    std::ifstream stream(std::string(kDefaultWindowIconPath), std::ios::binary);
    ASSERT_TRUE(stream.is_open());
    unsigned char magic[8] = {};
    stream.read(reinterpret_cast<char*>(magic), 8);
    ASSERT_EQ(stream.gcount(), 8);
    const unsigned char png[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(magic), 8),
              std::string(reinterpret_cast<const char*>(png), 8));
}

} // namespace ya
