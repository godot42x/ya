#include <GL/glew.h>
#include <math.h>
#include <ownkit/kit.h>

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
        float newX = radius * cos(angle * i);
        float newY = -radius * sin(angle * i);

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
