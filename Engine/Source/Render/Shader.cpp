#include "Shader.h"



#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <shaderc/shaderc.hpp>
#include <stdio.h>
#include <string>
#include <unordered_map>

#include "./opengl.h"
#include "C:\Users\norm\1\craft\Neon\engine\src\utils\file.h"
#include "utils/trait/disable_copy.hpp"



#define NE_INFO(...) fprintf(stdout, "%s\n", std::format(__VA_ARGS__).c_str());
#define NE_CORE_ERROR(...)
#define NE_CORE_ASSERT(...)



static const char *eolFlag =
#if _WIN32
    "\r\n";
#elif __linux__
    "\n";
#endif



namespace utils
{


static EShaderStage ShaderStageFromString(const std::string &type)
{
    if (type == "vertex")
        return EShaderStage::Vertex;
    else if (type == "fragment" || type == "pixel")
        return EShaderStage::Fragment;

    NE_CORE_ASSERT(false, "Unknown shader type!");
    return EShaderStage::Undefined;
}

static shaderc_shader_kind ShaderStageToShaderRcType(EShaderStage Stage)
{
    switch (Stage) {
    case EShaderStage::Vertex:
        return shaderc_glsl_vertex_shader;
    case EShaderStage::Fragment:
        return shaderc_glsl_fragment_shader;
    default:
        break;
    }
    NE_CORE_ASSERT(false, "Unknown shader type!");
    return shaderc_shader_kind(0);
}

static void CreateCacheDirectoryIfNeeded(std::filesystem::path cache_dir)
{
    NE_INFO("Initial Cache directory: {}", cache_dir.string());

    if (!std::filesystem::exists(cache_dir))
    {
        /*
        auto project_root = ::utils::ProjectRoot();
        auto dir_iterator = std::move(project_root);
        auto dirs         = ::utils::string_split(cache_relative_path, '/');
        for (auto dir : dirs) {
            auto new_dir = dir_iterator / dir;
            if (!std::filesystem::exists(new_dir)) {
                std::filesystem::create_directory(new_dir);
                dir_iterator = std::move(new_dir);
            }
        }
        */
        std::filesystem::create_directories(cache_dir);
    }
}

static const char *ShaderStage2CachedFileExtension_OpenGL(EShaderStage stage)
{
    switch (stage)
    {
    case EShaderStage::Vertex:
        return ".cached_opengl.vert";
    case EShaderStage::Fragment:
        return ".cached_opengl.frag";
    default:
        break;
    }
    NE_CORE_ASSERT(false);
    return "";
}

static const char *ShaderStage2CachedFileExtension_Vulkan(EShaderStage stage)
{
    switch (stage)
    {
    case EShaderStage::Vertex:
        return ".cached_vulkan.vert";
    case EShaderStage::Fragment:
        return ".cached_vulkan.frag";
    default:
        break;
    }
    // NE_CORE_ASSERT(false);
    return "";
}



} // namespace utils


GLSLScriptProcessor::GLSLScriptProcessor(const std::string &path)
{
    m_FilePath = FPath(path);

    auto source_code = utils::File::read_all(m_FilePath);
    if (!source_code.has_value()) {
        NE_CORE_ERROR("Failed to read shader source file: {}", m_FilePath.string());
        NE_CORE_ASSERT(false);
    }
    std::unordered_map<EShaderStage, std::string> codes = PreProcess(source_code.value());
    // TODO: check hash
    CreateVulkanBinaries(codes, true);
    bValid = true;
}

bool GLSLScriptProcessor::TakeSpv(std::unordered_map<EShaderStage, std::vector<uint32_t>> &spv_binaries)
{
    if (!bValid) {
        return false;
    }
    spv_binaries = std::move(m_Vulkan_SPIRV);
    bValid       = false;
    return true;
}

std::unordered_map<EShaderStage, std::string> GLSLScriptProcessor::PreProcess(const std::string &source)
{
    std::unordered_map<EShaderStage, std::string> shader_sources;

    // We split the source by "#type <vertex/fragment>" preprocessing directives
    const char  *type_token     = "#type";
    const size_t type_token_len = strlen(type_token);
    size_t       pos            = source.find(type_token, 0);

    while (pos != std::string ::npos)
    {
        // get the type string
        size_t eol = source.find_first_of(eolFlag, pos);
        NE_CORE_ASSERT(eol != std::string ::npos, "Syntax error");
        size_t      begin = pos + type_token_len + 1;
        std::string type  = source.substr(begin, eol - begin);

        EShaderStage shader_type = utils::ShaderStageFromString(type);
        NE_CORE_ASSERT(shader_type, "Invalid shader type specific");

        // get the shader content range
        size_t next_line_pos = source.find_first_not_of(eolFlag, eol);

        pos          = source.find(type_token, next_line_pos);
        size_t count = (next_line_pos == std::string ::npos ? source.size() - 1 : next_line_pos);

        auto codes = source.substr(next_line_pos, pos - count);

        auto [_, Ok] = shader_sources.insert({shader_type, codes});

        NE_CORE_ASSERT(Ok, "Failed to insert this shader source");
    }

    return shader_sources;
}

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


std::filesystem::path GLSLScriptProcessor::GetCachePath(bool bVulkan, EShaderStage stage)
{
    auto cached_dir = GetBaseCachePath();
    auto filename   = m_FilePath.filename().string() +
                    (bVulkan
                         ? utils::ShaderStage2CachedFileExtension_Vulkan(stage)
                         : utils::ShaderStage2CachedFileExtension_OpenGL(stage));
    return cached_dir / filename;
}


