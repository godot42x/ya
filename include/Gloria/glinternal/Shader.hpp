#pragma once

#include <logx/spdx.hpp>
#include <pch/gl.h>
#include <pch/std.h>


namespace glinternal {

using std::string;

class Shader
{
  public:
    Shader(string &vertexShaderPath, string &fragmentShaderPath);
    Shader(string &theIntengrateFile);
    Shader(const char *VertLiteralStr, const char *FragLiteralStr);

    Shader(const Shader &)            = delete;
    Shader &operator=(const Shader &) = delete;

    Shader(Shader &&other) noexcept            = default;
    Shader &operator=(Shader &&other) noexcept = default;

    ~Shader();

  public:
    void Use() const;
    void UnUse() const;

    [[nodiscard("Shader program ID")]] constexpr GLuint getID() const noexcept;

    // uniform 工具函数
    template <typename T>
    void SetUnifrom1(const string &name, T value) const;


  protected:
    GLuint ID;

  private:
    void initProgram(string &vertSource, string &fragSource);
    void initProgram(const char *vertSource, const char *fragSource);

  public:
    static GLuint getProgram(GLuint vert, GLuint frag);
    static void   testCompile(GLuint shaderId, std::string &errorPrefix);
    static GLuint getShader(const char *source, GLenum shaderType, std::string &&errorPrefix, GLint *place_holder);
};

} // namespace glinternal


namespace glinternal {

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



/**
 * @brief Construct a new Shader:: Shader object
 * - Handle a shader progorm by ID
 * @param vertexStr
 * @param fragmentStr
 */
inline Shader::Shader(const char *vertexStr, const char *fragmentStr)
{
    initProgram(vertexStr, fragmentStr);
}

/**
 * @brief Construct a new Shader:: Shader object
 * Create shader from 2 files by path
 * @param vertexShaderPath
 * @param fragmentShaderPath
 */
inline Shader::Shader(string &vertexShaderPath, string &fragmentShaderPath)
{
    std::string vertSource, fragSource;
    try {
        std::ifstream vsFile, fsFile;

        vsFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        fsFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        vsFile.open(vertexShaderPath);
        fsFile.open(fragmentShaderPath);

        std::stringstream vsStream;
        std::stringstream fsStream;

        vsStream << vsFile.rdbuf();
        fsStream << fsFile.rdbuf();

        vsFile.close();
        fsFile.close();

        vertSource = vsStream.str();
        fragSource = fsStream.str();
    }
    catch (std::ifstream::failure e) {
        LERROR("[Shader] {}", e.what());
    }

    initProgram(vertSource, fragSource);
}

inline Shader::Shader(string &theIntengrateFile)
{
    std::ifstream file;

    enum ShaderType
    {
        NONE   = -1,
        VERTEX = 0,
        FRAGMENT
    };
    std::array<std::stringstream, 2> stream;

    file.exceptions(std::ifstream::failbit | std::ifstream::badbit);

    try {
        file.open(theIntengrateFile);

        ShaderType type = ShaderType::NONE;

        std::string line;
        while (std::getline(file, line))
        {
            if (line.find("#shader") != std::string::npos)
            {
                if (line.find("vertex") != std::string::npos)
                {
                    type = VERTEX;
                }
                else if (line.find("fragment") != std::string::npos)
                {
                    type = FRAGMENT;
                }
            }
            else
            {
                stream[(int)type] << line << "\n";
            }
        }

        file.close();
    }
    catch (std::ifstream::failure e) {
        LERROR("[Shader] {}", e.what());
    }

    initProgram(stream[VERTEX].str().c_str(), stream[FRAGMENT].str().c_str());
}

inline void Shader::initProgram(string &vertSource, string &fragSource)
{
    initProgram(vertSource.c_str(), fragSource.c_str());
}

inline void Shader::initProgram(const char *vertSource, const char *fragSource)
{
    auto vertShader = getShader(vertSource, GL_VERTEX_SHADER, "ERROR::SHADER::VERTEX::COMPILATION_FAILURE", nullptr);
    auto fragShader = getShader(fragSource, GL_FRAGMENT_SHADER, "ERROR::SHADER::VERTEX::COMPILATION_FAILURE", nullptr);
    ID              = getProgram(vertShader, fragShader);
}

inline void Shader::testCompile(GLuint shaderId, std::string &errorPrefix)
{
    int  success;
    char infoLog[512];
    glGetShaderiv(shaderId, GL_COMPILE_STATUS, &success);
    if (success == 0)
    {
        glGetShaderInfoLog(shaderId, sizeof(infoLog), nullptr, infoLog);
        LERROR(errorPrefix + infoLog);
        exit(-1);
    }
}

inline GLuint Shader::getShader(const char *source, GLenum shaderType, std::string &&errorPrefix, GLint *place_holder)
{
    GLuint shaderId;

    shaderId = glCreateShader(shaderType);
    glShaderSource(shaderId, 1, &source, place_holder);
    glCompileShader(shaderId);
    testCompile(shaderId, errorPrefix);

    return shaderId;
}

inline GLuint Shader::getProgram(GLuint vert, GLuint frag)
{
    GLuint shaderProgram;

    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vert);
    glAttachShader(shaderProgram, frag);
    glLinkProgram(shaderProgram);
    int success;
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (success == 0)
    {
        char msg[512];
        glGetProgramInfoLog(shaderProgram, sizeof(msg), nullptr, msg);
        LERROR("[SHADER] link error: {}", msg);
        exit(-1);
    }

    glDeleteShader(vert);
    glDeleteShader(frag);

    return shaderProgram;
}

inline void Shader::Use() const
{
    glUseProgram(ID);
}
inline void Shader::UnUse() const
{
    glUseProgram(0);
}

[[nodiscard("Shader program ID")]] constexpr GLuint Shader::getID() const noexcept
{
    return ID;
};

} // namespace glinternal
