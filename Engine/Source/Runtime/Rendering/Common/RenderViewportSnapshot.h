#pragma once

#include "Render/Core/Image.h"

#include <memory>
#include <string>
#include <vector>

namespace ya
{

struct RenderImage;

struct RenderViewportDebugCatalog
{
    struct Slot
    {
        std::string             label;
        uint32_t                categoryIndex = 0;
        EImageAspect::T         aspectFlags   = EImageAspect::Color;
        glm::vec4               tint          = glm::vec4(1.0f);
    };

    struct Category
    {
        std::string id;
        std::string label;
    };

    enum class EGroupType
    {
        Generic,
        CubeMapFaces,
        CubeMapMipFaces,
    };

    struct Group
    {
        std::string              label;
        EGroupType               type = EGroupType::Generic;
        uint32_t                 categoryIndex = 0;
        uint32_t                 beginIndex = 0;
        uint32_t                 slotCount = 0;
        uint32_t                 groupSize = 1;
        std::vector<std::string> itemLabels;
    };

    std::vector<Category> categories;
    std::vector<Slot>     slots;
    std::vector<Group>    groups;
};

struct RenderViewportDebugImageSlot
{
    IImageView*                 defaultView = nullptr;
    std::shared_ptr<IImageView> ownedView;
    std::shared_ptr<IImage>     image;
};

struct RenderViewportSnapshot
{
    bool bForwardPipeline = false;

    std::shared_ptr<RenderImage>              viewportImageOwner = nullptr;
    IImageView*                               viewportImageView  = nullptr;
    // Scene depth of the same viewport render, exposed so editor overlays
    // (e.g. collision debug wireframes) can depth-test against the world.
    std::shared_ptr<RenderImage>              viewportDepthOwner = nullptr;
    bool                                      bPostprocessingEnabled = false;
    std::shared_ptr<const RenderViewportDebugCatalog> debugCatalog = nullptr;
    std::vector<RenderViewportDebugImageSlot>         debugImages;
};

} // namespace ya
