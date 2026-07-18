#pragma once

#include "Render/Core/Image.h"

#include <memory>
#include <string>
#include <vector>

namespace ya
{

struct RenderImage;

struct RenderViewportSnapshot
{
    bool bForwardPipeline = false;

    std::shared_ptr<RenderImage> viewportImageOwner = nullptr;
    IImageView*                  viewportImageView  = nullptr;
    bool                         bPostprocessingEnabled = false;

    struct ImageSlot
    {
        std::string             label;
        IImageView*             defaultView = nullptr;
        std::shared_ptr<IImageView> ownedView;
        std::shared_ptr<IImage> image;
        uint32_t                categoryIndex = 0;
        EImageAspect::T         aspectFlags   = EImageAspect::Color;
        glm::vec4               tint          = glm::vec4(1.0f);
    };

    struct DebugSpec
    {
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
        std::vector<ImageSlot> slots;
        std::vector<Group> groups;
    } debugSpec;
};

} // namespace ya
