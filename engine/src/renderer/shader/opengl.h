#pragma once

#include <glad/glad.h>


static const char *GLShaderStage_CachedOpenGL_FileExtension(GLenum stage)
{
    switch (stage)
    {
    case GL_VERTEX_SHADER:
        return ".cached_opengl.vert";
    case GL_FRAGMENT_SHADER:
        return ".cached_opengl.frag";
    }
    HZ_CORE_ASSERT(false);
    return "";
}

static const char *GLShaderStage_CachedVulkan_FileExtension(GLenum stage)
{
    switch (stage)
    {
    case GL_VERTEX_SHADER:
        return ".cached_vulkan.vert";
    case GL_FRAGMENT_SHADER:
        return ".cached_vulkan.frag";
    }
    HZ_CORE_ASSERT(false);
    return "";
}

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