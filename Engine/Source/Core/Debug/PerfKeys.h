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

inline const FName& renderFrame()
{
    using namespace ya::literals;
    static const FName key = "Render/Frame"_name;
    return key;
}

inline const FName& frameLogic()
{
    using namespace ya::literals;
    static const FName key = "Frame/Logic"_name;
    return key;
}

inline const FName& frameRender()
{
    using namespace ya::literals;
    static const FName key = "Frame/Render"_name;
    return key;
}

inline const FName& frameAutomation()
{
    using namespace ya::literals;
    static const FName key = "Frame/Automation"_name;
    return key;
}

inline const FName& frameUnaccounted()
{
    using namespace ya::literals;
    static const FName key = "Frame/Unaccounted"_name;
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

inline const FName& renderFlushCallbacks()
{
    using namespace ya::literals;
    static const FName key = "Render/FlushCallbacks"_name;
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