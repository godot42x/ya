#pragma once

#include <cmath>
#include <functional>
#include <iostream>
#include <string>


#include <config/gl.h>
#include <unordered_map>



struct GLFWwindow;


namespace glinternal {

class Gloria
{
  public:
    Gloria()               = default;
    Gloria(Gloria &&other) = default;
    Gloria(Gloria &other)  = delete;

    void Init();

  public:
    inline GLFWwindow *Window() { return p_Window; }

  private:
  private:
    GLFWwindow *p_Window;
};


} // namespace glinternal
