#pragma once

#include <ownkit/precompile.h>

#include <ownkit/gl.h>

#include <config/gl.h>


namespace glinternal {

using std::cout, std::endl;

class GLX
{
  public:
    GLX();
    GLX(GLX &&other) = default;
    GLX(GLX &other)  = delete;

  public:
    inline GLFWwindow *Window() { return p_Window; }

  private:
    GLFWwindow *p_Window;
};

} // namespace glinternal
