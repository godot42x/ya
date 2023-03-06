#pragma once

#include <functional>
#include <iostream>
#include <string>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <config/gl.h>



namespace glinternal {

using fn = std::function<int(int)>;

class Gloria
{
  public:
    Gloria();
    Gloria(Gloria &&other) = default;
    Gloria(Gloria &other)  = delete;

  public:
    inline GLFWwindow *Window() { return p_Window; }

    void ProcessInput();

  private:
    GLFWwindow *p_Window;
};

} // namespace glinternal
