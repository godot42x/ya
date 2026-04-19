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