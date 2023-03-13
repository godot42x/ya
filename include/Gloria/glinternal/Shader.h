#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <string>


namespace glinternal {

using std::string;

class Shader
{
  public:
    Shader(string &vertexShaderPath, string &fragmentShaderPath);
    Shader(string &theIntengrateFile);
    Shader(const char *vertexStr, const char *fragmentStr);

    Shader(Shader &&other) noexcept            = default;
    Shader &operator=(Shader &&other) noexcept = default;

    Shader(const Shader &)            = delete;
    Shader &operator=(const Shader &) = delete;


    ~Shader();

  public:
    void Use() const;
    void UnUse() const;

    // uniform 工具函数
    template <typename T>
    void SetUnifrom1(const string &name, T value) const;

    [[nodiscard("Shader program ID")]] constexpr GLuint getID() const noexcept
    {
        return ID;
    };

  protected:
    GLuint ID;

  private:
    void initProgram(string &vertSource, string &fragSource);
    void initProgram(const char *vertSource, const char *fragSource);

  public:
    static GLuint getProgram(GLuint vert, GLuint frag);
    static void   testCompile(GLuint shaderId, std::string &errorPrefix);
    static GLuint createShader(const char *source, GLenum shaderType, std::string &&errorPrefix, GLint *place_holder);
};

template <typename T>
void Shader::SetUnifrom1(const string &name, T value) const
{
    if (std::is_same<bool, T>::value) {
        glUniform1i(glGetUniformLocation(ID, name.c_str()), (int)value);
    }
    else if (std::is_same<int, T>::value) {
        glUniform1i(glGetUniformLocation(ID, name.c_str()), value);
    }
    else if (std::is_same<float, T>::value) {
        glUniform1f(glGetUniformLocation(ID, name.c_str()), value);
    }
}

} // namespace glinternal
