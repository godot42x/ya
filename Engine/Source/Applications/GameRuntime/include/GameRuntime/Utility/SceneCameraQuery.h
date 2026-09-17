#pragma once

#include "Core/Base.h"

namespace ya
{

struct Scene;
struct Entity;

/// The camera a game view renders from: the first camera component flagged
/// primary, otherwise the first camera in the Scene. Null when it has none.
[[nodiscard]] YA_GAME_RUNTIME_API Entity* findPrimaryCamera(Scene& scene);

/// The first camera other than the given one -- the other camera a preview view
/// can show. Null when the Scene has only that camera.
[[nodiscard]] YA_GAME_RUNTIME_API Entity* findSecondaryCamera(Scene& scene, Entity* primaryCamera);

} // namespace ya
