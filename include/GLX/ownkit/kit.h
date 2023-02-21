#ifndef GLX_OWNKIT_KIT_H
#define GLX_OWNKIT_KIT_H

#include <string>

#include <glm/vec3.hpp>

namespace ownkit {

static constexpr float PI = 3.1415926;

void DrawCircle(glm::vec3 &&rgb, int steps, int radius);

} // namespace ownkit

#endif