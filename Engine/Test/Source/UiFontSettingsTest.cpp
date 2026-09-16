#include "GameRuntime/Utility/UiFontSettings.h"

#include "Render/Resources/FontManager.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(UiFontSettingsTest, CatalogIdsAreUniqueAndTheDefaultResolves)
{
    std::vector<std::string_view> ids;
    bool                          sawDefault = false;
    for (const FUiFontFace& face : uiFontFaces()) {
        EXPECT_FALSE(face.id.empty());
        EXPECT_FALSE(face.label.empty());
        EXPECT_FALSE(face.bundledPath.empty() && face.systemPath.empty())
            << "face '" << face.id << "' names neither a bundled nor a system file";
        for (std::string_view seen : ids) {
            EXPECT_NE(seen, face.id) << "duplicate catalog id '" << face.id << "'";
        }
        ids.push_back(face.id);
        sawDefault = sawDefault || (face.id == defaultUiFontFace().id);
    }
    // The default is part of the catalog it is the default OF; a default that is
    // not listed would make the settings combo unable to show the current value.
    EXPECT_TRUE(sawDefault);
    EXPECT_EQ(defaultUiFontFace().id, uiFontFaces().front().id)
        << "index 0 is the documented preference order, so it must BE the default";
}

TEST(UiFontSettingsTest, UnknownIdIsNotSilentlyTreatedAsAFace)
{
    EXPECT_EQ(findUiFontFace("not-a-face"), nullptr);
    EXPECT_EQ(findUiFontFace(""), nullptr);
    EXPECT_NE(findUiFontFace(defaultUiFontFace().id), nullptr);
}

TEST(UiFontSettingsTest, MissingFaceResolvesToNoPathInsteadOfThrowing)
{
    // A catalog entry whose file is gone must resolve to empty so the CALLER can
    // report it; returning a path that does not exist would push the failure into
    // FreeType with no context.
    const FUiFontFace absent{.id = "absent", .label = "Absent", .bundledPath = "Engine/Content/Fonts/__nope__.ttf"};
    EXPECT_TRUE(resolveUiFontFacePath(absent).empty());
}

TEST(UiFontSettingsTest, ApplyingAnUnknownFaceIsRefused)
{
    // No render backend is needed to answer the question: an id that is not in
    // the catalog is rejected before any font work happens, so the caller knows
    // the switch did not take instead of believing it did.
    EXPECT_EQ(ui_font_settings::setFaceId("definitely-not-a-face"), ui_font_settings::faceId());
    EXPECT_NE(ui_font_settings::faceId(), "definitely-not-a-face");
}

TEST(UiFontSettingsTest, FaceIdAlwaysReportsSomethingUsable)
{
    // Whatever the config says, the reported id has to be a real catalog entry -
    // it is what the shell loads at startup.
    EXPECT_NE(findUiFontFace(ui_font_settings::faceId()), nullptr);
}

TEST(UiFontSettingsTest, OptionsCarryAvailabilityAndTheCatalogOrder)
{
    const std::vector<ui_font_settings::FOption> options = ui_font_settings::availableFaces();
    ASSERT_EQ(options.size(), uiFontFaces().size());
    for (size_t i = 0; i < options.size(); ++i) {
        EXPECT_EQ(options[i].id, uiFontFaces()[i].id) << "the combo order must mirror the catalog";
        EXPECT_EQ(options[i].bMonospace, uiFontFaces()[i].bMonospace);
        // Availability is a filesystem fact and differs per machine, but a
        // BUNDLED face must be present in a checkout that ships it.
        if (!uiFontFaces()[i].bundledPath.empty()) {
            EXPECT_TRUE(options[i].bAvailable) << "bundled face '" << options[i].id << "' was reported missing";
        }
    }
}

} // namespace ya
