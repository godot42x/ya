#pragma once

#include "App/Kernel/AppKernel.h"

namespace ya
{

class HostSdlEventSource final : public IAppEventSource
{
  public:
    void pollEvents(const std::function<void(const Event&)>& emit) override;
};

} // namespace ya
