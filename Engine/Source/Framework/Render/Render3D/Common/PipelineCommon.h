#pragma once

#include <memory>
#include <string_view>

namespace ya
{

struct IImage;
struct IImageView;
struct ImageResource;

/// Builds an `ImageResource` wrapping an existing shadow depth image + its view,
/// retaining both so they outlive queue submission. Returns nullptr when either
/// handle is missing. Shared by Forward/Deferred debug-output paths.
[[nodiscard]] std::shared_ptr<ImageResource> makeShadowDebugResource(const std::shared_ptr<IImage>& image,
                                                                     const std::shared_ptr<IImageView>& view,
                                                                     std::string_view label);

} // namespace ya