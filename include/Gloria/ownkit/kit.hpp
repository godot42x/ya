#pragma once

#include <pch/gl.h>
#include <pch/std.h>


namespace ownkit {

static constexpr float PI = 3.1415926;

void DrawCircle(glm::vec3 &&rgb, int steps, int radius);

} // namespace ownkit



/**
 * @brief
 *  deperacted in 3.3 core profile mode
 * @param rgb
 * @param steps
 * @param radius
 * @return void
 */
void ownkit::DrawCircle(glm::vec3 &&rgb, int steps, int radius)
{
    const float angle = PI * 2 / steps;

    float oldX = 0, oldY = 1 * radius;

    for (int i = 0; i <= steps; ++i)
    {
        float newX = radius * std::cos(angle * i);
        float newY = -radius * std::sin(angle * i);

        glColor3f(rgb.x, rgb.y, rgb.z);

        glBegin(GL_TRIANGLES);
        glVertex3f(0, 0, 0);
        glVertex3f(oldX, oldY, 0);
        glVertex3f(newX, newY, 0);
        glEnd();

        oldX = newX;
        oldY = newY;
    }
}
