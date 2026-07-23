#pragma once

#include "Editor/Panels/RuntimeToolsPanel.h"

#include "Config/ConfigManager.h"
#include "Core/Camera/FreeCameraController.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/Profiling/Profiling.h"
#include "Editor/App/EditorLayer.h"
#include "Editor/EditorProfilingSettings.h"
#include "Editor/Panels/RenderTargetInspector.h"
#include "Render/2D/Render2D.h"
#include "Render/Core/Swapchain.h"
#include "Runtime/App/App.h"
#include "Runtime/App/Common/PostProcessingStage.h"
#include "Runtime/App/Common/Shadow/BasicShadowMap/BasicShadowMapTechnique.h"
#include "Runtime/App/Common/Shadow/Common/ShadowSettingsConfig.h"
#include "Runtime/App/Common/Shadow/ShadowStage.h"
#include "Runtime/App/DebugRenderSystem.h"
#include "Runtime/App/DeferredRender/DeferredRenderPipeline.h"
#include "Runtime/App/ForwardRender/ForwardRenderPipeline.h"
#include "Runtime/App/RenderRuntime.h"
#include "Runtime/App/Utility/FPSCtrl.h"

#include <algorithm>
#include <array>
#include <glm/gtc/type_ptr.hpp>
#include <string>

namespace ya
{

extern ClearValue colorClearValue;
extern ClearValue depthClearValue;

inline constexpr const char* kCullModeLabels       = "None\0Front\0Back\0FrontAndBack\0";
inline constexpr const char* kPolygonModeLabels    = "Fill\0Line\0Point\0";
inline constexpr const char* kCompareOpLabels      = "Never\0Less\0Equal\0LessOrEqual\0Greater\0NotEqual\0GreaterOrEqual\0Always\0";
inline constexpr const char* kPresentModeLabels    = "Immediate\0Mailbox\0FIFO\0FIFO Relaxed\0";
inline constexpr const char* kRenderPipelineLabels = "Forward\0Deferred\0";

inline DeferredRenderPipeline* getDeferredPipeline(App& app)
{
    auto* renderRuntime = app.getRenderRuntime();
    return renderRuntime ? dynamic_cast<DeferredRenderPipeline*>(renderRuntime->getActivePipeline()) : nullptr;
}

inline ForwardRenderPipeline* getForwardPipeline(App& app)
{
    auto* renderRuntime = app.getRenderRuntime();
    return renderRuntime ? dynamic_cast<ForwardRenderPipeline*>(renderRuntime->getActivePipeline()) : nullptr;
}

template <typename Fn>
inline void renderPerfTree(const char* label, float valueMs, Fn&& body)
{
    if (!ImGui::TreeNode(label, "%s  %.3f ms", label, valueMs)) {
        return;
    }
    body();
    ImGui::TreePop();
}

void renderPerfLeaf(const char* label, float valueMs, float parentMs = -1.0f);
void renderFrameStatsContent(const App& app, float dt);
void renderGraphicsPipelineInspector(const char* label, IGraphicsPipeline* pipeline);
void renderSessionContent(App& app);
void renderProfilingContent();
void renderPresentationSettings(App& app, RenderRuntime& runtime);
void renderForwardSettingsContent(App& app);
void renderDeferredSettingsContent(App& app);
void renderRenderingInternalsContent(App& app);
void renderEditorCameraContent(EditorLayer& layer, FreeCameraController& controller);
void renderClearValuesContent();
void renderRender2DDebugContent();
void renderDiagnosticsContent(App& app);
void renderDebugPrimitivesContent(App& app);

} // namespace ya
