#pragma once

#include "Core/Common/Types.h"
#include "Render/RenderFrameData.h"

#include <string>

namespace ya
{

struct IRender;
struct ICommandBuffer;

/// Maximum number of frames that can be in-flight simultaneously.
/// UBOs and descriptor sets that change per-frame must be allocated in arrays of this size.
constexpr uint32_t MAX_FLIGHTS_IN_FLIGHT = 2;

/// Context passed to every RenderStage each frame.
struct RenderStageContext
{
    ICommandBuffer*        cmdBuf         = nullptr;
    const RenderFrameData* frameData      = nullptr;
    uint32_t               flightIndex    = 0;
    float                  deltaTime      = 0.0f;
    Extent2D               viewportExtent = {};
};

/// Base class for a render stage — a logical phase in the rendering pipeline.
///
/// A stage may own one or more graphics pipelines (multi-pipeline stage).
/// Per-flight resources (UBOs, descriptor sets) are managed by derived classes
/// as std::array<..., MAX_FLIGHTS_IN_FLIGHT>.
///
/// Naming: "RenderStage" instead of "RenderPass" to avoid confusion with VkRenderPass.
/// When a Render Graph is introduced later, its nodes can be called RGPass.
struct IRenderStage
{
    std::string _label;
    bool        bEnabled = true;

    explicit IRenderStage(std::string label) : _label(std::move(label)) {}
    virtual ~IRenderStage() = default;

    IRenderStage(const IRenderStage&)            = delete;
    IRenderStage& operator=(const IRenderStage&) = delete;
    IRenderStage(IRenderStage&&)                 = default;
    IRenderStage& operator=(IRenderStage&&)      = default;

    /// Initialize GPU resources (pipelines, descriptor sets, UBOs).
    virtual void init(IRender* render) = 0;

    /// Release all GPU resources.
    virtual void destroy() = 0;

    /// Upload UBOs, flush material dirty flags, update descriptor sets for this frame.
    /// Called once per frame before execute().
    virtual void prepare(const RenderStageContext& ctx) {}

    /// Record draw commands into the command buffer.
    virtual void execute(const RenderStageContext& ctx) = 0;

    /// Render ImGui debug UI for this stage.
    virtual void renderGUI() {}

    [[nodiscard]] const std::string& getLabel() const { return _label; }
};

} // namespace ya
