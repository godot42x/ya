#pragma once

#include "Core/Api.h"

namespace ya
{

class YA_GUI_API AppBootstrap
{
  public:
    static void initializeProcessCore();
    static void initializeVirtualFileSystem();

  private:
    static void configureBundledGraphicsRuntimeEnv();
};

} // namespace ya
