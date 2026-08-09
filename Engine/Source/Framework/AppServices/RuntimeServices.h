#pragma once

#include "AppServices/ShadowSettings.h"
#include "AppServices/AppAutomation.h"
#include "RHI/Core/OffscreenJob.h"
#include "RHI/WindowProvider.h"

#include <string>

namespace ya
{

struct IWindowProvider;
struct ICommandBuffer;
struct OffscreenJobState;

/// Narrow host-service contract injected into framework modules. The Host
/// implements and registers it; framework code never locates App through
/// globals. Kept deliberately small: one responsibility per accessor.
struct IRenderRuntimeHostServices
{
    virtual ~IRenderRuntimeHostServices() = default;

    // Frame timing
    virtual uint32_t getFrameIndex() const  = 0;
    virtual uint64_t getElapsedTimeMS() const = 0;

    // Main presentation window (the backend surface source).
    virtual IWindowProvider* getMainWindowProvider() = 0;
    // Ensure a main window exists (creates one with the given info when the
    // host has none yet, e.g. headless test bootstrap) and return it.
    virtual IWindowProvider* getOrCreateMainWindow(const WindowCreateInfo& ci) = 0;

    // Authoritative shadow configuration + automation overrides.
    virtual ShadowSettings*                        getShadowSettings() = 0;
    virtual const AppAutomationShadowOverrides*  getAutomationShadowOverrides() const = 0;

    // World/render stopped state (paused frame loop).
    virtual bool isStopped() const = 0;

    // Offscreen GPU job enqueue sink (Host implements against its task
    // manager); used by derived render processors.
    virtual OffscreenJobQueueService getOffscreenJobQueueService() = 0;
};

/// Narrow offscreen-task scheduler contract (implemented by the Host task
/// manager); drives queued GPU jobs from the render frame tick.
struct IOffscreenTaskScheduler
{
    virtual ~IOffscreenTaskScheduler() = default;
    virtual bool hasOffscreenTasks() const = 0;
    virtual void updateOffscreenTasks(ICommandBuffer* cmdBuf,
                                      std::vector<std::shared_ptr<OffscreenJobState>>* submittedJobs) = 0;
};

/// Service assembly point: the Host registers the render-runtime host
/// services once at startup; framework modules query them by contract.
/// This is the only cross-module service registry (narrow contracts only).
struct RuntimeServices
{
    static void setRenderRuntimeHost(IRenderRuntimeHostServices* services);
    [[nodiscard]] static IRenderRuntimeHostServices* getRenderRuntimeHost();
};

} // namespace ya
