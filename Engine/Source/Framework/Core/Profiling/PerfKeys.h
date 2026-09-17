#pragma once

#include "Core/FName.h"

namespace ya::perf
{

namespace domain
{

inline const FName& render()
{
    using namespace ya::literals;
    static const FName key = "Render"_name;
    return key;
}

inline const FName& game()
{
    using namespace ya::literals;
    static const FName key = "Game"_name;
    return key;
}

inline const FName& physics()
{
    using namespace ya::literals;
    static const FName key = "Physics"_name;
    return key;
}

inline const FName& gpu()
{
    using namespace ya::literals;
    static const FName key = "GPU"_name;
    return key;
}

} // namespace domain

namespace metric
{

inline const FName& cpuTimeMs()
{
    using namespace ya::literals;
    static const FName key = "cpu.time.ms"_name;
    return key;
}

inline const FName& gpuTimeMs()
{
    using namespace ya::literals;
    static const FName key = "gpu.time.ms"_name;
    return key;
}

inline const FName& threadTimeMs()
{
    using namespace ya::literals;
    static const FName key = "thread.time.ms"_name;
    return key;
}

} // namespace metric

namespace sample
{

inline const FName& hostTick()
{
    using namespace ya::literals;
    static const FName key = "Tick/Total"_name;
    return key;
}

inline const FName& tickLogic()
{
    using namespace ya::literals;
    static const FName key = "Tick/Logic"_name;
    return key;
}

inline const FName& tickEventPump()
{
    using namespace ya::literals;
    static const FName key = "Tick/EventPump"_name;
    return key;
}

inline const FName& tickFpsControl()
{
    using namespace ya::literals;
    static const FName key = "Tick/FpsControl"_name;
    return key;
}

inline const FName& tickRender()
{
    using namespace ya::literals;
    static const FName key = "Tick/Render"_name;
    return key;
}

inline const FName& tickMainThreadCallbacks()
{
    using namespace ya::literals;
    static const FName key = "Tick/MainThreadCallbacks"_name;
    return key;
}

inline const FName& tickAutomation()
{
    using namespace ya::literals;
    static const FName key = "Tick/Automation"_name;
    return key;
}

inline const FName& tickUnaccounted()
{
    using namespace ya::literals;
    static const FName key = "Tick/Unaccounted"_name;
    return key;
}

inline const FName& renderExtract()
{
    using namespace ya::literals;
    static const FName key = "Render/Extract"_name;
    return key;
}

inline const FName& renderRuntime()
{
    using namespace ya::literals;
    static const FName key = "Render/Runtime"_name;
    return key;
}

inline const FName& renderPrepareFrame()
{
    using namespace ya::literals;
    static const FName key = "Render/PrepareFrame"_name;
    return key;
}

inline const FName& renderWaitIdle()
{
    using namespace ya::literals;
    static const FName key = "Render/WaitIdle"_name;
    return key;
}

inline const FName& renderBegin()
{
    using namespace ya::literals;
    static const FName key = "Render/Begin"_name;
    return key;
}

inline const FName& renderWorld()
{
    using namespace ya::literals;
    static const FName key = "Render/World"_name;
    return key;
}

inline const FName& renderViewportOverlay()
{
    using namespace ya::literals;
    static const FName key = "Render/ViewportOverlay"_name;
    return key;
}

inline const FName& renderPostProcess()
{
    using namespace ya::literals;
    static const FName key = "Render/PostProcess"_name;
    return key;
}

inline const FName& renderPresentation()
{
    using namespace ya::literals;
    static const FName key = "Render/Presentation"_name;
    return key;
}

inline const FName& renderSubmit()
{
    using namespace ya::literals;
    static const FName key = "Render/Submit"_name;
    return key;
}

inline const FName& tickRenderCallbacks()
{
    using namespace ya::literals;
    static const FName key = "Tick/RenderCallbacks"_name;
    return key;
}

inline const FName& appEventRoute()
{
    using namespace ya::literals;
    static const FName key = "App/EventRoute"_name;
    return key;
}

inline const FName& appInputEvent()
{
    using namespace ya::literals;
    static const FName key = "App/InputEvent"_name;
    return key;
}

inline const FName& appUiEvent()
{
    using namespace ya::literals;
    static const FName key = "App/UIEvent"_name;
    return key;
}

inline const FName& appFileWatcher()
{
    using namespace ya::literals;
    static const FName key = "App/FileWatcher"_name;
    return key;
}

inline const FName& vulkanWaitFence()
{
    using namespace ya::literals;
    static const FName key = "Vulkan/WaitFence"_name;
    return key;
}

inline const FName& vulkanAcquire()
{
    using namespace ya::literals;
    static const FName key = "Vulkan/Acquire"_name;
    return key;
}

inline const FName& vulkanPresent()
{
    using namespace ya::literals;
    static const FName key = "Vulkan/Present"_name;
    return key;
}

inline const FName& deferredTick()
{
    using namespace ya::literals;
    static const FName key = "Deferred/Tick"_name;
    return key;
}

inline const FName& deferredShadow()
{
    using namespace ya::literals;
    static const FName key = "Deferred/Shadow"_name;
    return key;
}

inline const FName& deferredGBuffer()
{
    using namespace ya::literals;
    static const FName key = "Deferred/GBuffer"_name;
    return key;
}

inline const FName& deferredDepthCopy()
{
    using namespace ya::literals;
    static const FName key = "Deferred/DepthCopy"_name;
    return key;
}

inline const FName& deferredLight()
{
    using namespace ya::literals;
    static const FName key = "Deferred/Light"_name;
    return key;
}

inline const FName& deferredOverlay()
{
    using namespace ya::literals;
    static const FName key = "Deferred/Overlay"_name;
    return key;
}

inline const FName& shadowDirectional()
{
    using namespace ya::literals;
    static const FName key = "Shadow/Directional"_name;
    return key;
}

inline const FName& shadowPoint()
{
    using namespace ya::literals;
    static const FName key = "Shadow/Point"_name;
    return key;
}

inline const FName& shadowPointCull()
{
    using namespace ya::literals;
    static const FName key = "Shadow/Point/Cull"_name;
    return key;
}

inline const FName& shadowPointFaceLoop()
{
    using namespace ya::literals;
    static const FName key = "Shadow/Point/FaceLoop"_name;
    return key;
}

inline const FName& shadowPointFaceDirect()
{
    using namespace ya::literals;
    static const FName key = "Shadow/Point/FaceDirect"_name;
    return key;
}

inline const FName& shadowPointFaceSkinned()
{
    using namespace ya::literals;
    static const FName key = "Shadow/Point/FaceSkinned"_name;
    return key;
}

inline const FName& shadowPointDirectDrawStatic()
{
    using namespace ya::literals;
    static const FName key = "Shadow/Point/DirectDrawStatic"_name;
    return key;
}

inline const FName& deferredLightPrepare()
{
    using namespace ya::literals;
    static const FName key = "Deferred/Light/Prepare"_name;
    return key;
}

inline const FName& deferredLightExecute()
{
    using namespace ya::literals;
    static const FName key = "Deferred/Light/Execute"_name;
    return key;
}

} // namespace sample

} // namespace ya::perf
