#pragma once

#include "Core/Base.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <memory>

namespace ya
{

struct RenderTexture;

/// The UI Designer's view onto its design surface: pan/zoom navigation, the
/// canvas tab's on-screen extent and the picture composed for it this frame.
/// View state, not document data -- nothing here is saved with the document.
///
/// Coordinates: "view" is canvas-tab-local logical pixels, "canvas" is the
/// preview tree's logical pixels. view = canvas * zoom + pan.
struct EditorUICanvasView
{
    static constexpr float kMinZoom = 0.1f;
    static constexpr float kMaxZoom = 16.0f;

    glm::vec2                      pan    = {0.0f, 0.0f};
    float                          zoom   = 1.0f;
    /// Canvas tab image size in tree-logical pixels. The compositor sizes the
    /// canvas target from it.
    glm::vec2                      extent = {0.0f, 0.0f};
    /// The picture the compositor recorded this frame; null while no canvas tab
    /// is shown.
    std::shared_ptr<RenderTexture> image;
    /// How many canvas tabs are on screen. The compositor records only while
    /// this is nonzero; a count because each editor window may host one.
    uint32_t                       shownCount = 0;
    /// Set when a document opens or the design size changes. The canvas tab
    /// fits once it knows its on-screen extent (it may not be laid out yet).
    bool                           bFitPending = false;

    [[nodiscard]] bool isShown() const { return shownCount > 0; }

    void setZoom(float value) { zoom = std::clamp(value, kMinZoom, kMaxZoom); }

    /// Zoom so `content` (canvas px) fits the view with `margin` view px
    /// around it, centred. False while the view has no extent.
    bool fitTo(const glm::vec2& content, float margin = 24.0f)
    {
        const glm::vec2 room = extent - glm::vec2(2.0f * margin);
        if (room.x <= 0.0f || room.y <= 0.0f || content.x <= 0.0f || content.y <= 0.0f) {
            return false;
        }
        setZoom(std::min(room.x / content.x, room.y / content.y));
        pan = (extent - content * zoom) * 0.5f;
        return true;
    }

    /// Zoom by `factor` keeping the canvas point under `viewPoint` in place.
    void zoomAt(const glm::vec2& viewPoint, float factor)
    {
        const float before = zoom;
        setZoom(zoom * factor);
        pan = viewPoint - (viewPoint - pan) * (zoom / before);
    }

    [[nodiscard]] glm::vec2 viewToCanvas(const glm::vec2& viewPoint) const { return (viewPoint - pan) / zoom; }
    [[nodiscard]] glm::vec2 canvasToView(const glm::vec2& canvasPoint) const { return canvasPoint * zoom + pan; }
};

} // namespace ya
