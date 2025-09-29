#pragma once

#include "Core/Base.h"
#include "Core/Log.h"
#include "RenderDefines.h"
#include "reflect.cc/enum"

namespace ya
{


struct IRender
{
    RenderCreateInfo _ci;
    ERenderAPI::T    _renderAPI = ERenderAPI::None;

    IRender() = default;
    virtual ~IRender() { YA_CORE_TRACE("IRender::~IRender()"); }

    static IRender *create(const RenderCreateInfo &ci);

    virtual bool init(const RenderCreateInfo &ci)
    {
        YA_CORE_TRACE("IRender::init()");
        _ci = ci;
        return true;
    }
    virtual void destroy() = 0;

    virtual bool begin(int32_t *imageIndex)                                  = 0;
    virtual bool end(int32_t imageIndex, std::vector<void *> CommandBuffers) = 0;

    [[nodiscard]] ERenderAPI::T getAPI() const { return _renderAPI; }
};

} // namespace ya
