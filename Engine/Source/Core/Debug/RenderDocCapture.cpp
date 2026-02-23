#include "RenderDocCapture.h"

#include "Core/Log.h"

#include <algorithm>
#include <filesystem>

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>

namespace
{
    #define RENDERDOC_CC __cdecl

enum RENDERDOC_Version
{
    eRENDERDOC_API_Version_1_6_0 = 10600,
    eRENDERDOC_API_Version_1_7_0 = 10700,
};

using RENDERDOC_DevicePointer = void*;
using RENDERDOC_WindowHandle  = void*;

using pRENDERDOC_GetAPI                     = int(RENDERDOC_CC*)(RENDERDOC_Version version, void** outAPIPointers);
using pRENDERDOC_GetAPIVersion              = void(RENDERDOC_CC*)(int* major, int* minor, int* patch);
using pRENDERDOC_TriggerCapture             = void(RENDERDOC_CC*)(void);
using pRENDERDOC_TriggerMultiFrameCapture   = void(RENDERDOC_CC*)(uint32_t numFrames);
using pRENDERDOC_StartFrameCapture          = void(RENDERDOC_CC*)(RENDERDOC_DevicePointer device, RENDERDOC_WindowHandle wndHandle);
using pRENDERDOC_IsFrameCapturing           = uint32_t(RENDERDOC_CC*)(void);
using pRENDERDOC_EndFrameCapture            = uint32_t(RENDERDOC_CC*)(RENDERDOC_DevicePointer device, RENDERDOC_WindowHandle wndHandle);
using pRENDERDOC_GetOverlayBits             = uint32_t(RENDERDOC_CC*)(void);
using pRENDERDOC_MaskOverlayBits            = void(RENDERDOC_CC*)(uint32_t andMask, uint32_t orMask);
using pRENDERDOC_GetNumCaptures             = uint32_t(RENDERDOC_CC*)(void);
using pRENDERDOC_GetCapture                 = uint32_t(RENDERDOC_CC*)(uint32_t idx, char* filename, uint32_t* pathlength, uint64_t* timestamp);
using pRENDERDOC_LaunchReplayUI             = uint32_t(RENDERDOC_CC*)(uint32_t connectTargetControl, const char* cmdline);
using pRENDERDOC_SetCaptureFilePathTemplate = void(RENDERDOC_CC*)(const char* pathTemplate);

constexpr uint32_t RENDERDOC_OVERLAY_DEFAULT = 0x0f;

struct RENDERDOC_API_1_6_0
{
    pRENDERDOC_GetAPIVersion GetAPIVersion;

    void* SetCaptureOptionU32;
    void* SetCaptureOptionF32;

    void* GetCaptureOptionU32;
    void* GetCaptureOptionF32;

    void* SetFocusToggleKeys;
    void* SetCaptureKeys;

    pRENDERDOC_GetOverlayBits  GetOverlayBits;
    pRENDERDOC_MaskOverlayBits MaskOverlayBits;

    void* RemoveHooks;
    void* UnloadCrashHandler;

    pRENDERDOC_SetCaptureFilePathTemplate SetCaptureFilePathTemplate;
    void*                                 GetCaptureFilePathTemplate;

    pRENDERDOC_GetNumCaptures GetNumCaptures;
    pRENDERDOC_GetCapture     GetCapture;

    pRENDERDOC_TriggerCapture TriggerCapture;

    void*                     IsTargetControlConnected;
    pRENDERDOC_LaunchReplayUI LaunchReplayUI;

    void* SetActiveWindow;

    pRENDERDOC_StartFrameCapture StartFrameCapture;
    pRENDERDOC_IsFrameCapturing  IsFrameCapturing;
    pRENDERDOC_EndFrameCapture   EndFrameCapture;

    pRENDERDOC_TriggerMultiFrameCapture TriggerMultiFrameCapture;

    void* SetCaptureFileComments;

    void* DiscardFrameCapture;

    void* ShowReplayUI;

    void* SetCaptureTitle;
};

} // namespace
#endif

