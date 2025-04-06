//
/*
 * @ Author: godot42
 * @ Create Time: 2025-02-27 01:14:28
 * @ Modified by: @godot42
 * @ Modified time: 2025-03-22 00:40:40
 * @ Description:
 */

#include "Shader.h"


#include <shaderc/shaderc.hpp>
#include <stdio.h>
#include <string>
#include <unordered_map>

#include <spirv_cross/spirv.h>
#include <spirv_cross/spirv_cross.hpp>

#include "utility/string_utils.h"


#include <SDL3/SDL_gpu.h>


#include "Core/FileSystem/FileSystem.h"

#include "utility/file_utils.h"

static const char *eolFlag =
#if _WIN32
    "\r\n";
#elif __linux__
    "\n";
#endif


namespace EShaderStage
{


shaderc_shader_kind toShadercType(EShaderStage::T Stage)
{
    switch (Stage) {
    case Vertex:
        return shaderc_glsl_vertex_shader;
    case Fragment:
        return shaderc_glsl_fragment_shader;
    default:
        break;
    }

    NE_CORE_ASSERT(false, "Unknown shader type!");
    return shaderc_shader_kind(0);
}

const char *getOpenGLCacheFileExtension(EShaderStage::T stage)
{
    switch (stage)
    {
    case EShaderStage::Vertex:
        return ".cached.opengl.vert";
    case EShaderStage::Fragment:
        return ".cached.opengl.frag";
    default:
        break;
    }
    NE_CORE_ASSERT(false, "Unknown shader type!");
    return "";
}

const char *getVulkanCacheFileExtension(EShaderStage::T stage)
{
    switch (stage)
    {
    case EShaderStage::Vertex:
        return ".cached.vulkan.vert";
    case EShaderStage::Fragment:
        return ".cached.vulkan.frag";
    default:
        break;
    }
    // NE_CORE_ASSERT(false);
    return "";
}

const char *toString(EShaderStage::T Stage)
{
    return std::to_string(Stage);
}

SDL_GPUShaderStage toSDLStage(EShaderStage::T Stage)
{
    switch (Stage) {
    case Vertex:
        return SDL_GPU_SHADERSTAGE_VERTEX;
    case Fragment:
        return SDL_GPU_SHADERSTAGE_FRAGMENT;
    default:
        break;
    }
    NE_CORE_ASSERT(false, "Unknown shader type!");
    return (SDL_GPUShaderStage)-1;
}
} // namespace EShaderStage


