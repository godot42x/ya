#include "GameEditor/UI/Shell/EditorOpenSplashPage.h"

#include "GameEditor/UI/Shell/EditorTheme.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Image.h"

#include <filesystem>
#include <glm/glm.hpp>

namespace ya
{

namespace
{

/// YA branding mark shown on the open splash.
constexpr const char* kProjectBannerMark = "Engine/Content/Branding/ya-icon.png";

} // namespace

void EditorOpenSplashPage::build(const std::string& projectPath, WidgetTree& tree)
{
    auto bannerProject = ui::text("ProjectOpenBannerName")
                             .setText(std::filesystem::path(projectPath).stem().string())
                             .setStyleKey("text.header")
                             .setHAlign(EWidgetAlignH::Center);
    auto bannerRoot = ui::border("ProjectOpenBanner")
                          .setStyleKey("panel.surface")
                          .setPadding(FMargin::all(20.0f))
                          .child(ui::column("ProjectOpenBannerContent")
                                     .setSpacing(6.0f)
                                     .child(ui::image("ProjectOpenBannerMark")
                                                .setAssetPath(kProjectBannerMark)
                                                .setScaleMode(EImageScaleMode::Contain),
                                            ui::boxSlot().preferredSize({0.0f, 56.0f}))
                                     .child(ui::text("ProjectOpenBannerEyebrow")
                                                .setText("OPENING PROJECT")
                                                .setStyleKey("text.eyebrow")
                                                .setHAlign(EWidgetAlignH::Center))
                                     .child(std::move(bannerProject))
                                     .child(ui::text("ProjectOpenBannerStatus")
                                                .setText("Loading scene, modules and content…")
                                                .setStyleKey("text.muted")
                                                .setHAlign(EWidgetAlignH::Center)));
    _banner = bannerRoot.share();
    // The window is exactly the banner's size: fill it, corners included.
    (void)ui::attach(tree,
                     *tree.getLayer(WidgetTree::ELayer::Content),
                     std::move(bannerRoot).release(),
                     ui::canvasSlot().fill());
}

} // namespace ya