// void GLSLScriptProcessor::Reflect(EShaderStage stage, const std::vector<uint32_t> &shader_data)
// {

//     spirv_cross::Compiler        compiler(shader_data);
//     spirv_cross::ShaderResources resources = compiler.get_shader_resources();

//     HZ_CORE_TRACE("OpenGLShader:Reflect  - {} {}", utils::GLShaderStageToString(stage), m_FilePath.string());
//     HZ_CORE_TRACE("\t {} uniform buffers ", resources.uniform_buffers.size());
//     HZ_CORE_TRACE("\t {} resources ", resources.sampled_images.size());

//     HZ_CORE_TRACE("Uniform buffers:");
//     for (const auto &resource : resources.uniform_buffers)
//     {
//         const auto &buffer_type  = compiler.get_type(resource.base_type_id);
//         uint32_t    bufferSize   = compiler.get_declared_struct_size(buffer_type);
//         uint32_t    binding      = compiler.get_decoration(resource.id, spv::DecorationBinding);
//         int         member_count = buffer_type.member_types.size();

//         HZ_CORE_TRACE("  {0}", resource.name);
//         HZ_CORE_TRACE("\tSize = {0}", bufferSize);
//         HZ_CORE_TRACE("\tBinding = {0}", binding);
//         HZ_CORE_TRACE("\tMembers = {0}", member_count);
//     }
// }

void GLSLScriptProcessor::CreateVulkanBinaries(const std::unordered_map<EShaderStage, std::string> &shader_sources, bool bSourceChanged)
{
}
}

extern const char *std::to_string(EShaderStage stage)
{
    switch (stage) {

    case EShaderStage::Vertex:
        return "vertex";
    case EShaderStage::Fragment:
        return "fragment";
        break;
    case EShaderStage::Undefined:
        break;
    }
    NE_CORE_ASSERT(false);
    return "";
}



void GLSLScriptProcessor::process(std::string_view fileName)
{
    auto content = readStorageFile(shaderStorage, fileName);
    if (!content.has_value()) {
        return;
    }

    std::string_view source(content.value().begin(), content.value().end());

    std::unordered_map<EShaderStage, std::string> shaderSources;

    // We split the source by "#type <vertex/fragment>" preprocessing directives
    const char  *typeToken    = "#type";
    const size_t typeTokenLen = strlen(typeToken);
    size_t       pos          = source.find(typeToken, 0);

    while (pos != std::string ::npos)
    {
        // get the type string
        size_t eol = source.find_first_of(eolFlag, pos);
        NE_CORE_ASSERT(eol != std::string ::npos, "Syntax error");
        size_t begin = pos + typeTokenLen + 1;
        auto   type  = std::string(source.substr(begin, eol - begin));

        EShaderStage shader_type = utils::ShaderStageFromString(type);
        NE_CORE_ASSERT(shader_type, "Invalid shader type specific");

        // get the shader content range
        size_t nextLinePos = source.find_first_not_of(eolFlag, eol);

        pos          = source.find(typeToken, nextLinePos);
        size_t count = (nextLinePos == std::string ::npos ? source.size() - 1 : nextLinePos);

        std::string codes = std::string(source.substr(nextLinePos, pos - count));

        auto [_, Ok] = shaderSources.insert({shader_type, codes});

        NE_CORE_ASSERT(Ok, "Failed to insert this shader source");
    }



    shaderc::Compiler       compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
    const bool optimize = true;
    if (optimize) {
        options.SetOptimizationLevel(shaderc_optimization_level_performance);
    }

    auto &shader_data = m_Vulkan_SPIRV;
    shader_data.clear();

    for (auto &&[stage, source] : shader_sources)
    {
        auto shader_file_path = m_FilePath;
        auto cached_path      = GetCachePath(true, stage);

        // load binary spirv caches
        if (!bSourceChanged) {
            std::ifstream f(cached_path, std::ios::in | std::ios::binary);
            NE_CORE_ASSERT(f.is_open(), "Cached spirv file not found when source do not changed!!");
            f.seekg(0, std::ios::end);
            auto size = f.tellg();
            f.seekg(0, std::ios::beg);
            auto &data = shader_data[stage];
            data.resize(size / sizeof(uint32_t));
            f.read((char *)data.data(), size);
            f.close();

            continue;
        }

        // recompile
        shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
            source,
            utils::ShaderStageToShaderRcType(stage),
            std::format("{} ({})", m_FilePath.string(), std::to_string(stage)).c_str(),
            options);

        if (result.GetCompilationStatus() != shaderc_compilation_status_success)
        {
            NE_CORE_ERROR(result.GetErrorMessage());
            std::filesystem::remove(cached_path);
            NE_CORE_ASSERT(false);
        }

        // store compile result into memory and cached file
        shader_data[stage] = std::vector<uint32_t>(result.begin(), result.end());
        std::ofstream ofs(cached_path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (ofs.is_open()) {
            auto &data = shader_data[stage];
            ofs.write((char *)data.data(), data.size() * sizeof(uint32_t));
            ofs.flush();
            ofs.close();
        }
    }

    for (auto &&[stage, data] : shader_data)
    {
        Reflect(stage, data);
    }