// why we need this function?
// from glsl to spv,
// then spv to glsl source code,
// then glsl source code to spv binary,
// then binary to create opengl shader program
/*
void GLSLScriptProcessor::CreateGLBinaries(bool bSourceChanged)
{
    shaderc::Compiler       compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_opengl, shaderc_env_version_opengl_4_5);

    if (bOptimizeGLBinaries) {
        options.SetOptimizationLevel(shaderc_optimization_level_performance);
    }

    auto &shader_data = m_OpenGL_SPIRV;
    shader_data.clear();
    m_GLSL_SourceCode.clear();

    for (auto &&[stage, spirv_binary] : m_Vulkan_SPIRV)
    {
        auto cached_filepath = GetCachePath(false, stage);
        if (!bSourceChanged) {
            // load binary opengl caches
            std::ifstream in(cached_filepath, std::ios::in | std::ios::binary);
            NE_CORE_ASSERT(in.is_open(), "Cached opengl file not found when source do not changed!!");

            in.seekg(0, std::ios::end);
            auto size = in.tellg();
            in.seekg(0, std::ios::beg);

            auto &data = shader_data[stage];
            data.resize(size / sizeof(uint32_t));
            in.read((char *)data.data(), size);
            in.close();
        }
        else
        {
            shaderc::Compiler compiler;
            // spirv binary -> glsl source code
            spirv_cross::CompilerGLSL glsl_compiler(spirv_binary);
            m_GLSL_SourceCode[stage] = glsl_compiler.compile();


            // HZ_CORE_INFO("????????????????????????\n{}", m_OpenGL_SourceCode[stage]);
            auto &source = m_GLSL_SourceCode[stage];
            // HZ_CORE_ERROR(source);
            // HZ_CORE_ERROR(m_FilePath.string());

            shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
                source,
                utils::ShaderStageToShaderRcType(stage),
                m_FilePath.string().c_str());

            if (result.GetCompilationStatus() != shaderc_compilation_status_success)
            {
                NE_CORE_ERROR(result.GetErrorMessage());
                NE_CORE_ASSERT(false);
            }

            // store compile result(opengl) into memory and cached file
            shader_data[stage] = std::vector<uint32_t>(result.cbegin(), result.cend());
            std::ofstream out(cached_filepath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (out.is_open())
            {
                auto &data = shader_data[stage];
                out.write((char *)data.data(), data.size() * sizeof(uint32_t));
                out.flush();
                out.close();
            }
        }
    }
}

void GLSLProcessor::CreateProgram()
{
    GLuint program = glCreateProgram();

    std::vector<GLuint> shader_ids;
    for (auto &&[stage, spirv] : m_OpenGL_SPIRV)
    {
        GLuint shader_id = shader_ids.emplace_back(glCreateShader(stage));
        // binary from SPIR-V
        glShaderBinary(1,
                       &shader_id,
                       GL_SHADER_BINARY_FORMAT_SPIR_V,
                       spirv.data(),
                       spirv.size() * sizeof(uint32_t));
        glSpecializeShader(shader_id, "main", 0, nullptr, nullptr);
        glAttachShader(program, shader_id);
    }

    glLinkProgram(program);

    GLint bLinked;
    glGetProgramiv(program, GL_LINK_STATUS, &bLinked);

    // error handle
    if (bLinked == GL_FALSE)
    {
        GLint max_len;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &max_len);

        std::vector<GLchar> info_log(max_len);
        glGetProgramInfoLog(program, max_len, &max_len, info_log.data());

        HZ_CORE_ERROR("Shader linking failed ({0}):\n{1}", m_FilePath.string(), info_log.data());

        glDeleteProgram(program);

        for (auto id : shader_ids) {
            glDeleteShader(id);
        }
    }

    // already linked
    for (auto id : shader_ids)
    {
        glDetachShader(program, id);
        glDeleteShader(id);
    }

    m_ShaderID = program;
}
*/


std::filesystem::path GLSLScriptProcessor::GetCachePath(bool bVulkan, EShaderStage::T stage)
{
    // auto cached_dir = GetBaseCachePath();
    // auto filename   = m_FilePath.filename().string() +
    //                 (bVulkan
    //                      ? utils::ShaderStage2CachedFileExtension_Vulkan(stage)
    //                      : utils::ShaderStage2CachedFileExtension_OpenGL(stage));
    // return cached_dir / filename;

    return "";
}


void GLSLScriptProcessor::reflect(EShaderStage::T stage, const std::vector<ir_t> &spirvData)
{

    std::vector<uint32_t>        spirv_ir(spirvData.begin(), spirvData.end());
    spirv_cross::Compiler        compiler(spirv_ir);
    spirv_cross::ShaderResources resources = compiler.get_shader_resources();

    NE_CORE_TRACE("OpenGLShader:Reflect  - {} {}", EShaderStage::toString(stage), tempProcessingPath);
    NE_CORE_TRACE("\t {} uniform buffers ", resources.uniform_buffers.size());
    NE_CORE_TRACE("\t {} resources ", resources.sampled_images.size());

    NE_CORE_TRACE("Uniform buffers:");
    for (const auto &resource : resources.uniform_buffers)
    {
        const auto &buffer_type  = compiler.get_type(resource.base_type_id);
        uint32_t    bufferSize   = compiler.get_declared_struct_size(buffer_type);
        uint32_t    binding      = compiler.get_decoration(resource.id, spv::DecorationBinding);
        int         member_count = buffer_type.member_types.size();

        NE_CORE_TRACE("  {0}", resource.name);
        NE_CORE_TRACE("\tSize = {0}", bufferSize);
        NE_CORE_TRACE("\tBinding = {0}", binding);
        NE_CORE_TRACE("\tMembers = {0}", member_count);
    }
}



