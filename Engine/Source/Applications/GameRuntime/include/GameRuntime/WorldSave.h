#pragma once

#include "Core/Api.h"

#include <optional>
#include <string>

namespace ya
{

/// One save (G2.2). The authored scene stays the scene file. `persist` is the
/// `Persist` table after each loaded script's `onSave`. The scene's entities
/// are not walked.
struct YA_GAME_RUNTIME_API FWorldSave
{
    int         version = 1;
    std::string scene;
    std::string persist = "{}";
};

[[nodiscard]] YA_GAME_RUNTIME_API FWorldSave captureWorldSave(std::string scenePath, std::string persistJson);

[[nodiscard]] YA_GAME_RUNTIME_API bool writeWorldSave(const std::string& path, const FWorldSave& save);

[[nodiscard]] YA_GAME_RUNTIME_API std::optional<FWorldSave> readWorldSave(const std::string& path);

} // namespace ya
