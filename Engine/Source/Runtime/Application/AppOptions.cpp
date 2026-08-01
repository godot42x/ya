#include "Runtime/Application/AppOptions.h"

#include "Core/Log.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace ya
{

namespace
{

std::string lowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
                   { return static_cast<char>(std::tolower(ch)); });
    return value;
}

} // namespace

bool tryParseAutomationScreenshotTarget(const std::string& text, EAutomationScreenshotTarget& outValue)
{
    const std::string normalized = lowerCopy(text);
    if (normalized == "viewport") {
        outValue = EAutomationScreenshotTarget::Viewport;
        return true;
    }
    if (normalized == "presentation") {
        outValue = EAutomationScreenshotTarget::Presentation;
        return true;
    }
    return false;
}

bool tryParseAutomationVec3(const std::string& text, glm::vec3& outValue)
{
    std::string normalized = text;
    for (char& ch : normalized) {
        if (ch == ',' || ch == ';') {
            ch = ' ';
        }
    }

    std::stringstream stream(normalized);
    float             x = 0.0f;
    float             y = 0.0f;
    float             z = 0.0f;
    if (!(stream >> x >> y >> z)) {
        return false;
    }

    stream >> std::ws;
    if (!stream.eof()) {
        return false;
    }

    outValue = glm::vec3(x, y, z);
    return true;
}

bool tryParseAutomationRenderPipeline(const std::string& text, EAutomationRenderPipeline& outValue)
{
    const std::string normalized = lowerCopy(text);
    if (normalized == "forward") {
        outValue = EAutomationRenderPipeline::Forward;
        return true;
    }
    if (normalized == "deferred") {
        outValue = EAutomationRenderPipeline::Deferred;
        return true;
    }
    return false;
}

bool tryParseAutomationToneMappingCurve(const std::string& text,
                                        PostProcessingState::EToneMappingCurve& outValue)
{
    const std::string normalized = lowerCopy(text);
    if (normalized == "aces") {
        outValue = PostProcessingState::EToneMappingCurve::ACES;
        return true;
    }
    if (normalized == "uncharted2" || normalized == "uncharted") {
        outValue = PostProcessingState::EToneMappingCurve::Uncharted2;
        return true;
    }
    return false;
}

bool tryParseLogLevel(const std::string& text, logcc::LogLevel::T& outValue)
{
    const std::string normalized = lowerCopy(text);
    if (normalized == "debug" || normalized == "d") {
        outValue = logcc::LogLevel::Debug;
        return true;
    }
    if (normalized == "trace" || normalized == "t") {
        outValue = logcc::LogLevel::Trace;
        return true;
    }
    if (normalized == "info" || normalized == "i") {
        outValue = logcc::LogLevel::Info;
        return true;
    }
    if (normalized == "warn" || normalized == "warning" || normalized == "w") {
        outValue = logcc::LogLevel::Warn;
        return true;
    }
    if (normalized == "error" || normalized == "e") {
        outValue = logcc::LogLevel::Error;
        return true;
    }
    if (normalized == "fatal" || normalized == "f") {
        outValue = logcc::LogLevel::Fatal;
        return true;
    }
    return false;
}

