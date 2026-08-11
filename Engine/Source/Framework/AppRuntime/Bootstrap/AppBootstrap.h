#pragma once

#include "Core/Api.h"

#include <optional>
#include <string>

namespace ya
{

class YA_APP_RUNTIME_API AppBootstrap
{
  public:
    static void initializeProcess(const std::optional<std::string>& gameRoot = std::nullopt);

  private:
    static void configureBundledGraphicsRuntimeEnv();
};

} // namespace ya

