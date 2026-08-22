#pragma once

#include "Core/Base.h"

#include "RHI/RenderDefines.h"

namespace ya
{

/**
 * Holds the runtime viewport state: the viewport rect (in logical pixels),
 * the frame-buffer scale and the world-scene render flag.
 *
 * Pure state — the service neither touches the render backend nor the
 * pipelines. Notification (e.g. forwarding a resize to the active pipeline)
 * and first-frame initialization remain with the orchestrator (RenderRuntime),
 * which reads/writes this state through the service.
 */
struct YA_RENDER_3D_API ViewportStateService
{
    void setRect(const Rect2D& rect) { _viewportRect = rect; }
    [[nodiscard]] const Rect2D& getRect() const { return _viewportRect; }
    [[nodiscard]] bool isRectInitialized() const
    {
        return _viewportRect.extent.x > 0 && _viewportRect.extent.y > 0;
    }

    void setFrameBufferScale(float scale) { _viewportFrameBufferScale = scale; }
    [[nodiscard]] float getFrameBufferScale() const { return _viewportFrameBufferScale; }

    void setWorldSceneRenderEnabled(bool bEnabled) { _bWorldSceneRenderEnabled = bEnabled; }
    [[nodiscard]] bool isWorldSceneRenderEnabled() const { return _bWorldSceneRenderEnabled; }

  private:
    Rect2D _viewportRect{};
    float  _viewportFrameBufferScale = 1.0f;
    bool   _bWorldSceneRenderEnabled = true;
};

} // namespace ya