void AppDesc::init(int argc, char** argv)
{
    if (argc > 0 && argv && argv[0]) {
        executablePath = std::string(argv[0]);
    }

    params
        .opt<int>("w", {"width"}, "Window width")
        .opt<int>("h", {"height"}, "Window height")
        .opt<bool>("f", {"fullscreen"}, "Fullscreen mode", "false")
        .opt<uint64_t>("", {"exit-after-frame"}, "Quit gracefully after rendering N frames", "0")
        .opt<std::string>("", {"automation-config"}, "Automation override settings file path")
        .opt<std::string>("", {"scene"}, "Startup scene path override")
        .opt<std::string>("", {"project"}, "Project descriptor path")
        .opt<std::string>("", {"ya-project"}, "Project descriptor path (XMake-safe alias)")
        .opt<bool>("", {"editor"}, "Enable the Editor module", "false")
        .opt<std::string>("", {"screenshot"}, "Automation screenshot output PNG path")
        .opt<std::string>("", {"screenshot-target"}, "Automation screenshot target: viewport or presentation")
        .opt<uint64_t>("", {"screenshot-frame"}, "Earliest frame index allowed to request automation screenshot", "0")
        .opt<bool>("", {"cpu-profile"}, "Enable runtime CPU trace profiling",
                   !profiling::isCompiledOut() ? "true" : "false")
        .opt<std::string>("", {"cpu-profile-output"}, "Runtime CPU trace output path")
        .opt<std::string>("", {"profile-session"}, "Runtime profile session name")
        .opt<uint64_t>("", {"screenshot-warmup-frames"}, "Frames to wait before checking screenshot stability", "30")
        .opt<uint64_t>("", {"screenshot-settle-frames"}, "Consecutive stable frames required before screenshot", "5")
        .opt<bool>("", {"renderdoc-capture"}, "Automation trigger one RenderDoc frame capture after warmup and settle", "false")
        .opt<std::string>("", {"editor-camera-pos"}, "Editor camera position override as x,y,z")
        .opt<std::string>("", {"editor-camera-rot"}, "Editor camera rotation override as pitch,yaw,roll")
        .opt<std::string>("", {"log-level"}, "Runtime log level: debug/trace/info/warn/error/fatal")
        .opt<std::string>("", {"log-detail-level"}, "Runtime source-detail log level: debug/trace/info/warn/error/fatal")
        .opt<std::string>("", {"renderdoc-dll"}, "RenderDoc dll path", renderDocDllPath)
        .opt<std::string>("", {"renderdoc-output"}, "RenderDoc capture output directory", renderDocCaptureOutputDir)
        .opt<uint16_t>("", {"automation-control-port"}, "Automation control TCP port; 0 disables the server", "0")
        .parse(argc, argv);

    title = params._opt.program();
    params.tryGet<int>("width", width);
    params.tryGet<int>("height", height);
    params.tryGet<bool>("fullscreen", fullscreen);
    params.tryGet<uint64_t>("exit-after-frame", automation.exitAfterFrame);
    params.tryGet<uint16_t>("automation-control-port", automation.controlPort);
    if (std::string automationConfigPath; params.tryGet<std::string>("automation-config", automationConfigPath)) {
        automation.configPath = std::move(automationConfigPath);
    }
    params.tryGet<uint64_t>("screenshot-frame", automation.screenshotFrameIndex);
    params.tryGet<uint64_t>("screenshot-warmup-frames", automation.screenshotWarmupFrames);
    params.tryGet<uint64_t>("screenshot-settle-frames", automation.screenshotSettleFrames);
    if (bool renderDocCapture; params.tryGet<bool>("renderdoc-capture", renderDocCapture)) {
        automation.renderDocCapture             = renderDocCapture;
        automation.bRenderDocCaptureOverridden = true;
    }
    if (bool cpuProfileEnabled; params.tryGet<bool>("cpu-profile", cpuProfileEnabled)) {
        profiling.bCpuProfileEnabled    = cpuProfileEnabled;
        profiling.bCpuProfileOverridden = true;
    }
    if (std::string cpuProfileOutputPath; params.tryGet<std::string>("cpu-profile-output", cpuProfileOutputPath)) {
        profiling.cpuProfileOutputPath        = std::move(cpuProfileOutputPath);
        profiling.bCpuProfileOutputOverridden = true;
    }
    if (std::string profileSessionName; params.tryGet<std::string>("profile-session", profileSessionName)) {
        if (!profileSessionName.empty()) {
            profiling.profileSessionName             = std::move(profileSessionName);
            profiling.bProfileSessionNameOverridden = true;
        }
    }
    if (std::string scenePath; params.tryGet<std::string>("scene", scenePath)) {
        automation.scenePath = std::move(scenePath);
    }
    if (std::string project; params.tryGet<std::string>("project", project)) {
        projectPath = std::move(project);
    }
    if (std::string project; params.tryGet<std::string>("ya-project", project)) {
        projectPath = std::move(project);
    }
    params.tryGet<bool>("editor", bEditor);
    if (std::string screenshotPath; params.tryGet<std::string>("screenshot", screenshotPath)) {
        automation.screenshotPath = std::move(screenshotPath);
    }
    if (std::string cameraPos; params.tryGet<std::string>("editor-camera-pos", cameraPos)) {
        glm::vec3 parsedPosition{0.0f};
        if (tryParseAutomationVec3(cameraPos, parsedPosition)) {
            automation.editorCameraPosition = parsedPosition;
        }
        else {
            YA_CORE_WARN("Ignoring invalid editor camera position override: {}", cameraPos);
        }
    }
    if (std::string cameraRot; params.tryGet<std::string>("editor-camera-rot", cameraRot)) {
        glm::vec3 parsedRotation{0.0f};
        if (tryParseAutomationVec3(cameraRot, parsedRotation)) {
            automation.editorCameraRotation = parsedRotation;
        }
        else {
            YA_CORE_WARN("Ignoring invalid editor camera rotation override: {}", cameraRot);
        }
    }
    if (std::string screenshotTargetText; params.tryGet<std::string>("screenshot-target", screenshotTargetText)) {
        EAutomationScreenshotTarget screenshotTarget = EAutomationScreenshotTarget::Viewport;
        if (tryParseAutomationScreenshotTarget(screenshotTargetText, screenshotTarget)) {
            automation.screenshotTarget            = screenshotTarget;
            automation.bScreenshotTargetOverridden = true;
        }
        else {
            YA_CORE_WARN("Ignoring invalid automation screenshot target: {}", screenshotTargetText);
        }
    }
    if (std::string logLevelText; params.tryGet<std::string>("log-level", logLevelText)) {
        logcc::LogLevel::T parsedLogLevel = logcc::LogLevel::Info;
        if (tryParseLogLevel(logLevelText, parsedLogLevel)) {
            automation.logLevel = parsedLogLevel;
        }
    }
    if (std::string logDetailLevelText; params.tryGet<std::string>("log-detail-level", logDetailLevelText)) {
        logcc::LogLevel::T parsedLogDetailLevel = logcc::LogLevel::Warn;
        if (tryParseLogLevel(logDetailLevelText, parsedLogDetailLevel)) {
            automation.logDetailLevel = parsedLogDetailLevel;
        }
    }
    YA_CORE_INFO(FUNCTION_SIG);
    params.tryGet<std::string>("renderdoc-dll", renderDocDllPath);
    if (std::string renderDocOutputPath; params.tryGet<std::string>("renderdoc-output", renderDocOutputPath)) {
        renderDocCaptureOutputDir  = std::move(renderDocOutputPath);
        bRenderDocOutputOverridden = true;
    }
    if (automation.renderDocCapture) {
        bEnableRenderDoc = true;
    }
}

} // namespace ya

