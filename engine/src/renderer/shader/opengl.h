#pragma once

#include <glad/glad.h>


static const char *GLShaderStageToString(GLenum stage)
{
    switch (stage)
    {
    case GL_VERTEX_SHADER:
        return "GL_VERTEX_SHADER";
    case GL_FRAGMENT_SHADER:
        return "GL_FRAGMENT_SHADER";
    }
    HZ_CORE_ASSERT(false);
    return nullptr;
}