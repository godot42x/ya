
#include <glm/vec3.hpp>


class ownkit
{
  public:
    static constexpr float PI = 3.1415926;

    static void DrawCircle(glm::vec3 &&rgb, int steps, int radius);
}; // namespace ownkit
