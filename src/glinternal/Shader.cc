#include <glinternal/Shader.h>
#include <precompile.h>



namespace glinternal {

using std::cout;
using std::endl;

/**
 * @brief Construct a new Shader:: Shader object
 * - Handle a shader progorm by ID
 * @param vertexStr
 * @param fragmentStr
 */
Shader::Shader(const char *vertexStr, const char *fragmentStr)
{
    initProgram(vertexStr, fragmentStr);
}

/**
 * @brief Construct a new Shader:: Shader object
 * Create shader from 2 files by path
 * @param vertexShaderPath
 * @param fragmentShaderPath
 */
Shader::Shader(string &vertexShaderPath, string &fragmentShaderPath)
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
        cout << "ERROR::SHADER::FILE_NOT_SUCCESSFULLY_READ" << endl;
    }

    initProgram(vertSource, fragSource);
}

Shader::Shader(string &theIntengrateFile)
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
        cout << "ERROR::SHADER::FILE_NOT_SUCCESSFULLY_READ" << endl;
    }

    initProgram(stream[VERTEX].str().c_str(), stream[FRAGMENT].str().c_str());
}

void Shader::initProgram(string &vertSource, string &fragSource)
{
    initProgram(vertSource.c_str(), fragSource.c_str());
}

void Shader::initProgram(const char *vertSource, const char *fragSource)
{
    auto vertShader = getShader(vertSource, GL_VERTEX_SHADER, "ERROR::SHADER::VERTEX::COMPILATION_FAILURE", nullptr);
    auto fragShader = getShader(fragSource, GL_FRAGMENT_SHADER, "ERROR::SHADER::VERTEX::COMPILATION_FAILURE", nullptr);
    ID              = getProgram(vertShader, fragShader);
}

void Shader::testCompile(GLuint shaderId, std::string &errorPrefix)
{
    int  success;
    char infoLog[512];
    glGetShaderiv(shaderId, GL_COMPILE_STATUS, &success);
    if (success == 0)
    {
        glGetShaderInfoLog(shaderId, sizeof(infoLog), nullptr, infoLog);
        std::cout << errorPrefix << "\n"
                  << infoLog << std::endl;
        exit(-1);
    }
}

GLuint Shader::getShader(const char *source, GLenum shaderType, std::string &&errorPrefix, GLint *place_holder)
{
    GLuint shaderId;

    shaderId = glCreateShader(shaderType);
    glShaderSource(shaderId, 1, &source, place_holder);
    glCompileShader(shaderId);
    testCompile(shaderId, errorPrefix);

    return shaderId;
}

GLuint Shader::getProgram(GLuint vert, GLuint frag)
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
        cout << "ERROR::SHADER::PROGRAM::LINK_ERROR\n"
             << msg << endl;
        exit(-1);
    }

    glDeleteShader(vert);
    glDeleteShader(frag);

    return shaderProgram;
}

void Shader::Use() const
{
    glUseProgram(ID);
}
void Shader::UnUse() const
{
    glUseProgram(0);
}

} // namespace glinternal