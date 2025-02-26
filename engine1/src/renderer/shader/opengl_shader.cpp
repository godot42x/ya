/**
 *  Author: @godot42
 *  Create Time: 2023-11-17 23:45:29
 *  Modified by: @godot42
 *  Modified time: 2024-07-28 19:16:08
 *  Description:
 */

//
// Created by nono on 9/23/23.
//


#include "hz_pch.h"

#include "hazel/core/timer.h"

#include "hazel/core/log.h"
#include "hazel/debug/instrumentor.h"

#include "gl_macros.h"
#include "hazel/core/base.h"

#include "glad/glad.h"
#include "glm/gtc/type_ptr.hpp"
#include "nlohmann/json_fwd.hpp"
#include "opengl_shader.h"
#include "utils/path.h"



#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>

#include <shaderc/shaderc.hpp>
#include <spirv_cross/spirv_cross.hpp>
#include <spirv_cross/spirv_glsl.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "nlohmann/json.hpp"
#include "utils/file.h"



namespace hazel {



OpenGLShader::OpenGLShader(const std::string &shader_file_path)
{
    HZ_PROFILE_FUNCTION();

    m_FilePath = shader_file_path;
    m_ShaderID = 0;

    utils::CreateCacheDirectoryIfNeeded();


    // derive the shader name from the filename
    m_Name = std::filesystem::path(m_FilePath).stem().string();
    HZ_CORE_ASSERT(!m_Name.empty(), "Cannot derive the shader name from shader file.");

    HZ_CORE_TRACE("Creating OpenGL Shader....\n\tShader file path: {} \n\tShader name: {}", m_FilePath.string(), m_Name);

    // check the source file hash
    bool bSourceChanged = true;

    auto source = ::utils::File::read_all(m_FilePath.string());
    HZ_CORE_ASSERT(source.has_value(), "Failed to read shader source file: {}", m_FilePath.string());

    auto hash = ::utils::File::get_hash(std::ref(source.value())).value();
    HZ_CORE_INFO("Current Shader hash: {}", hash);

    nlohmann::json cached_meta_json;
    auto           cached_meta_filepath = GetCacheMetaPath();
    std::ifstream  ifs(cached_meta_filepath);
    if (!ifs.fail()) {
        cached_meta_json = nlohmann::json::parse(ifs);
        if (cached_meta_json.contains("hash")) {
            HZ_CORE_ASSERT(cached_meta_json["hash"].is_number(), "Old shader hash is not a number");
            auto old_hash  = cached_meta_json["hash"].get<size_t>();
            bSourceChanged = (hash != old_hash);
            HZ_CORE_INFO("Cached shader hash:  {}", old_hash);
        }
    }

    // split shader sources
    std::unordered_map<GLenum, std::string> shader_sources;
    if (bSourceChanged) {
        shader_sources = PreProcess(source.value());
        // HZ_CORE_ERROR("{}:\n{}", utils::GLShaderStageToString(GL_VERTEX_SHADER), shader_sources[GL_VERTEX_SHADER]);
        // HZ_CORE_INFO("{}:\n{}", utils::GLShaderStageToString(GL_FRAGMENT_SHADER), shader_sources[GL_FRAGMENT_SHADER]);
    }
    else {
        // construct  empty shader sources for recompilation
        // TODO: remove  this  and fix successor steps
        shader_sources = {
            {  GL_VERTEX_SHADER, {}},
            {GL_FRAGMENT_SHADER, {}}
        };
    }


    {
        Timer timer;
        CreateVulkanBinaries(shader_sources, bSourceChanged),
        CreateGLBinaries(bSourceChanged);
        CreateProgram();
        // Sleep(3000);
        HZ_CORE_WARN("Shader compile and creation took {0} ms", timer.ElapsedMillis());
    }

    // overwrite the source hash
    if (bSourceChanged) {
        cached_meta_json["hash"] = hash;
        std::ofstream ofs(cached_meta_filepath, std::ios::out | std::ios::trunc);
        ofs << cached_meta_json.dump(4) << std::endl;
    }
}

OpenGLShader::OpenGLShader(const std::string &name, const std::string &vert_src, const std::string &frag_src)
{
    HZ_PROFILE_FUNCTION();


    m_ShaderID = 0;
    m_Name     = name;

    std::unordered_map<GLenum, std::string> sources = {
        {  GL_VERTEX_SHADER, vert_src},
        {GL_FRAGMENT_SHADER, frag_src}
    };

    CreateVulkanBinaries(sources, true);
    CreateGLBinaries(true);
    CreateProgram();
}

OpenGLShader::~OpenGLShader()
{
}

void OpenGLShader::Bind() const
{
    glUseProgram(m_ShaderID);
}
void OpenGLShader::Unbind() const
{
    glUseProgram(0);
}

void OpenGLShader::UploadUniformMat4(const std::string &name, const glm::mat4 &matrix)
{
    // transpose: A^t
    glUniformMatrix4fv(glGetUniformLocation(m_ShaderID, name.c_str()), 1, GL_FALSE, glm::value_ptr(matrix));
}
void OpenGLShader::UploadUniformFloat4(const std::string &name, const glm::vec4 &float4)
{
    //    glUniform4fv(glGetUniformLocation(m_ShaderID, name.c_str()), 1, glm::value_ptr(float4));
    glUniform4f(glGetUniformLocation(m_ShaderID, name.c_str()), float4.x, float4.y, float4.z, float4.z);
}
void OpenGLShader::UploadUniformFloat(const std::string &name, const float value)
{
    glUniform1f(glGetUniformLocation(m_ShaderID, name.c_str()), value);
}
void OpenGLShader::UploadUniformFloat2(const std::string &name, const glm::vec2 &values)
{
    glUniform2f(glGetUniformLocation(m_ShaderID, name.c_str()), values.x, values.y);
}
void OpenGLShader::UploadUniformFloat3(const std::string &name, const glm::vec3 &values)
{
    GL_CALL(glUniform3f(glGetUniformLocation(m_ShaderID, name.c_str()), values.x, values.y, values.z));
}
void OpenGLShader::UploadUniformInt(const std::string &name, const int32_t value)
{
    // GL_CALL(
    glUniform1i(glGetUniformLocation(m_ShaderID, name.c_str()), value) /*)*/;
}

void OpenGLShader::UploadUniformIntArray(const std::string &name, const int32_t *value, uint32_t count)
{
    glUniform1iv(glad_glGetUniformLocation(m_ShaderID, name.c_str()), count, value);
}


void OpenGLShader::SetInt(const std::string &name, const int32_t value)
{
    UploadUniformInt(name, value);
}

void OpenGLShader::SetIntArray(const std::string &name, int32_t *value, uint32_t count)
{
    UploadUniformIntArray(name, value, count);
}

void OpenGLShader::SetFloat3(const std::string &name, const glm::vec3 &values)
{
    UploadUniformFloat3(name, values);
}
void OpenGLShader::SetFloat4(const std::string &name, const glm::vec4 &float4)
{
    UploadUniformFloat4(name, float4);
}
void OpenGLShader::SetMat4(const std::string &name, const glm::mat4 &matrix)
{
    UploadUniformMat4(name, matrix);
}

void OpenGLShader::SetFloat(const std::string &name, const float value)
{
    UploadUniformFloat(name, value);
}


} // namespace hazel