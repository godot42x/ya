#pragma once

namespace ya
{

struct DeferredRenderPipeline;
struct DeferredRenderTickDesc;
struct Scene;

struct IDeferredRenderPath
{
    virtual ~IDeferredRenderPath() = default;

    virtual const char* name() const = 0;
    virtual void        init(DeferredRenderPipeline& pipeline) = 0;
    virtual void        shutdown(DeferredRenderPipeline& pipeline) = 0;
    virtual void        beginFrame(DeferredRenderPipeline& pipeline) = 0;
    virtual void        tick(DeferredRenderPipeline& pipeline, const DeferredRenderTickDesc& desc, Scene* scene) = 0;
    virtual void        renderGUI(DeferredRenderPipeline& pipeline) = 0;
};

} // namespace ya
