#include "GUI/Host/GUIPresentationTarget.h"

#include "RHI/Render.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/Core/RenderTexture.h"

namespace ya
{

bool GUIPresentationTarget::buildAll(
    IRender& render,
    IRenderSurfaceContext& surface,
    const char* labelPrefix,
    std::vector<std::shared_ptr<GUIPresentationTarget>>& outTargets)
{
    outTargets.clear();
    IRenderResourceFactory* factory = render.getResourceFactory();
    if (!factory) {
        return false;
    }

    std::vector<std::shared_ptr<RenderTexture>> images;
    if (!surface.buildPresentationImages(*factory, labelPrefix, images)) {
        return false;
    }
    outTargets.reserve(images.size());
    for (auto& image : images) {
        if (!image || !image->isValid()) {
            outTargets.clear();
            return false;
        }
        outTargets.push_back(std::make_shared<GUIPresentationTarget>(GUIPresentationTarget{
            .renderSurface = GUIRenderSurface::wrapExternal(
                std::move(image),
                EImageLayout::PresentSrcKHR),
        }));
    }
    return !outTargets.empty();
}

} // namespace ya
