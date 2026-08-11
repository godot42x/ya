#pragma once

#include "Core/Api.h"

namespace ya
{

class YA_APP_RUNTIME_API AppBootstrap
{
  public:
    static void initializeProcessCore();
    static void initializeVirtualFileSystem();

  private:
    static void configureBundledGraphicsRuntimeEnv();
};

} // namespace ya
