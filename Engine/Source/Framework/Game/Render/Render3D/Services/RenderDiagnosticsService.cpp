#include "RenderDiagnosticsService.h"

#include "Host/App.h"

#include "Core/Async/TaskQueue.h"
#include "Core/Debug/RenderDocCapture.h"

#include "RHI/Backend/Vulkan/VulkanRender.h"

#include <SDL3/SDL.h>
#include <cstdlib>
#include <filesystem>
#include <format>

namespace ya
{

namespace
{

struct AutomationRenderDocSummaryResult
{
    bool        bSuccess = false;
    std::string capturePath;
    std::string passSummaryPath;
    std::string error;
};

std::string sanitizeCapturePath(std::string path)
{
    if (const size_t nullPos = path.find('\0'); nullPos != std::string::npos) {
        path.resize(nullPos);
    }
    return std::filesystem::path(path).generic_string();
}

std::string toProjectRelativePath(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto absPath = std::filesystem::absolute(path, ec);
    if (ec) {
        return path.generic_string();
    }

    const auto cwd = std::filesystem::current_path(ec);
    if (ec) {
        return absPath.generic_string();
    }

    const auto rel = std::filesystem::relative(absPath, cwd, ec);
    if (ec || rel.empty()) {
        return absPath.generic_string();
    }

    return rel.generic_string();
}

AutomationRenderDocSummaryResult buildAutomationRenderDocPassSummary(const std::string& capturePath)
{
    AutomationRenderDocSummaryResult result;
    const std::string normalizedCapturePath = sanitizeCapturePath(capturePath);
    if (normalizedCapturePath.empty()) {
        result.error = "capture path is empty";
        return result;
    }

    const auto capture = std::filesystem::absolute(std::filesystem::path(normalizedCapturePath));
    result.capturePath = toProjectRelativePath(capture);
    if (!std::filesystem::exists(capture)) {
        result.error = std::format("capture does not exist: {}", capture.generic_string());
        return result;
    }

    const auto scriptPath = std::filesystem::absolute(std::filesystem::path("Script/renderdoc/rdc_pass_summary.py"));
    if (!std::filesystem::exists(scriptPath)) {
        result.error = std::format("summary script not found: {}", scriptPath.generic_string());
        return result;
    }

    const auto summaryPath = capture.parent_path() / "pass_summary.json";
#if defined(_WIN32)
    const std::string command = std::format(R"(py -3 "{}" --input "{}" --output "{}")",
                                            scriptPath.generic_string(),
                                            capture.generic_string(),
                                            summaryPath.generic_string());
#else
    const std::string command = std::format(R"(python3 "{}" --input "{}" --output "{}")",
                                            scriptPath.generic_string(),
                                            capture.generic_string(),
                                            summaryPath.generic_string());
#endif
    const int exitCode = std::system(command.c_str());
    if (exitCode != 0) {
        result.error = std::format("summary script exited with code {}", exitCode);
        return result;
    }
    if (!std::filesystem::exists(summaryPath)) {
        result.error = std::format("summary output not found: {}", summaryPath.generic_string());
        return result;
    }

    result.bSuccess        = true;
    result.passSummaryPath = toProjectRelativePath(summaryPath);
    return result;
}

void openCaptureDirectoryInOS(const std::string& filePath)
{
    if (filePath.empty()) {
        YA_CORE_WARN("File path is empty, cannot open directory");
        return;
    }

    auto dir = std::filesystem::path(filePath).parent_path();
    if (dir.empty()) {
        YA_CORE_WARN("Directory path is empty for file: {}", filePath);
        return;
    }

    dir            = std::filesystem::absolute(dir);
    const auto url = std::format("file:///{}", dir.string());
    if (!SDL_OpenURL(url.c_str())) {
        YA_CORE_ERROR("Failed to open directory {}: {}", dir.string(), SDL_GetError());
    }
}

} // namespace

void RenderDiagnosticsService::init(IRender* render, const AppDesc& appDesc)
{
    _render = render;
    if (!appDesc.bEnableRenderDoc) {
        return;
    }

    _renderDoc.capture             = ya::makeShared<RenderDocCapture>();
    _renderDoc.configuredDllPath   = appDesc.renderDocDllPath;
    _renderDoc.configuredOutputDir = appDesc.renderDocCaptureOutputDir;
    if (!_renderDoc.capture->init(_renderDoc.configuredDllPath, _renderDoc.configuredOutputDir)) {
        YA_CORE_WARN("RenderDoc unavailable: failed to initialize with dll '{}'", _renderDoc.configuredDllPath);
    }
    _renderDoc.capture->setCaptureFinishedCallback([this](const RenderDocCapture::CaptureResult& result)
                                                   { handleCaptureFinished(result); });
    configureRenderContext();

    if (auto* swapchain = _render ? _render->getSwapchain() : nullptr) {
        swapchain->onRecreate.addLambda(
            this,
            [this](ISwapchain::DiffInfo old, ISwapchain::DiffInfo now, bool bImageRecreated)
            {
                (void)old;
                (void)now;
                (void)bImageRecreated;
                configureRenderContext();
            });
    }
}

void RenderDiagnosticsService::shutdown()
{
    if (_renderDoc.capture) {
        _renderDoc.capture->shutdown();
        _renderDoc.capture.reset();
    }
    _render = nullptr;
    _renderDoc = {};
}

void RenderDiagnosticsService::onFrameBegin()
{
    if (_renderDoc.capture) {
        _renderDoc.capture->onFrameBegin();
    }
}

void RenderDiagnosticsService::onFrameEnd()
{
    if (_renderDoc.capture) {
        _renderDoc.capture->onFrameEnd();
    }
}

bool RenderDiagnosticsService::requestAutomationRenderDocCapture()
{
    _renderDoc.bAutomationCaptureFinished    = false;
    _renderDoc.bAutomationCaptureFailed      = false;
    _renderDoc.bAutomationPostProcessPending = false;
    _renderDoc.lastCapturePath.clear();
    _renderDoc.automationPassSummaryPath.clear();

    if (!_renderDoc.capture) {
        YA_CORE_WARN("Automation requested RenderDoc capture but RenderDoc integration is disabled");
        _renderDoc.bAutomationCaptureFailed = true;
        return false;
    }

    if (!_renderDoc.capture->isAvailable()) {
        YA_CORE_WARN("Automation requested RenderDoc capture but RenderDoc is unavailable: {}",
                     _renderDoc.configuredDllPath.empty() ? "renderdoc.dll" : _renderDoc.configuredDllPath);
        _renderDoc.bAutomationCaptureFailed = true;
        return false;
    }

    if (!_renderDoc.capture->isCaptureEnabled()) {
        YA_CORE_WARN("Automation requested RenderDoc capture but capture is disabled");
        _renderDoc.bAutomationCaptureFailed = true;
        return false;
    }

    if (_renderDoc.bAutomationCaptureRequested || _renderDoc.capture->isCapturing()) {
        return true;
    }

    _renderDoc.bAutomationCaptureRequested = true;
    _renderDoc.capture->requestNextFrame();
    YA_CORE_INFO("Automation queued a single RenderDoc frame capture");
    return true;
}

bool RenderDiagnosticsService::isAutomationRenderDocCapturePending() const
{
    return _renderDoc.bAutomationCaptureRequested ||
           (_renderDoc.capture && _renderDoc.capture->isCapturing()) ||
           _renderDoc.bAutomationPostProcessPending;
}

bool RenderDiagnosticsService::isAutomationRenderDocCaptureTerminal() const
{
    return _renderDoc.bAutomationCaptureFinished || _renderDoc.bAutomationCaptureFailed;
}

const std::string& RenderDiagnosticsService::getAutomationRenderDocCapturePath() const
{
    return _renderDoc.lastCapturePath;
}

const std::string& RenderDiagnosticsService::getAutomationRenderDocPassSummaryPath() const
{
    return _renderDoc.automationPassSummaryPath;
}

void RenderDiagnosticsService::configureRenderContext()
{
    if (!_render || !_renderDoc.capture) {
        return;
    }

    auto* vkRender = _render->as<VulkanRender>();
    if (!vkRender || !vkRender->getSwapchain()) {
        return;
    }

    _renderDoc.capture->setRenderContext({
        .device    = vkRender->getDevice(),
        .swapchain = vkRender->getSwapchain()->getHandle(),
    });
}

void RenderDiagnosticsService::handleCaptureFinished(const RenderDocCapture::CaptureResult& result)
{
    const bool bAutomationCapture = _renderDoc.bAutomationCaptureRequested;
    if (bAutomationCapture) {
        _renderDoc.bAutomationCaptureRequested = false;
        _renderDoc.lastCapturePath             = sanitizeCapturePath(result.capturePath);
    }

    if (!result.bSuccess) {
        if (bAutomationCapture) {
            _renderDoc.bAutomationCaptureFinished    = false;
            _renderDoc.bAutomationCaptureFailed      = true;
            _renderDoc.bAutomationPostProcessPending = false;
            _renderDoc.automationPassSummaryPath.clear();
            YA_CORE_WARN("Automation RenderDoc capture failed");
        }
        return;
    }

    _renderDoc.lastCapturePath = sanitizeCapturePath(result.capturePath);
    if (bAutomationCapture) {
        YA_CORE_INFO("Automation RenderDoc capture finished: {}", _renderDoc.lastCapturePath);
        _renderDoc.onCaptureAction               = 0;
        _renderDoc.bAutomationCaptureFinished    = false;
        _renderDoc.bAutomationCaptureFailed      = false;
        _renderDoc.bAutomationPostProcessPending = true;
        _renderDoc.automationPassSummaryPath.clear();

        TaskQueue::get().submitWithCallback(
            [capturePath = _renderDoc.lastCapturePath]() {
                return buildAutomationRenderDocPassSummary(capturePath);
            },
            [this](AutomationRenderDocSummaryResult summaryResult)
            {
                _renderDoc.bAutomationPostProcessPending = false;
                _renderDoc.lastCapturePath               = std::move(summaryResult.capturePath);
                if (!summaryResult.bSuccess) {
                    _renderDoc.bAutomationCaptureFinished = false;
                    _renderDoc.bAutomationCaptureFailed   = true;
                    _renderDoc.automationPassSummaryPath.clear();
                    YA_CORE_WARN("Automation RenderDoc pass summary failed for {}: {}",
                                 _renderDoc.lastCapturePath,
                                 summaryResult.error);
                    return;
                }

                _renderDoc.automationPassSummaryPath  = std::move(summaryResult.passSummaryPath);
                _renderDoc.bAutomationCaptureFinished = true;
                _renderDoc.bAutomationCaptureFailed   = false;
                YA_CORE_INFO("Automation RenderDoc pass summary generated: {}",
                             _renderDoc.automationPassSummaryPath);
            });
        return;
    }

    switch (_renderDoc.onCaptureAction) {
    case 0:
    case 1:
        if (!_renderDoc.capture->launchReplayUI(true, nullptr)) {
            YA_CORE_WARN("RenderDoc: failed to launch replay UI");
        }
        break;
    case 2:
        openCaptureDirectoryInOS(result.capturePath);
        break;
    default:
        break;
    }
}

} // namespace ya
