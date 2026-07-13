#pragma once

#include "Render/Core/IRenderTarget.h"
#include "Render/Core/Image.h"
#include "Render/Core/Sampler.h"

#include <array>
#include <string_view>

namespace ya
{

struct ShadowMapResourceDesc
{
    std::string_view renderTargetLabel;
    std::string_view samplerLabel;
    std::string_view viewLabelPrefix;
    Extent2D         extent{};
    EFormat::T       depthFormat = EFormat::D32_SFLOAT;
};

struct ShadowMapResources
{
    stdptr<IRenderTarget>                                           renderTarget;
    stdptr<Sampler>                                                 sampler;
    stdptr<IImage>                                                  depthImage;
    Extent2D                                                        extent{};
    EFormat::T                                                      depthFormat = EFormat::Undefined;
    uint32_t                                                        layerCount = 0;
    stdptr<IImageView>                                              directionalDepthIV;
    std::array<stdptr<IImageView>, MAX_POINT_LIGHTS>                pointCubeIVs{};
    std::array<std::array<stdptr<IImageView>, 6>, MAX_POINT_LIGHTS> pointFaceIVs{};

    void init(IRender* render, const ShadowMapResourceDesc& desc);
    void destroy();
    void rebuildViews(IRender* render, std::string_view viewLabelPrefix);

    [[nodiscard]] bool isDirty() const
    {
        return renderTarget && renderTarget->isDirty();
    }

    [[nodiscard]] bool hasAttachmentDirty() const
    {
        return renderTarget && renderTarget->hasAttachmentDirty();
    }

    [[nodiscard]] bool flushIfDirty() const
    {
        return renderTarget && renderTarget->flushIfDirty();
    }
};

} // namespace ya