namespace ya
{

bool RenderDocCapture::init(const std::string& dllPath, const std::string& captureOutputDir)
{
#if defined(_WIN32)
    if (_available) {
        if (!captureOutputDir.empty()) {
            auto            dir = std::filesystem::path(captureOutputDir);
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);

            auto templatePath = (dir / "capture").string();
            std::ranges::replace(templatePath, '\\', '/');

            auto* rdoc = reinterpret_cast<RENDERDOC_API_1_6_0*>(_api);
            if (rdoc && rdoc->SetCaptureFilePathTemplate) {
                rdoc->SetCaptureFilePathTemplate(templatePath.c_str());
                _captureOutputDir = dir.string();
            }
        }
        return true;
    }

    const std::string configuredDll = dllPath.empty() ? "renderdoc.dll" : dllPath;

    HMODULE module = GetModuleHandleA(configuredDll.c_str());
    if (!module) {
        module = LoadLibraryA(configuredDll.c_str());
    }
    if (!module && configuredDll != "renderdoc.dll") {
        YA_CORE_WARN("RenderDoc: failed to load '{}', fallback to renderdoc.dll", configuredDll);
        module = GetModuleHandleA("renderdoc.dll");
        if (!module) {
            module = LoadLibraryA("renderdoc.dll");
        }
    }
    if (!module) {
        return false;
    }

    auto getAPI = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI"));
    if (!getAPI) {
        return false;
    }

    void* api = nullptr;
    if (!getAPI(eRENDERDOC_API_Version_1_7_0, &api)) {
        if (!getAPI(eRENDERDOC_API_Version_1_6_0, &api)) {
            return false;
        }
    }

    auto* rdoc = reinterpret_cast<RENDERDOC_API_1_6_0*>(api);
    if (!rdoc || !rdoc->StartFrameCapture || !rdoc->EndFrameCapture) {
        return false;
    }

    _module    = module;
    _api       = api;
    _available = true;

    int major = 0, minor = 0, patch = 0;
    if (rdoc->GetAPIVersion) {
        rdoc->GetAPIVersion(&major, &minor, &patch);
    }
    YA_CORE_INFO("RenderDoc attached: API {}.{}.{}", major, minor, patch);

    if (!captureOutputDir.empty()) {
        auto            dir = std::filesystem::path(captureOutputDir);
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);

        auto templatePath = (dir / "capture").string();
        std::replace(templatePath.begin(), templatePath.end(), '\\', '/');

        if (rdoc->SetCaptureFilePathTemplate) {
            rdoc->SetCaptureFilePathTemplate(templatePath.c_str());
            _captureOutputDir = dir.string();
            YA_CORE_INFO("RenderDoc capture output dir: {}", _captureOutputDir);
        }
    }

    setHUDVisible(_hudVisible);

    return true;
#else
    return false;
#endif
}

void RenderDocCapture::shutdown()
{
    _captureQueued = false;
    _delayFrames   = 0;
    _capturing     = false;
    _available     = false;
    _api           = nullptr;
    _module        = nullptr;
    _activeCaptureContext = {};
    _captureOutputDir.clear();
    _lastCapturePath.clear();
}

void RenderDocCapture::requestNextFrame()
{
    if (!_available || !_captureEnabled) {
        return;
    }
    _delayFrames   = 0;
    _captureQueued = true;
    YA_CORE_INFO("RenderDoc: capture queued for next frame");
}

void RenderDocCapture::requestAfterFrames(uint32_t frames)
{
    if (!_available || !_captureEnabled) {
        return;
    }
    _delayFrames   = frames;
    _captureQueued = false;
    YA_CORE_INFO("RenderDoc: capture queued after {} frames", frames);
}

