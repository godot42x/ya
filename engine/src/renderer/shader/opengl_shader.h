/**
 *  Author: @godot42
 *  Create Time: 2023-11-17 23:45:29
 *  Modified by: @godot42
 *  Modified time: 2024-07-28 18:53:52
 *  Description:
 */


//
// Created by nono on 9/23/23.

#ifndef HAZEL_OPEN_GL_SHADER_H
#define HAZEL_OPEN_GL_SHADER_H

#include "opengl_shader.h"
#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace hazel {

using GLenum = unsigned int;

class OpenGLShader : public Shader
{
    std::string           m_Name{};
    uint32_t              m_ShaderID{0};
    std::filesystem::path m_FilePath;

    std::unordered_map<GLenum, std::vector<uint32_t>> m_Vulkan_SPIRV;
    std::unordered_map<GLenum, std::vector<uint32_t>> m_OpenGL_SPIRV;
    std::unordered_map<GLenum, std::string>           m_OpenGL_SourceCode;

  public:
    OpenGLShader(const std::string &shader_file_path);
    OpenGLShader(const std::string &name, const std::string &vert_src, const std::string &frag_src);
    ~OpenGLShader() override;

    void Bind() const override;
    void Unbind() const override;

    [[nodiscard]] const std::string &GetName() const override { return m_Name; }

  public:
    void SetInt(const std::string &name, const int32_t value) override;
    void SetIntArray(const std::string &name, int32_t *value, uint32_t count) override;
    void SetFloat(const std::string &name, const float value) override;
    void SetFloat3(const std::string &name, const glm::vec3 &values) override;
    void SetFloat4(const std::string &name, const glm::vec4 &float4) override;
    void SetMat4(const std::string &name, const glm::mat4 &matrix) override;


  private:
    void UploadUniformFloat(const std::string &name, const float value);
    void UploadUniformFloat2(const std::string &name, const glm::vec2 &values);
    void UploadUniformFloat3(const std::string &name, const glm::vec3 &values);
    void UploadUniformFloat4(const std::string &name, const glm::vec4 &float4);

    void UploadUniformInt(const std::string &name, const int32_t value);
    void UploadUniformIntArray(const std::string &name, const int32_t *value, uint32_t count);

    void UploadUniformMat4(const std::string &name, const glm::mat4 &matrix);

  private:
    std::string                                   ReadFile(const std::string &file_path);
    std::unordered_map<unsigned int, std::string> PreProcess(const std::string &source);

    void Compile();
    void CreateVulkanBinaries(const std::unordered_map<unsigned int, std::string> &shader_sources, bool bSourceChanged = true);
    void CreateGLBinaries(bool bSourceChanged = true);
    void CreateProgram();
    void Reflect(GLenum stage, const std::vector<uint32_t> &shader_data);

    // vulkan or opengl
    std::filesystem::path GetCachePath(bool bVulkan, GLenum stage);
    std::filesystem::path GetCacheMetaPath();
};

} // namespace hazel

#endif // HAZEL_OPEN_GL_SHADER_H
