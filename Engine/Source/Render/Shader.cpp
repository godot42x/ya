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


ShaderReflection::ShaderResources GLSLScriptProcessor::reflect(EShaderStage::T stage, const std::vector<ir_t> &spirvData)
{
    ShaderReflection::ShaderResources result;
    result.stage = stage;

    std::vector<uint32_t>        spirv_ir(spirvData.begin(), spirvData.end());
    spirv_cross::Compiler        compiler(spirv_ir);
    spirv_cross::ShaderResources resources = compiler.get_shader_resources();

    NE_CORE_TRACE("OpenGLShader:Reflect  - {} {}", EShaderStage::toString(stage), tempProcessingPath);
    NE_CORE_TRACE("\t {} uniform buffers ", resources.uniform_buffers.size());
    NE_CORE_TRACE("\t {} resources ", resources.sampled_images.size());

    // Process uniform buffers
    NE_CORE_TRACE("Uniform buffers:");
    for (const auto &resource : resources.uniform_buffers)
    {
        const auto &buffer_type  = compiler.get_type(resource.base_type_id);
        uint32_t    bufferSize   = compiler.get_declared_struct_size(buffer_type);
        uint32_t    binding      = compiler.get_decoration(resource.id, spv::DecorationBinding);
        int         member_count = buffer_type.member_types.size();
        uint32_t    set          = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);

        NE_CORE_TRACE("  {0}", resource.name);
        NE_CORE_TRACE("\tSize = {0}", bufferSize);
        NE_CORE_TRACE("\tBinding = {0}", binding);
        NE_CORE_TRACE("\tSet = {0}", set);
        NE_CORE_TRACE("\tMembers = {0}", member_count);

        // Create uniform buffer object
        ShaderReflection::UniformBuffer uniformBuffer;
        uniformBuffer.name    = resource.name;
        uniformBuffer.binding = binding;
        uniformBuffer.size    = bufferSize;

        // Process each member of the uniform buffer
        for (int i = 0; i < member_count; i++)
        {
            const std::string memberName   = compiler.get_member_name(buffer_type.self, i);
            const auto       &memberType   = compiler.get_type(buffer_type.member_types[i]);
            uint32_t          memberOffset = compiler.type_struct_member_offset(buffer_type, i);
            uint32_t          memberSize   = compiler.get_declared_struct_member_size(buffer_type, i);

            ShaderReflection::UniformBufferMember member;
            member.name   = memberName;
            member.offset = memberOffset;
            member.size   = memberSize;

            // Determine type
            if (memberType.basetype == spirv_cross::SPIRType::Float)
            {
                if (memberType.vecsize == 1 && memberType.columns == 1)
                    member.type = ShaderReflection::DataType::Float;
                else if (memberType.vecsize == 2 && memberType.columns == 1)
                    member.type = ShaderReflection::DataType::Vec2;
                else if (memberType.vecsize == 3 && memberType.columns == 1)
                    member.type = ShaderReflection::DataType::Vec3;
                else if (memberType.vecsize == 4 && memberType.columns == 1)
                    member.type = ShaderReflection::DataType::Vec4;
                else if (memberType.vecsize == 3 && memberType.columns == 3)
                    member.type = ShaderReflection::DataType::Mat3;
                else if (memberType.vecsize == 4 && memberType.columns == 4)
                    member.type = ShaderReflection::DataType::Mat4;
                else
                    member.type = ShaderReflection::DataType::Unknown;
            }
            else if (memberType.basetype == spirv_cross::SPIRType::Int)
            {
                member.type = ShaderReflection::DataType::Int;
            }
            else if (memberType.basetype == spirv_cross::SPIRType::UInt)
            {
                member.type = ShaderReflection::DataType::UInt;
            }
            else if (memberType.basetype == spirv_cross::SPIRType::Boolean)
            {
                member.type = ShaderReflection::DataType::Bool;
            }
            else
            {
                member.type = ShaderReflection::DataType::Unknown;
            }

            NE_CORE_TRACE("\t\tMember {0} (offset: {1}, size: {2})", memberName, memberOffset, memberSize);
            uniformBuffer.members.push_back(member);
        }

        result.uniformBuffers.push_back(uniformBuffer);
    }

    // Process sampled images
    for (const auto &resource : resources.sampled_images)
    {
        uint32_t    binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
        uint32_t    set     = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
        const auto &type    = compiler.get_type(resource.type_id);

        ShaderReflection::Resource sampledImage;
        sampledImage.name    = resource.name;
        sampledImage.binding = binding;
        sampledImage.set     = set;

        // Determine sampler type
        if (type.image.dim == spv::Dim2D)
            sampledImage.type = ShaderReflection::DataType::Sampler2D;
        else if (type.image.dim == spv::DimCube)
            sampledImage.type = ShaderReflection::DataType::SamplerCube;
        else
            sampledImage.type = ShaderReflection::DataType::Unknown;

        NE_CORE_TRACE("Sampled Image: {0} (binding: {1}, set: {2})", resource.name, binding, set);
        result.sampledImages.push_back(sampledImage);
    }

    return result;
}


std::optional<GLSLScriptProcessor::stage2spirv_t> GLSLScriptProcessor::process(std::string_view fileName)
{
    std::string contentStr;
    std::string fullPath = this->shaderStoragePath + "/" + fileName.data();
    if (!FileSystem::get()->readFileToString(fullPath, contentStr))
    {
        NE_CORE_ERROR("Failed to read shader file: {}", fullPath);
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

    // Record the processing path for reflection logging
    tempProcessingPath = fullPath;

    // Compile
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
            // recompile
            shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
                source,
                EShaderStage::toShadercType(stage),
                std::format("{} ({})", fullPath, std::to_string(stage)).c_str(),
                stage == EShaderStage::Vertex ? "vs_main\0" : "fs_main\0",
                options);

            if (result.GetCompilationStatus() != shaderc_compilation_status_success)
            {
                NE_CORE_ERROR("\n{}", result.GetErrorMessage());
                NE_CORE_ASSERT(false, "Shader compilation failed!");
            }

            // store compile result into memory
            ret[stage] = spirv_ir_t(result.begin(), result.end());
        }
    }

    // Perform reflection after compilation, but we don't need to store the results
    // here. Callers can use the reflect method directly on the SPIR-V data if needed.
    // for (auto &&[stage, data] : ret)
    // {
    //     ShaderReflection::ShaderResources resources = reflect(stage, data);
    //     NE_CORE_INFO("Reflected shader stage: {} with {} uniform buffers and {} sampled images",
    //         std::to_string(stage), resources.uniformBuffers.size(), resources.sampledImages.size());
    // }

    return {std::move(ret)};
}