void RenderDocCapture::onFrameBegin()
{
#if defined(_WIN32)
    if (!_available || !_api) {
        return;
    }

    if (!_captureEnabled) {
        _captureQueued = false;
        _delayFrames   = 0;
        return;
    }

    auto* rdoc = reinterpret_cast<RENDERDOC_API_1_6_0*>(_api);

    if (_delayFrames > 0) {
        --_delayFrames;
        if (_delayFrames == 0) {
            _captureQueued = true;
        }
    }

    if (_captureQueued && !_capturing) {
        RenderContext beginCtx = {
            .device    = _renderContext.device,
            .swapchain = nullptr,
        };

        rdoc->StartFrameCapture(beginCtx.device, beginCtx.swapchain);

        bool bStarted = true;
        if (rdoc->IsFrameCapturing) {
            bStarted = (rdoc->IsFrameCapturing() != 0);
        }

        if (!bStarted && (_renderContext.device != nullptr || _renderContext.swapchain != nullptr)) {
            YA_CORE_WARN("RenderDoc: capture did not start with explicit context, retrying with global context");
            beginCtx = {
                .device    = nullptr,
                .swapchain = nullptr,
            };
            rdoc->StartFrameCapture(beginCtx.device, beginCtx.swapchain);
            if (rdoc->IsFrameCapturing) {
                bStarted = (rdoc->IsFrameCapturing() != 0);
            }
            else {
                bStarted = true;
            }
        }

        _captureQueued = false;
        _capturing     = bStarted;
        if (_capturing) {
            _activeCaptureContext = beginCtx;
            YA_CORE_INFO("RenderDoc: StartFrameCapture");
        }
        else {
            YA_CORE_WARN("RenderDoc: StartFrameCapture rejected");
        }
    }
#endif
}

void RenderDocCapture::setCaptureEnabled(bool enabled)
{
    _captureEnabled = enabled;
    if (!enabled) {
        _captureQueued = false;
        _delayFrames   = 0;
    }
}

void RenderDocCapture::setHUDVisible(bool visible)
{
#if defined(_WIN32)
    _hudVisible = visible;
    if (!_available || !_api) {
        return;
    }

    auto* rdoc = reinterpret_cast<RENDERDOC_API_1_6_0*>(_api);
    if (!rdoc->MaskOverlayBits) {
        return;
    }

    if (visible) {
        rdoc->MaskOverlayBits(~0u, RENDERDOC_OVERLAY_DEFAULT);
    }
    else {
        rdoc->MaskOverlayBits(~0u, 0u);
    }
#else
    _hudVisible = visible;
#endif
}

void RenderDocCapture::onFrameEnd()
{
#if defined(_WIN32)
    if (!_available || !_api || !_capturing) {
        return;
    }

    auto* rdoc = reinterpret_cast<RENDERDOC_API_1_6_0*>(_api);

    CaptureResult result{};
    result.bSuccess = (rdoc->EndFrameCapture(_activeCaptureContext.device, _activeCaptureContext.swapchain) == 1);
    if (!result.bSuccess && (_activeCaptureContext.device != nullptr || _activeCaptureContext.swapchain != nullptr)) {
        YA_CORE_WARN("RenderDoc: EndFrameCapture failed with explicit context, retrying global context");
        result.bSuccess = (rdoc->EndFrameCapture(nullptr, nullptr) == 1);
    }
    _capturing      = false;
    _activeCaptureContext = {};

    if (result.bSuccess && rdoc->GetNumCaptures && rdoc->GetCapture) {
        uint32_t numCaptures = rdoc->GetNumCaptures();
        if (numCaptures > 0) {
            uint32_t pathLen = 0;
            uint64_t ts      = 0;
            uint32_t idx     = numCaptures - 1;
            if (rdoc->GetCapture(idx, nullptr, &pathLen, &ts) == 1 && pathLen > 0) {
                std::string capturePath(pathLen + 1, '\0');
                if (rdoc->GetCapture(idx, capturePath.data(), &pathLen, &ts) == 1) {
                    capturePath.resize(pathLen);
                    _lastCapturePath   = capturePath;
                    result.capturePath = capturePath;
                    result.timestamp   = ts;
                }
            }
        }
    }

    YA_CORE_INFO("RenderDoc: EndFrameCapture => {}", result.bSuccess ? "ok" : "failed");
    if (_onCaptureFinished) {
        _onCaptureFinished(result);
    }
#endif
}

bool RenderDocCapture::launchReplayUI(bool bConnectTargetControl, const char* cmdLine)
{
#if defined(_WIN32)
    if (!_available || !_api) {
        return false;
    }

    auto* rdoc = reinterpret_cast<RENDERDOC_API_1_6_0*>(_api);
    if (!rdoc->LaunchReplayUI) {
        return false;
    }

    uint32_t pid = rdoc->LaunchReplayUI(bConnectTargetControl ? 1u : 0u, cmdLine);
    return pid != 0;
#else
    return false;
#endif
}

} // namespace ya
