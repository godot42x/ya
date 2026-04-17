#pragma once

#include "Render/Core/Texture.h"

#include <functional>
#include <memory>
#include <string>

namespace ya
{

struct IRender;
struct ICommandBuffer;

struct OffscreenJobResult
{
    stdptr<Texture> outputTexture = nullptr;
};

struct OffscreenJobState
{
    using CreateOutputFn = std::function<stdptr<Texture>(IRender* render)>;
    using ExecuteFn      = std::function<bool(ICommandBuffer* cmdBuf, Texture* output)>;

    std::string    debugName;
    CreateOutputFn createOutputFn;
    ExecuteFn      executeFn;

    std::shared_ptr<OffscreenJobResult> result = std::make_shared<OffscreenJobResult>();
    bool                                bTaskQueued    = false;
    bool                                bTaskFinished  = false;
    bool                                bTaskSucceeded = false;
    bool                                bCancelled     = false;

    [[nodiscard]] bool isReadyToQueue() const
    {
        return createOutputFn && executeFn && !bTaskQueued;
    }
};


} // namespace ya