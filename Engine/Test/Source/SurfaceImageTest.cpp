#include "Render3D/Common/SurfaceImage.h"

#include <gtest/gtest.h>

namespace ya
{
namespace
{

constexpr EFormat::T kStoreAsGiven = EFormat::B8G8R8A8_UNORM;
constexpr EFormat::T kEncodeOnWrite = EFormat::B8G8R8A8_SRGB;

/// The surface pass writes a finished image and nothing else, so the one thing
/// it can get wrong is the pairing of that image with the surface format. Three
/// of the four pairings are fine and one is a defect, and the defect is the one
/// the surface itself would cause -- the reason this is a check and not a
/// convention.
TEST(SurfaceImageTest, OnlyTheSurfaceReEncodingAnEncodedImageIsRefused)
{
    const FSurfaceImage encoded{.encoding = EImageEncoding::DisplayEncoded};
    const FSurfaceImage linear{.encoding = EImageEncoding::Linear};

    /// The ordinary path: the View's finalize encoded the image and the surface
    /// stores what it is given.
    EXPECT_EQ(findSurfaceImageMismatch(encoded, kStoreAsGiven), nullptr);
    /// Letting the hardware encode: linear values into a surface whose format
    /// applies the transfer function. This is what "present does the encoding"
    /// looks like, and it is correct.
    EXPECT_EQ(findSurfaceImageMismatch(linear, kEncodeOnWrite), nullptr);
    /// Linear on a surface that stores as given: the surface is transparent, and
    /// what the image looks like is the producer's business (this is the gamma
    /// correction switch turned off).
    EXPECT_EQ(findSurfaceImageMismatch(linear, kStoreAsGiven), nullptr);

    /// The defect: the image already carries the transfer function and the
    /// surface would apply it again.
    const char* mismatch = findSurfaceImageMismatch(encoded, kEncodeOnWrite);
    ASSERT_NE(mismatch, nullptr);
    EXPECT_NE(std::string(mismatch).find("second time"), std::string::npos) << mismatch;
}

TEST(SurfaceImageTest, EncodesOnWriteFollowsTheSurfaceFormat)
{
    EXPECT_TRUE(surfaceEncodesOnWrite(kEncodeOnWrite));
    EXPECT_FALSE(surfaceEncodesOnWrite(kStoreAsGiven));
    /// An unknown surface format is not claimed to encode: the gate reports a
    /// mismatch for the pairings it can name, and does not invent one.
    EXPECT_FALSE(surfaceEncodesOnWrite(EFormat::Undefined));
    EXPECT_EQ(findSurfaceImageMismatch(FSurfaceImage{}, EFormat::Undefined), nullptr);
}

} // namespace
} // namespace ya
