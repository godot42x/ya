#pragma once

#include <cmath>
#include <functional>
#include <iostream>
#include <string>


#include <config/gl.h>
#include <unordered_map>

struct GLFWwindow;


namespace glinternal {

using MappingFn = std::function<bool(void)>;

class Gloria
{
  public:
    Gloria()               = default;
    Gloria(Gloria &&other) = default;
    Gloria(Gloria &other)  = delete;

    void Init();

  public:
    inline GLFWwindow *Window() { return p_Window; }

    void ProcessInput();


    template <typename aMappingFn>
    bool AddInputMapping(int key, aMappingFn fn);

    void InputCallback(int key)
    {
        m_InputMappings[key]();
    }


  private:
    GLFWwindow *p_Window;

    std::unordered_map<int, MappingFn> m_InputMappings;
};

template <typename aMappingFn>
bool Gloria::AddInputMapping(int key, aMappingFn fn)
{
    static_assert(std::is_same<MappingFn, aMappingFn>(), "Bind mapping func's Args/Retval not the same, should be std::function<bool>");
    static_assert(std::is_invocable<aMappingFn>(), "Invalid function type");

    m_InputMappings[key] = fn;
    return true;
}

} // namespace glinternal
