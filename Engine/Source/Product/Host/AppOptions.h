#pragma once

#include "Core/Api.h"
#include "Core/Log.h"
#include "Core/Profiling/Profiling.h"
#include "AppServices/AppAutomation.h"
#include "Host/Utility/ClLIParams.h"
#include "AppServices/PostProcessingState.h"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <vector>

namespace ya
{

enum class EAutomationScreenshotTarget : uint8_t
{
    Viewport = 0,
    Presentation,
};

enum class EAutomationRenderPipeline : uint8_t
{
    Forward = 0,
    Deferred,
};

struct AppAutomationViewportResize
{
    uint32_t width      = 0;
    uint32_t height     = 0;
    uint64_t frameIndex = 1;
};

struct AppAutomationPipelineSwitch
{
    EAutomationRenderPipeline target = EAutomationRenderPipeline::Deferred;
    uint64_t                  frameIndex = 1;
};

struct AppAutomationDeferredOverrides
{
    std::optional<bool> ssaoEnabled;
};

struct AppAutomationPostProcessOverrides
{
    std::optional<bool>                                  enabled;
    std::optional<bool>                                  bloomEnabled;
    std::optional<bool>                                  toneMappingEnabled;
    std::optional<PostProcessingState::EToneMappingCurve> toneMappingCurve;
};

struct AppProfilingOptions
{
    bool                       bCpuProfileEnabled            = profiling::isProfileBuild();
    bool                       bCpuProfileOverridden         = false;
    bool                       bCpuProfileOutputOverridden   = false;
    bool                       bProfileSessionNameOverridden = false;
    std::string                profileSessionName            = "App";
    std::optional<std::string> cpuProfileOutputPath;
};

struct AppAutomationOptions
{
    uint64_t                     exitAfterFrame               = 0;
    uint64_t                     screenshotFrameIndex         = 0;
    uint64_t                     screenshotWarmupFrames       = 30;
    uint64_t                     screenshotSettleFrames       = 5;
    bool                         renderDocCapture             = false;
    bool                         bRenderDocCaptureOverridden  = false;
    bool                         bScreenshotTargetOverridden  = false;
    EAutomationScreenshotTarget  screenshotTarget             = EAutomationScreenshotTarget::Viewport;
    std::optional<std::string>   configPath;
    std::optional<std::string>   scenePath;
    std::optional<std::string>   screenshotPath;
    std::optional<glm::vec3>     editorCameraPosition;
    std::optional<glm::vec3>     editorCameraRotation;
    std::optional<AppAutomationViewportResize> viewportResize;
    std::optional<AppAutomationPipelineSwitch> pipelineSwitch;
    std::optional<logcc::LogLevel::T> logLevel;
    std::optional<logcc::LogLevel::T> logDetailLevel;
    AppAutomationShadowOverrides shadow;
    AppAutomationDeferredOverrides deferred;
    AppAutomationPostProcessOverrides postprocess;
    uint16_t                         controlPort = 0;
};

[[nodiscard]] bool tryParseAutomationScreenshotTarget(const std::string& text,
                                                      EAutomationScreenshotTarget& outValue);
[[nodiscard]] bool tryParseAutomationVec3(const std::string& text, glm::vec3& outValue);
[[nodiscard]] bool tryParseAutomationRenderPipeline(const std::string& text,
                                                     EAutomationRenderPipeline& outValue);
[[nodiscard]] bool tryParseAutomationToneMappingCurve(const std::string& text,
                                                       PostProcessingState::EToneMappingCurve& outValue);
[[nodiscard]] bool tryParseLogLevel(const std::string& text, logcc::LogLevel::T& outValue);

struct YA_HOST_API AppDesc
{
    CliParams params = CliParams("Yet Another Game Engine", "Command line options");

    std::string          title      = "Yet Another Game Engine";
    int                  width      = 1024;
    int                  height     = 768;
    bool                 fullscreen = false;
    AppProfilingOptions  profiling;
    AppAutomationOptions automation;

    std::optional<std::string> projectPath;
    std::optional<std::string> defaultScenePath;
    std::optional<std::string> projectRoot;
    std::optional<std::string> executablePath;
    bool                       bEditor = false;

    bool                     bEnableRenderDoc           = false;
    bool                     bRenderDocOutputOverridden = false;
    std::string              renderDocDllPath           = "C:/Program Files/RenderDoc/renderdoc.dll";
    std::string              renderDocCaptureOutputDir  = "Engine/Saved/RenderDoc";
    std::vector<std::string> disabledGraphicsCards;

    void init(int argc, char** argv);
};

} // namespace ya