std::optional<GLSLScriptProcessor::stage2spirv_t> GLSLScriptProcessor::process(std::string_view fileName)
{
    std::string contentStr;
    if (!FileSystem::get()->readFileToString(fileName, contentStr))
    {
        NE_CORE_ERROR("Failed to read shader file: {}", fileName);
        return {};
    }


    // Preprocess
    std::unordered_map<EShaderStage::T, std::string> shaderSources;
    {
        std::string_view source(contentStr.begin(), contentStr.end());


        // We split the source by "#type <vertex/fragment>" preprocessing directives
        std::string_view typeToken    = "#type";
        const size_t     typeTokenLen = typeToken.size();
        size_t           pos          = source.find(typeToken, 0);

        while (pos != std::string ::npos)
        {
            // get the type string
            size_t eol = source.find_first_of(eolFlag, pos);
            NE_CORE_ASSERT(eol != std::string ::npos, "Syntax error");

            size_t           begin = pos + typeTokenLen + 1;
            std::string_view type  = source.substr(begin, eol - begin);
            type                   = ut::str::trim(type);

            EShaderStage::T shader_type = EShaderStage::fromString(type);
            NE_CORE_ASSERT(shader_type, "Invalid shader type specific");

            // get the shader content range
            size_t nextLinePos = source.find_first_not_of(eolFlag, eol);

            pos          = source.find(typeToken, nextLinePos);
            size_t count = (nextLinePos == std::string ::npos ? source.size() - 1 : nextLinePos);

            std::string codes = std::string(source.substr(nextLinePos, pos - count));

            auto [_, Ok] = shaderSources.insert({shader_type, codes});

            NE_CORE_ASSERT(Ok, "Failed to insert this shader source");
        }
    }



    // Compile
    // std::unordered_map<EShaderStage::T, std::vector<uint32_t>> ret;
    stage2spirv_t ret;
    {
        shaderc::Compiler       compiler;
        shaderc::CompileOptions options;
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
        options.SetTargetSpirv(shaderc_spirv_version_1_3);
        const bool optimize = true;
        if (optimize) {
            options.SetOptimizationLevel(shaderc_optimization_level_performance);
        }


        for (auto &&[stage, source] : shaderSources)
        {
            // auto shader_file_path = fileName;
            // auto cached_path      = GetCachePath(true, stage);

            // cachedStorage.readStorageFile(cached_path.string());

            // load binary spirv caches
            // if (!bSourceChanged) {
            //     std::ifstream f(cached_path, std::ios::in | std::ios::binary);
            //     NE_CORE_ASSERT(f.is_open(), "Cached spirv file not found when source do not changed!!");
            //     f.seekg(0, std::ios::end);
            //     auto size = f.tellg();
            //     f.seekg(0, std::ios::beg);
            //     auto &data = shader_data[stage];
            //     data.resize(size / sizeof(uint32_t));
            //     f.read((char *)data.data(), size);
            //     f.close();

            //     continue;
            // }

            // recompile
            // options
            shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
                source,
                EShaderStage::toShadercType(stage),
                std::format("{} ({})", fileName, std::to_string(stage)).c_str(),
                stage == EShaderStage::Vertex ? "vs_main\0" : "fs_main\0",
                options);

            if (result.GetCompilationStatus() != shaderc_compilation_status_success)
            {
                NE_CORE_ERROR("\n{}", result.GetErrorMessage());
                NE_CORE_ASSERT(false, "Shader compilation failed!");
            }

            // store compile result into memory
            ret[stage] = spirv_ir_t(result.begin(), result.end());

            // and into cached file
            // std::filesystem::remove(cached_path);
            //  std::ofstream ofs(cached_path, std::ios::out | std::ios::binary | std::ios::trunc);
            //  if (ofs.is_open()) {
            //      auto &data = shader_data[stage];
            //      ofs.write((char *)data.data(), data.size() * sizeof(uint32_t));
            //      ofs.flush();
            //      ofs.close();
            //  }
        }
    }

    for (auto &&[stage, data] : ret)
    {
        reflect(stage, data);
    }

    return {std::move(ret)};
}
