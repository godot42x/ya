#include "GameEditor/UI/Shell/EditorSurfaceContext.h"

#include "GameRuntime/App.h"
#include "GUI/Host/GUIWindowChrome.h"
#include "GUI/Host/GUIWindowPlacement.h"
#include "GUI/Widgets/WidgetTree.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/Core/Swapchain.h"
#include "RHI/NativeWindow.h"
#include "RHI/Render.h"

#include <algorithm>

namespace ya
{

EditorWindowMetrics makeEditorWindowMetrics(int windowW,
                                            int windowH,
                                            float windowDpi,
                                            Extent2D framebufferExtent)
{
    EditorWindowMetrics metrics;
    metrics.logicalExtent = Extent2D{
        .width  = static_cast<uint32_t>(std::max(windowW, 1)),
        .height = static_cast<uint32_t>(std::max(windowH, 1)),
    };
    metrics.framebufferExtent = framebufferExtent;
    metrics.dpiScale          = windowDpi > 0.0f ? windowDpi : 1.0f;
    if (windowW > 0 && windowH > 0 && framebufferExtent.width > 0 && framebufferExtent.height > 0) {
        metrics.dpiScale = static_cast<float>(framebufferExtent.width) /
                           static_cast<float>(windowW);
    }
    return metrics;
}

void applyEditorWindowMetrics(WidgetTree& tree, const EditorWindowMetrics& metrics)
{
    tree.setLogicalExtent(Extent2D{
        .width  = std::max(metrics.logicalExtent.width, 1u),
        .height = std::max(metrics.logicalExtent.height, 1u),
    });
    tree.setDpiScale(metrics.dpiScale > 0.0f ? metrics.dpiScale : 1.0f);
}

FEditorSurfaceContext makeEditorSurfaceContext(App& app,
                                               IRenderSurfaceContext& surface,
                                               const HostViewportView& hostViewport)
{
    int      windowW = 0;
    int      windowH = 0;
    float    windowDpi = 1.0f;
    Extent2D framebuffer{};
    FWindowChromeLayout chromeLayout;
    FWindowScreenPlacement screenPlacement;

    if (INativeWindow* window = surface.getNativeWindow()) {
        window->getWindowSize(windowW, windowH);
        windowDpi       = window->getDpiScale();
        chromeLayout    = queryWindowChromeLayout(*window, defaultWindowChromeMode(), true);
        screenPlacement = queryWindowScreenPlacement(*window);
    }
    if (const ISwapchain* swapchain = surface.getSwapchain()) {
        framebuffer = swapchain->getExtent();
    }

    FEditorSurfaceContext context;
    context.app            = &app;
    context.presentSurface = &surface;
    context.metrics        = makeEditorWindowMetrics(windowW, windowH, windowDpi, framebuffer);
    context.metrics.chromeInsetLeft   = chromeLayout.contentInsets.left;
    context.metrics.chromeInsetTop    = chromeLayout.contentInsets.top;
    context.metrics.chromeInsetRight  = chromeLayout.contentInsets.right;
    context.metrics.chromeInsetBottom = chromeLayout.contentInsets.bottom;
    context.metrics.chromeDragGutter  = chromeLayout.dragRegion.width;
    context.metrics.screenX           = screenPlacement.x;
    context.metrics.screenY           = screenPlacement.y;
    context.metrics.monitorIndex      = screenPlacement.monitorIndex;
    context.metrics.bMaximized        = screenPlacement.bMaximized;
    context.metrics.bHasScreenOrigin  = screenPlacement.bHasOrigin;
    context.view                      = hostViewport.view;
    context.projection                = hostViewport.projection;
    return context;
}

} // namespace ya
