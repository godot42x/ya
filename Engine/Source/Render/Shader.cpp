//
/*
 * @ Author: godot42
 * @ Create Time: 2025-02-27 01:14:28
 * @ Modified by: @godot42
 * @ Modified time: 2025-03-22 00:40:40
 * @ Description:
 */

#include "Shader.h"


#include <algorithm>
#include <array>
#include <map>
#include <shaderc/shaderc.h>
#include <shaderc/shaderc.hpp>
#include <stdio.h>
#include <string>
#include <unordered_map>



#include <spirv_cross/spirv.h>
#include <spirv_cross/spirv_cross.hpp>

#include "utility.cc/string_utils.h"


#include <SDL3/SDL_gpu.h>


#include "Core/System/VirtualFileSystem.h"

// Slang runtime API
#include <slang/slang-com-ptr.h>
#include <slang/slang.h>



static const char* eolFlag =
#if _WIN32
    "\r\n";
#elif __linux__
    "\n";
#endif

namespace ya
{
namespace EShaderStage
{


shaderc_shader_kind toShadercType(EShaderStage::T Stage)
{
    switch (Stage) {
    case Vertex:
        return shaderc_glsl_vertex_shader;
    case Fragment:
        return shaderc_glsl_fragment_shader;
    case Geometry:
        return shaderc_glsl_geometry_shader;
    case Compute:
        return shaderc_glsl_compute_shader;
    default:
        UNREACHABLE();
        break;
    }

    YA_CORE_ASSERT(false, "Unknown shader type!");
    return shaderc_shader_kind(0);
}

const char* getOpenGLCacheFileExtension(EShaderStage::T stage)
{
    switch (stage)
    {
    case EShaderStage::Vertex:
        return ".cached.opengl.vert";
    case EShaderStage::Fragment:
        return ".cached.opengl.frag";
    case EShaderStage::Geometry:
        return ".cached.opengl.geom";
    default:
        UNREACHABLE();
        break;
    }
    YA_CORE_ASSERT(false, "Unknown shader type!");
    return "";
}

const char* getVulkanCacheFileExtension(EShaderStage::T stage)
{
    switch (stage)
    {
    case EShaderStage::Vertex:
        return ".cached.vulkan.vert";
    case EShaderStage::Fragment:
        return ".cached.vulkan.frag";
    case EShaderStage::Geometry:
        return ".cached.vulkan.geom";
    default:
        UNREACHABLE();
        break;
    }
    YA_CORE_ASSERT(false, "Unknown shader type!");
    return "";
}

const char* getSpvOutputExtension(EShaderStage::T stage)
{
    switch (stage)
    {
    case EShaderStage::Vertex:
        return "vert.spv";
    case EShaderStage::Fragment:
        return "frag.spv";
    case EShaderStage::Geometry:
        return "geom.spv";
    default:
        UNREACHABLE();
        break;
    }
    // YA_CORE_ASSERT(false);
    return "";
}


SDL_GPUShaderStage toSDLStage(EShaderStage::T Stage)
{
    switch (Stage) {
    case Vertex:
        return SDL_GPU_SHADERSTAGE_VERTEX;
    case Fragment:
        return SDL_GPU_SHADERSTAGE_FRAGMENT;
    // case Geometry:
    // return SDL_GPU_SHADERSTAGE_GEOMETRY;
    default:
        break;
    }
    YA_CORE_ASSERT(false, "Unknown shader type!");
    return (SDL_GPUShaderStage)-1;
}
} // namespace EShaderStage



std::filesystem::path GLSLProcessor::GetCachePath(bool bVulkan, EShaderStage::T stage)
{
    // auto cached_dir = GetBaseCachePath();
    // auto filename   = m_FilePath.filename().string() +
    //                 (bVulkan
    //                      ? utils::ShaderStage2CachedFileExtension_Vulkan(stage)
    //                      : utils::ShaderStage2CachedFileExtension_OpenGL(stage));
    // return cached_dir / filename;

    return "";
}



// Get size of a SPIRV type with proper alignment for C++ structs
uint32_t SPIRVHelper::getSpirvTypeSize(const spirv_cross::SPIRType& type)
{
    uint32_t size = 0;
    switch (type.basetype)
    {
    case spirv_cross::SPIRType::Float:
        size = sizeof(float) * type.vecsize * type.columns;
        break;
    case spirv_cross::SPIRType::Int:
        size = sizeof(int32_t) * type.vecsize * type.columns;
        break;
    case spirv_cross::SPIRType::UInt:
        size = sizeof(uint32_t) * type.vecsize * type.columns;
        break;
    case spirv_cross::SPIRType::Boolean:
        size = sizeof(uint32_t) * type.vecsize * type.columns; // Booleans in SPIR-V typically use 32 bits
        break;
    default:
        break;
    }

    // TODO: handled this
    // Handle alignment requirements for vec3 (which is actually 16 bytes in many GPU APIs)
    // This is a critical consideration when working with struct data in shaders
    // if (type.basetype == spirv_cross::SPIRType::Float && type.vecsize == 3 && type.columns == 1) {
    // vec3 is typically padded to 16 bytes (4 floats) for alignment
    // return 16;
    // }

    return size;
}

// Get the offset for a member in a struct with proper C++ alignment
uint32_t SPIRVHelper::getVertexAlignedOffset(uint32_t current_offset, const spirv_cross::SPIRType& type)
{
    // Determine the alignment requirement based on std140 layout rules
    uint32_t alignment = 4; // Default to 4 bytes for basic types

    return (current_offset + alignment) % alignment == 0 ? current_offset : current_offset + alignment;


    // if ((current_offset + size) % alignment != 0) {
    //     // Align to the next multiple of the alignment size
    //     current_offset += (alignment - (current_offset % alignment));
    // }
    // return aligned_offset;
}

namespace ShaderReflection
{

DataType getSpirvBaseType(const spirv_cross::SPIRType& type)
{
    switch (type.basetype) {
    case spirv_cross::SPIRType::Boolean:
        return DataType::Bool;

    case spirv_cross::SPIRType::Int:
        return DataType::Int;

    case spirv_cross::SPIRType::UInt:
        return DataType::UInt;

    case spirv_cross::SPIRType::Float:
        if (type.columns > 1) {
            // Matrix types
            if (type.columns == 3 && type.vecsize == 3)
                return DataType::Mat3;
            else if (type.columns == 4 && type.vecsize == 4)
                return DataType::Mat4;
        }
        else {
            // Vector or scalar types
            if (type.vecsize == 1)
                return DataType::Float;
            else if (type.vecsize == 2)
                return DataType::Vec2;
            else if (type.vecsize == 3)
                return DataType::Vec3;
            else if (type.vecsize == 4)
                return DataType::Vec4;
        }
        break;

    case spirv_cross::SPIRType::SampledImage:
        switch (type.image.dim) {
        case spv::Dim2D:
            return DataType::Sampler2D;
        case spv::DimCube:
            return DataType::SamplerCube;
        case spv::Dim3D:
            return DataType::Sampler3D;
        default:
            break;
        }
        break;

    default:
        break;
    }

    return DataType::Unknown;
}


uint32_t getDataTypeSize(DataType type)
{
    switch (type) {
    case DataType::Bool:
        return sizeof(uint32_t); // Booleans in SPIR-V typically use 32 bits
    case DataType::Int:
        return sizeof(int32_t);
    case DataType::UInt:
        return sizeof(uint32_t);
    case DataType::Float:
        return sizeof(float);
    case DataType::Vec2:
        return sizeof(float) * 2;
    case DataType::Vec3:
        return sizeof(float) * 3;
    case DataType::Vec4:
        return sizeof(float) * 4;
    case DataType::Mat3:
        return sizeof(float) * 3 * 3;
    case DataType::Mat4:
        return sizeof(float) * 4 * 4;
    case DataType::Sampler2D:
    case DataType::SamplerCube:
    case DataType::Sampler3D:
        return sizeof(uint32_t); // Samplers are typically bound by descriptor sets/slots
    default:
        return 0;
    }
}

} // namespace ShaderReflection

ShaderReflection::ShaderResources GLSLProcessor::reflect(EShaderStage::T stage, const std::vector<ir_t>& spirvData)
{
    std::vector<uint32_t>        spirv_ir(spirvData.begin(), spirvData.end());
    spirv_cross::Compiler        compiler(spirv_ir);
    spirv_cross::ShaderResources spirvResources = compiler.get_shader_resources();

    // Create our custom shader resources structure
    ShaderReflection::ShaderResources resources;
    resources.stage          = stage;
    resources.spirvResources = spirvResources; // Store original spirv resources

    YA_CORE_TRACE("===============================================================================");
    YA_CORE_TRACE("OpenGLShader:Reflect {} -> {}", curFileName, EShaderStage::T2Strings[stage]);
    YA_CORE_TRACE("\t {} uniform buffers ", spirvResources.uniform_buffers.size());
    YA_CORE_TRACE("\t {} storage buffers ", spirvResources.storage_buffers.size());
    YA_CORE_TRACE("\t {} stage inputs ", spirvResources.stage_inputs.size());
    YA_CORE_TRACE("\t {} stage outputs ", spirvResources.stage_outputs.size());
    YA_CORE_TRACE("\t {} storage images ", spirvResources.storage_images.size());
    YA_CORE_TRACE("\t {} resources ", spirvResources.sampled_images.size());


    // Process stage inputs with alignment information
    YA_CORE_TRACE("Stage Inputs (with alignment information):");
    uint32_t   struct_offset      = 0;
    const bool IS_CPP_STRUCT_PACK = true;
    for (const auto& input : spirvResources.stage_inputs) {

        uint32_t location = compiler.get_decoration(input.id, spv::DecorationLocation);
        uint32_t offset   = compiler.get_decoration(input.id, spv::DecorationOffset);

        const spirv_cross::SPIRType& type = compiler.get_type(input.type_id);

        // Calculate aligned offset
        uint32_t aligned_offset = SPIRVHelper::getVertexAlignedOffset(struct_offset, type);
        uint32_t type_size      = SPIRVHelper::getSpirvTypeSize(type);
        struct_offset           = aligned_offset + type_size;


        // Create stage input data
        ShaderReflection::StageIOData inputData{};
        inputData.name     = input.name;
        inputData.type     = ShaderReflection::getSpirvBaseType(type);
        inputData.location = location;
        inputData.offset   = aligned_offset;
        inputData.size     = type_size;
        inputData.vecsize  = type.vecsize;
        inputData.basetype = static_cast<uint32_t>(type.basetype);

        // Add to our resources
        resources.inputs.push_back(inputData);


        YA_CORE_TRACE("\t(name: {0}, location: {1}, shader offset: {2}, aligned offset: {3}, size: {4}, type: {5}, {6}",
                      input.name,
                      location,
                      offset,
                      aligned_offset,
                      type_size,
                      ShaderReflection::DataType2Strings[inputData.type],
                      type);
    }

    // Process stage outputs
    YA_CORE_TRACE("Stage Outputs:");
    for (const auto& output : spirvResources.stage_outputs) {
        uint32_t                     location = compiler.get_decoration(output.id, spv::DecorationLocation);
        uint32_t                     offset   = compiler.get_decoration(output.id, spv::DecorationOffset);
        const spirv_cross::SPIRType& type     = compiler.get_type(output.type_id);

        // Create stage output data
        ShaderReflection::StageIOData outputData;
        outputData.name     = output.name;
        outputData.type     = ShaderReflection::getSpirvBaseType(type);
        outputData.location = location;
        outputData.offset   = offset;
        outputData.size     = SPIRVHelper::getSpirvTypeSize(type);
        outputData.vecsize  = type.vecsize;
        outputData.basetype = static_cast<uint32_t>(type.basetype);

        // Add to our resources
        resources.outputs.push_back(outputData);

        YA_CORE_TRACE("\t(name: {0}, location: {1}, offset: {2}, type: {3})", output.name, location, offset, ShaderReflection::DataType2Strings[outputData.type]);
    }

    // Process uniform buffers
    YA_CORE_TRACE("Uniform buffers:");
    for (const auto& resource : spirvResources.uniform_buffers) {
        const auto& buffer_type  = compiler.get_type(resource.base_type_id);
        uint32_t    bufferSize   = compiler.get_declared_struct_size(buffer_type);
        uint32_t    binding      = compiler.get_decoration(resource.id, spv::DecorationBinding);
        int         member_count = buffer_type.member_types.size();
        uint32_t    set          = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);

        // Create uniform buffer
        ShaderReflection::UniformBuffer uniformBuffer;
        uniformBuffer.name    = resource.name;
        uniformBuffer.binding = binding;
        uniformBuffer.set     = set;
        uniformBuffer.size    = bufferSize;

        YA_CORE_TRACE("Buffer Name:  {0}", resource.name);
        YA_CORE_TRACE("\tSize = {0}", bufferSize);
        YA_CORE_TRACE("\tBinding = {0}", binding);
        YA_CORE_TRACE("\tSet = {0}", set);
        YA_CORE_TRACE("\tMembers = {0}", member_count);

        // Process each member of the uniform buffer with alignment information
        YA_CORE_TRACE("\tMembers with alignment:");
        uint32_t struct_offset = 0;

        for (int i = 0; i < member_count; i++) {
            const std::string memberName   = compiler.get_member_name(buffer_type.self, i);
            const auto&       memberType   = compiler.get_type(buffer_type.member_types[i]);
            uint32_t          memberOffset = compiler.type_struct_member_offset(buffer_type, i);
            uint32_t          memberSize   = compiler.get_declared_struct_member_size(buffer_type, i);

            // Calculate proper C++ aligned offset
            // uint32_t aligned_offset = SPIRVHelper::getAlignedOffset(struct_offset, memberType);
            // struct_offset           = aligned_offset + SPIRVHelper::getSpirvTypeSize(memberType);

            // Create uniform buffer member
            ShaderReflection::UniformBufferMember member;
            member.name   = memberName;
            member.type   = ShaderReflection::getSpirvBaseType(memberType);
            member.offset = memberOffset;
            member.size   = memberSize;

            // Add to uniform buffer
            uniformBuffer.members.push_back(member);

            YA_CORE_TRACE("\t\t-Member {0} (shader offset: {1}, C++ aligned offset: {2}, size: {3}, type: {4})",
                          memberName,
                          memberOffset,
                          -1,
                          memberSize,
                          ShaderReflection::DataType2Strings[member.type]);

            // Check for alignment mismatches between shader and C++
            // if (memberOffset != aligned_offset) {
            //     YA_CORE_WARN("\t\t  ⚠️ ALIGNMENT MISMATCH: Shader offset {0} != C++ aligned offset {1} for member {2}",
            //                  memberOffset,
            //                  aligned_offset,
            //                  memberName);
            // }
        }

        // Add uniform buffer to resources
        resources.uniformBuffers.push_back(uniformBuffer);
    }

    // Process sampled images
    YA_CORE_TRACE("Sampled images:");
    for (const auto& resource : spirvResources.sampled_images) {
        uint32_t    binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
        uint32_t    set     = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
        const auto& type    = compiler.get_type(resource.type_id);

        // Determine array size (1 for non-array, >1 for array descriptors)
        uint32_t arraySize = 1;
        if (!type.array.empty()) {
            arraySize = type.array[0]; // First dimension of the array
            if (arraySize == 0) {
                arraySize = 1; // Runtime-sized arrays default to 1
            }
        }

        // Create resource
        ShaderReflection::Resource sampledImage;
        sampledImage.name      = resource.name;
        sampledImage.binding   = binding;
        sampledImage.set       = set;
        sampledImage.type      = ShaderReflection::getSpirvBaseType(type);
        sampledImage.arraySize = arraySize;

        // Add to resources
        resources.sampledImages.push_back(sampledImage);

        YA_CORE_TRACE("\tSampled Image: {0} (binding: {1}, set: {2}, type: {3}, arraySize: {4})", resource.name, binding, set, ShaderReflection::DataType2Strings[sampledImage.type], arraySize);
    }

    // Process push constant buffers
    YA_CORE_TRACE("Push constant buffers:");
    for (const auto& resource : spirvResources.push_constant_buffers) {
        const auto& bufferType = compiler.get_type(resource.base_type_id);
        uint32_t    bufferSize = compiler.get_declared_struct_size(bufferType);

        ShaderReflection::PushConstantBuffer pcBuffer;
        pcBuffer.name = resource.name;
        pcBuffer.size = bufferSize;

        resources.pushConstantBuffers.push_back(pcBuffer);

        YA_CORE_TRACE("\tPush Constant: {0} (size: {1})", resource.name, bufferSize);
    }

    return resources;
}

auto getOption(bool bOptimized)
{
    TODO("Choose vulkan version?");
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
    options.SetTargetSpirv(shaderc_spirv_version_1_3);
    const bool optimize = true;
    if (optimize) {
        options.SetOptimizationLevel(shaderc_optimization_level_performance);
    }
    options.AddMacroDefinition("YA_PLATFORM_VULKAN", "1");
    return options;
}

static const char* getStageMacroName(EShaderStage::T stage)
{
    switch (stage)
    {
    case EShaderStage::Vertex:
        return "SHADER_STAGE_VERTEX";
    case EShaderStage::Fragment:
        return "SHADER_STAGE_FRAGMENT";
    case EShaderStage::Geometry:
        return "SHADER_STAGE_GEOMETRY";
    case EShaderStage::Compute:
        return "SHADER_STAGE_COMPUTE";
    default:
        return nullptr;
    }
}

struct ShadercIncludeResultData
{
    shaderc_include_result result{};
    std::string            sourceName;
    std::string            content;
};

struct ShadercVfsIncluder : public shaderc::CompileOptions::IncluderInterface
{
    // Extra base directories searched when relative include resolution fails.
    // Populated with "Engine/Shader/GLSL" so that #include "Common/Limits.glsl"
    // works from any sub-directory shader.
    static constexpr std::string_view kGlslBaseDir = "Engine/Shader/GLSL";

    shaderc_include_result* GetInclude(const char* requested_source,
                                       shaderc_include_type /*type*/,
                                       const char* requesting_source,
                                       size_t /*include_depth*/) override
    {
        auto* data = new ShadercIncludeResultData();

        auto reqPath = std::filesystem::path(requested_source ? requested_source : "");
        auto srcPath = std::filesystem::path(requesting_source ? requesting_source : "");

        std::filesystem::path resolvedPath;
        if (reqPath.is_absolute()) {
            resolvedPath = reqPath;
        }
        else {
            resolvedPath = (srcPath.parent_path() / reqPath).lexically_normal();
        }

        data->sourceName = resolvedPath.generic_string();
        bool ok          = false;
        if (VFS::get()->isFileExists(data->sourceName))
        {
            ok = VirtualFileSystem::get()->readFileToString(data->sourceName, data->content);
        }

        if (!ok) {
            // Fallback: search Engine/Shader/GLSL base directory
            auto fallback     = (std::filesystem::path(kGlslBaseDir) / reqPath).lexically_normal();
            auto fallbackName = fallback.generic_string();
            if (VirtualFileSystem::get()->readFileToString(fallbackName, data->content)) {
                data->sourceName = fallbackName;
                ok               = true;
            }
            if (!ok) {
                data->content = std::format("#error \"Failed to include file: {}\"\n or fallback file: {}", data->sourceName, fallbackName);
                YA_CORE_ERROR("Shader include failed: '{}' or '{}' requested by '{}'", data->sourceName, fallbackName, requesting_source ? requesting_source : "");
            }
        }

        if (!VirtualFileSystem::get()->readFileToString(data->sourceName, data->content)) {
            // Fallback: search Engine/Shader/GLSL base directory
            auto fallback     = (std::filesystem::path(kGlslBaseDir) / reqPath).lexically_normal();
            auto fallbackName = fallback.generic_string();
            if (VirtualFileSystem::get()->readFileToString(fallbackName, data->content)) {
                data->sourceName = fallbackName;
            }
            else {
                data->content = std::format("#error \"Failed to include file: {}\"\n", data->sourceName);
                YA_CORE_ERROR("Shader include failed: '{}' requested by '{}'", data->sourceName, requesting_source ? requesting_source : "");
            }
        }

        data->result.source_name        = data->sourceName.c_str();
        data->result.source_name_length = data->sourceName.size();
        data->result.content            = data->content.c_str();
        data->result.content_length     = data->content.size();
        data->result.user_data          = data;
        return &data->result;
    }

    void ReleaseInclude(shaderc_include_result* include_result) override
    {
        if (!include_result) {
            return;
        }
        auto* data = static_cast<ShadercIncludeResultData*>(include_result->user_data);
        delete data;
    }
};



bool GLSLProcessor::compileToSpv(std::string_view filename, std::string_view content, EShaderStage::T stage, const std::vector<std::string>& defines, std::vector<ir_t>& outSpv)
{
    shaderc::Compiler compiler;

    auto options = getOption(false);
    options.SetIncluder(std::make_unique<ShadercVfsIncluder>());

    if (const char* stageMacro = getStageMacroName(stage))
    {
        options.AddMacroDefinition(stageMacro, "1");
    }

    for (const auto& def : defines)
    {
        auto eq = def.find('=');
        if (eq != std::string::npos)
        {
            auto name = ut::str::trim(std::string_view(def).substr(0, eq));
            auto val  = ut::str::trim(std::string_view(def).substr(eq + 1));
            if (!name.empty()) {
                options.AddMacroDefinition(std::string(name), std::string(val.empty() ? "1" : val));
            }
            continue;
        }

        auto ws = def.find_first_of(" \t");
        if (ws != std::string::npos)
        {
            auto name = ut::str::trim(std::string_view(def).substr(0, ws));
            auto val  = ut::str::trim(std::string_view(def).substr(ws + 1));
            if (!name.empty()) {
                options.AddMacroDefinition(std::string(name), std::string(val.empty() ? "1" : val));
            }
            continue;
        }

        auto name = ut::str::trim(std::string_view(def));
        if (!name.empty()) {
            options.AddMacroDefinition(std::string(name), "1");
        }
    }

    // recompile
    shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
        content.data(),
        EShaderStage::toShadercType(stage),
        filename.data(),
        "main",
        options);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success)
    {
        YA_CORE_ERROR("{}:\n{}", EShaderStage::toString(stage), result.GetErrorMessage());
        // YA_CORE_ASSERT(false, "Shader compilation failed!");
        return false;
    }

    // store compile result into memory
    outSpv = spirv_ir_t(result.begin(), result.end());
    return true;
}

std::unordered_map<EShaderStage::T, std::string> GLSLProcessor::preprocessCombinedSource(const stdpath& filepath)
{
    std::unordered_map<EShaderStage::T, std::string> shaderSources;

    std::string contentStr;

    if (!VirtualFileSystem::get()->readFileToString(filepath.string(), contentStr))
    {
        YA_CORE_ERROR("Failed to read shader file: {}", filepath.string());
        return {};
    }


    std::string_view source(contentStr.begin(), contentStr.end());

    const bool hasTypeToken = source.find("#type", 0) != std::string::npos;

    // Mode 2: Macro-gated single source (no #type), e.g. #ifdef SHADER_STAGE_VERTEX
    // Compile the same source for each declared stage; compileToSpv injects SHADER_STAGE_*.
    if (!hasTypeToken)
    {
        auto hasStageMacro = [&](std::string_view macro) {
            return source.find(macro) != std::string::npos;
        };

        if (hasStageMacro("SHADER_STAGE_VERTEX")) {
            shaderSources[EShaderStage::Vertex] = contentStr;
        }
        if (hasStageMacro("SHADER_STAGE_FRAGMENT")) {
            shaderSources[EShaderStage::Fragment] = contentStr;
        }
        if (hasStageMacro("SHADER_STAGE_GEOMETRY")) {
            shaderSources[EShaderStage::Geometry] = contentStr;
        }
        if (hasStageMacro("SHADER_STAGE_COMPUTE")) {
            shaderSources[EShaderStage::Compute] = contentStr;
        }

        if (!shaderSources.empty()) {
            return shaderSources;
        }

        YA_CORE_ERROR("Shader source '{}' has no '#type' markers and no SHADER_STAGE_* macros", filepath.string());
        return {};
    }


    // We split the source by "#type <vertex/fragment>" preprocessing directives
    std::string_view typeToken    = "#type";
    const size_t     typeTokenLen = typeToken.size();
    size_t           pos          = source.find(typeToken, 0);

    while (pos != std::string ::npos)
    {
        // get the type string
        size_t eol = source.find_first_of(eolFlag, pos);
        YA_CORE_ASSERT(eol != std::string ::npos, "Syntax error");

        size_t           begin = pos + typeTokenLen + 1;
        std::string_view type  = source.substr(begin, eol - begin);
        type                   = ut::str::trim(type);

        EShaderStage::T shader_type = EShaderStage::fromString(type);
        // YA_CORE_ASSERT(shader_type, "Invalid shader type specific");

        // get the shader content range
        size_t nextLinePos = source.find_first_not_of(eolFlag, eol);

        pos          = source.find(typeToken, nextLinePos);
        size_t count = (nextLinePos == std::string ::npos ? source.size() - 1 : nextLinePos);

        // 计算这个shader stage在原始文件中的起始行号
        size_t lineNumber = std::count(source.begin(), source.begin() + nextLinePos, '\n');

        // 提取shader代码
        std::string codes = std::string(source.substr(nextLinePos, pos - count));

        // 在代码前面填充空白行，使错误行号能够直接对应到原始文件
        std::string paddedCodes;
        paddedCodes.reserve(lineNumber + codes.size());
        paddedCodes.append(lineNumber, '\n');
        paddedCodes.append(codes);

        auto [_, Ok] = shaderSources.insert(std::make_pair(shader_type, paddedCodes));

        YA_CORE_ASSERT(Ok, "Failed to insert this shader source");
    }
    return shaderSources;
}

bool GLSLProcessor::processSpvFiles(std::string_view vertFile, std::string_view fragFile, stage2spirv_t& outSpvMap)
{
    YA_CORE_INFO("Found spv shader files for {}: {} and {}", curFileName, vertFile, fragFile);


    auto toSpirv = [](std::string_view ctx, const std::string& src, std::vector<ir_t>& spirv) {
        if (src.size() % sizeof(ir_t) != 0)
        {
            YA_CORE_WARN("Cached vertex shader file size is not a multiple of ir_t size: {}", ctx);
            return false;
        }
        spirv.resize(src.size() / sizeof(ir_t));
        std::memcpy(spirv.data(), src.data(), src.size());
        return true;
    };

    std::vector<ir_t> vertSpv;
    std::string       vertFileStr;
    if (!VirtualFileSystem::get()->readFileToString(vertFile, vertFileStr))
    {
        YA_CORE_WARN("Failed to read cached vertex shader file: {}", vertFile);
        return false;
    }
    if (!toSpirv(curFileName, vertFileStr, vertSpv)) {
        YA_CORE_ERROR("Failed to convert cached vertex shader file to SPIR-V: {}", vertFile);
        return false;
    }
    outSpvMap[EShaderStage::Vertex] = std::move(vertSpv);

    std::vector<ir_t> fragSpv;
    std::string       fragFileStr;
    if (!VirtualFileSystem::get()->readFileToString(fragFile, fragFileStr))
    {
        YA_CORE_ERROR("Failed to read cached fragment shader file: {}", fragFile);
        return false;
    }
    if (!toSpirv(curFileName, fragFileStr, fragSpv)) {
        YA_CORE_ERROR("Failed to convert cached fragment shader file to SPIR-V: {}", fragFile);
        return false;
    }
    outSpvMap[EShaderStage::Fragment] = std::move(fragSpv);

    return true;
}



bool GLSLProcessor::processCombinedSource(const stdpath& filepath, const std::vector<std::string>& defines, stage2spirv_t& outSpvMap)
{
    std::unordered_map<EShaderStage::T, std::string> shaderSources = preprocessCombinedSource(filepath);

    if (shaderSources.empty())
    {
        YA_CORE_ERROR("Failed to preprocess shader source: {}", filepath.string());
        return false;
    }


    // Compile
    for (auto&& [stage, source] : shaderSources)
    {
        std::vector<ir_t> spv;
        if (!compileToSpv(filepath.string(), source, stage, defines, spv))
        {
            YA_CORE_ERROR("Failed to compile shader: {}", filepath.string());
            return false;
        }

        // store compile result into memory
        outSpvMap[stage] = std::move(spv);
    }

    return true;
}


std::optional<GLSLProcessor::stage2spirv_t> GLSLProcessor::process(const ShaderDesc& ci)
{
    const auto cacheKey = ci.cacheKey();
    YA_CORE_ASSERT(!cacheKey.empty(), "ShaderDesc cache key cannot be empty");

    stage2spirv_t ret;

    auto compileStageFromFile = [&](EShaderStage::T stage, const std::string& stagePath, const char* errorTag) -> bool {
        std::string stageSource;
        if (!VirtualFileSystem::get()->readFileToString(stagePath, stageSource)) {
            YA_CORE_ERROR("Failed to read {} shader source: {}", errorTag, stagePath);
            return false;
        }

        std::vector<ir_t> spv;
        if (!compileToSpv(stagePath, stageSource, stage, ci.defines, spv)) {
            YA_CORE_ERROR("Failed to compile {} shader stage: {}", errorTag, stagePath);
            return false;
        }

        ret[stage] = std::move(spv);
        return true;
    };

    auto hasRequiredGraphicsStages = [&]() {
        return ret.contains(EShaderStage::Vertex) && ret.contains(EShaderStage::Fragment);
    };

    if (ci.sourceMode == ShaderDesc::ESourceMode::StageFiles)
    {
        for (const auto& stageFile : ci.stageFiles)
        {
            if (ret.contains(stageFile.stage)) {
                YA_CORE_ERROR("Duplicate stage entry in ShaderDesc::stageFiles, stage={}", static_cast<int>(stageFile.stage));
                return {};
            }

            std::filesystem::path stagePath(stageFile.file);
            if (!stagePath.is_absolute()) {
                stagePath = stdpath(shaderStoragePath) / stagePath;
            }

            const auto stagePathStr = stagePath.generic_string();
            if (!compileStageFromFile(stageFile.stage, stagePathStr, "explicit stage-file")) {
                return {};
            }
        }

        if (!hasRequiredGraphicsStages()) {
            YA_CORE_ERROR("Explicit stage-files mode requires at least vertex and fragment stages: {}", cacheKey);
            return {};
        }

        curFileName = cacheKey;
        curFilePath = stdpath(shaderStoragePath) / cacheKey;
        YA_CORE_INFO("Preprocessed explicit stage-files shader for {}: {} stages found", cacheKey, ret.size());
    }
    else
    {
        std::string shaderName = ci.shaderName;
        YA_CORE_ASSERT(!shaderName.empty(), "SingleShader mode requires shaderName");
        if (!shaderName.ends_with(".glsl")) {
            shaderName += ".glsl";
        }

        curFileName = ut::str::replace(shaderName, ".glsl", "");
        curFilePath = stdpath(shaderStoragePath) / shaderName;

        // 1. detect file changed unimplemented for now
        // TODO: use hash to detect shader source change, and output to "xxx.cached.vert.spv" or "xxxx.cached.frag.spv" and a "xxx.metadata" file

        // 2. find the "xxx.cached.vert.spv" or "xxxx.cached.frag.spv" file
        // auto vertFile = std::format("{}/{}.{}", this->intermediateStoragePath, filename, EShaderStage::getSpvOutputExtension(EShaderStage::Vertex));
        // auto fragFile = std::format("{}/{}.{}", this->intermediateStoragePath, filename, EShaderStage::getSpvOutputExtension(EShaderStage::Fragment));
        // if (VirtualFileSystem::get()->isFileExists(vertFile) && VirtualFileSystem::get()->isFileExists(fragFile))
        // {
        //     if (processSpvFiles(vertFile, fragFile, ret)) {
        //         return std::move(ret);
        //     }
        // }


        // TODO: use a config file as options to remap to different single shader file
        // if (ci.shaderName.ends_with(".ya.shader")) {
        //     try {
        //         auto j    = nlohmann::json::parse(ci.shaderName);
        //         auto type = j["type"];
        //         if (type.is_string() && type.get<std::string>() == "splits") {
        //             // load from  different single shader file
        //             auto vertFile = j["vertex"];
        //             auto fragFile = j["fragment"];
        //             auto geomFile = j["geometry"];
        //         }
        //     }
        //     catch (const std::exception& e)
        //     {
        //         YA_CORE_ERROR("Failed to parse shader config file: {}", e.what());
        //         return {};
        //     }
        // }


        ret.clear();

        // SingleShader mode only supports combined source with #type sections.
        if (processCombinedSource(curFilePath, ci.defines, ret)) {
            YA_CORE_INFO("Preprocessed shader source for {}: {} stages found", shaderName, ret.size());
        }
        else {
            YA_CORE_ERROR("Failed to preprocess shader source: {}", shaderName);
            return {};
        }

        if (!hasRequiredGraphicsStages()) {
            // Allow compute-only shaders (single compute stage, no vertex/fragment)
            if (!ret.contains(EShaderStage::Compute) || ret.size() != 1) {
                YA_CORE_ERROR("SingleShader mode requires at least vertex and fragment stages: {}", shaderName);
                return {};
            }
        }
    }

    // Validate SPIR-V magic number
    for (const auto& [stage, spirv] : ret) {
        if (spirv.empty() || spirv[0] != 0x07230203) {
            YA_CORE_ERROR("Invalid SPIR-V module for stage {}: Missing or incorrect magic number.", EShaderStage::T2Strings[stage]);
            YA_CORE_ASSERT(false, "SPIR-V validation failed for stage {}", EShaderStage::T2Strings[stage]);
        }
    }



    return {std::move(ret)};
}

// ============================================================
// SlangProcessor implementation
// ============================================================

// ---------------------------------------------------------------------------
// VFS-backed ISlangFileSystem so Slang can resolve #include / import via VFS
// ---------------------------------------------------------------------------
struct SlangVfsFileSystem : public ISlangFileSystem
{
    static constexpr std::string_view kSlangBaseDir = "Engine/Shader/Slang";

    virtual ~SlangVfsFileSystem() = default;
    // ISlangUnknown
    uint32_t addRef() override { return ++_refCount; }
    uint32_t release() override
    {
        uint32_t rc = --_refCount;
        if (rc == 0) delete this;
        return rc;
    }
    SlangResult queryInterface(const SlangUUID& uuid, void** outObject) override
    {
        if (uuid == ISlangFileSystem::getTypeGuid() ||
            uuid == ISlangUnknown::getTypeGuid())
        {
            addRef();
            *outObject = static_cast<ISlangFileSystem*>(this);
            return SLANG_OK;
        }
        *outObject = nullptr;
        return SLANG_E_NO_INTERFACE;
    }

    // ISlangCastable
    void* castAs(const SlangUUID& guid) override
    {
        if (guid == ISlangFileSystem::getTypeGuid() ||
            guid == ISlangUnknown::getTypeGuid())
        {
            return static_cast<ISlangFileSystem*>(this);
        }
        return nullptr;
    }

    // ISlangFileSystem
    SlangResult loadFile(const char* path, ISlangBlob** outBlob) override
    {
        std::string content;
        if (!VFS::get()->isFileExists(path)) {
            return SLANG_E_NOT_FOUND;
        }
        if (!VirtualFileSystem::get()->readFileToString(path, content))
        {
            YA_CORE_ERROR("[SlangVFS] Failed to load: {}", path);
            throw "1";
            return SLANG_E_NOT_FOUND;
        }

        // Wrap content in a simple blob
        struct StringBlob : public ISlangBlob
        {
            std::string           data;
            std::atomic<uint32_t> rc{1};

            uint32_t addRef() override { return ++rc; }
            uint32_t release() override
            {
                uint32_t r = --rc;
                if (r == 0) delete this;
                return r;
            }
            SlangResult queryInterface(const SlangUUID& uuid, void** out) override
            {
                if (uuid == ISlangBlob::getTypeGuid() || uuid == ISlangUnknown::getTypeGuid())
                {
                    addRef();
                    *out = static_cast<ISlangBlob*>(this);
                    return SLANG_OK;
                }
                *out = nullptr;
                return SLANG_E_NO_INTERFACE;
            }
            const void* getBufferPointer() override { return data.data(); }
            size_t      getBufferSize() override { return data.size(); }
        };

        auto* blob = new StringBlob();
        blob->data = std::move(content);
        *outBlob   = blob;
        return SLANG_OK;
    }

  private:
    std::atomic<uint32_t> _refCount{1};
};

// ---------------------------------------------------------------------------
// Map EShaderStage to Slang stage enum
// ---------------------------------------------------------------------------
static SlangStage toSlangStage(EShaderStage::T stage)
{
    switch (stage)
    {
    case EShaderStage::Vertex:
        return SLANG_STAGE_VERTEX;
    case EShaderStage::Fragment:
        return SLANG_STAGE_FRAGMENT;
    case EShaderStage::Geometry:
        return SLANG_STAGE_GEOMETRY;
    case EShaderStage::Compute:
        return SLANG_STAGE_COMPUTE;
    default:
        YA_CORE_ASSERT(false, "Unknown shader stage");
        return SLANG_STAGE_NONE;
    }
}

// ---------------------------------------------------------------------------
// SlangProcessor::compileToSpv
// ---------------------------------------------------------------------------
bool SlangProcessor::compileToSpv(std::string_view                source,
                                  std::string_view                filePath,
                                  std::string_view                entryName,
                                  EShaderStage::T                 stage,
                                  const std::vector<std::string>& defines,
                                  std::vector<ir_t>&              outSpv)
{
    // 1. Create global session (one per call is fine for now; can be cached later)
    Slang::ComPtr<slang::IGlobalSession> globalSession;
    if (SLANG_FAILED(slang::createGlobalSession(globalSession.writeRef())))
    {
        YA_CORE_ERROR("[Slang] Failed to create global session");
        return false;
    }

    // 2. Build session descriptor: SPIR-V target
    slang::TargetDesc targetDesc{};
    targetDesc.format  = SLANG_SPIRV;
    targetDesc.profile = globalSession->findProfile("spirv_1_3");

    // Build preprocessor macros
    std::vector<slang::PreprocessorMacroDesc> macros;
    macros.reserve(defines.size() + 1);
    // Always define YA_PLATFORM_VULKAN
    macros.push_back({"YA_PLATFORM_VULKAN", "1"});
    // User-supplied defines (format: "NAME" or "NAME=VALUE")
    std::vector<std::pair<std::string, std::string>> macroStorage;
    macroStorage.reserve(defines.size());
    for (const auto& def : defines)
    {
        auto eq = def.find('=');
        if (eq != std::string::npos)
        {
            macroStorage.push_back({def.substr(0, eq), def.substr(eq + 1)});
        }
        else
        {
            macroStorage.push_back({def, "1"});
        }
        macros.push_back({
            .name  = macroStorage.back().first.c_str(),
            .value = macroStorage.back().second.c_str(),
        });
    }

    // VFS-backed file system for #include / import resolution
    auto* slangVfs = new SlangVfsFileSystem();

    // Search paths: shader storage root + slang root fallback
    std::string searchPath0   = shaderStoragePath.string();
    std::string searchPath1   = "Engine/Shader/Slang";
    const char* searchPaths[] = {
        searchPath0.c_str(),
        searchPath1.c_str(),
    };

    slang::SessionDesc sessionDesc{};
    sessionDesc.targets                 = &targetDesc;
    sessionDesc.targetCount             = 1;
    sessionDesc.preprocessorMacros      = macros.data();
    sessionDesc.preprocessorMacroCount  = static_cast<SlangInt>(macros.size());
    sessionDesc.fileSystem              = slangVfs;
    sessionDesc.searchPaths             = searchPaths;
    sessionDesc.searchPathCount         = static_cast<SlangInt>(sizeof(searchPaths) / sizeof(searchPaths[0]));
    sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

    Slang::ComPtr<slang::ISession> session;
    if (SLANG_FAILED(globalSession->createSession(sessionDesc, session.writeRef())))
    {
        YA_CORE_ERROR("[Slang] Failed to create session for: {}", filePath);
        slangVfs->release();
        return false;
    }
    slangVfs->release(); // session holds a ref

    // 3. Load module from source string
    Slang::ComPtr<slang::IBlob> diagBlob;
    Slang::ComPtr<slang::IBlob> sourceBlob;

    // Wrap source in a blob
    struct SourceBlob : public ISlangBlob
    {
        virtual ~SourceBlob() = default;
        std::string           data;
        std::atomic<uint32_t> rc{1};
        uint32_t              addRef() override { return ++rc; }
        uint32_t              release() override
        {
            uint32_t r = --rc;
            if (r == 0) delete this;
            return r;
        }
        SlangResult queryInterface(const SlangUUID& uuid, void** out) override
        {
            if (uuid == ISlangBlob::getTypeGuid() || uuid == ISlangUnknown::getTypeGuid())
            {
                addRef();
                *out = static_cast<ISlangBlob*>(this);
                return SLANG_OK;
            }
            *out = nullptr;
            return SLANG_E_NO_INTERFACE;
        }
        const void* getBufferPointer() override { return data.data(); }
        size_t      getBufferSize() override { return data.size(); }
    };
    auto* srcBlob = new SourceBlob();
    srcBlob->data = std::string(source);

    // Module name derived from file path (without extension)
    std::string moduleName = std::filesystem::path(filePath).stem().string();

    Slang::ComPtr<slang::IModule> slangModule;
    slangModule = session->loadModuleFromSource(
        moduleName.c_str(),
        std::string(filePath).c_str(),
        srcBlob,
        diagBlob.writeRef());
    srcBlob->release();

    if (diagBlob && diagBlob->getBufferSize() > 0)
    {
        std::string_view diagStr(static_cast<const char*>(diagBlob->getBufferPointer()),
                                 diagBlob->getBufferSize());
        YA_CORE_WARN("[Slang] Diagnostics for {}:\n{}", filePath, diagStr);
    }

    if (!slangModule)
    {
        YA_CORE_ERROR("[Slang] Failed to load module: {}", filePath);
        return false;
    }

    // 4. Find entry point
    Slang::ComPtr<slang::IEntryPoint> entryPoint;
    if (SLANG_FAILED(slangModule->findEntryPointByName(std::string(entryName).c_str(), entryPoint.writeRef())))
    {
        YA_CORE_ERROR("[Slang] Entry point '{}' not found in: {}", entryName, filePath);
        return false;
    }

    // 5. Link: compose module + entry point
    slang::IComponentType*               components[] = {slangModule, entryPoint};
    Slang::ComPtr<slang::IComponentType> composedProgram;
    Slang::ComPtr<slang::IBlob>          linkDiag;
    if (SLANG_FAILED(session->createCompositeComponentType(
            components, 2, composedProgram.writeRef(), linkDiag.writeRef())))
    {
        if (linkDiag && linkDiag->getBufferSize() > 0)
        {
            std::string_view d(static_cast<const char*>(linkDiag->getBufferPointer()),
                               linkDiag->getBufferSize());
            YA_CORE_ERROR("[Slang] Link error for {}:\n{}", filePath, d);
        }
        return false;
    }

    // 6. Get SPIR-V code for entry point 0, target 0
    Slang::ComPtr<slang::IBlob> spvBlob;
    Slang::ComPtr<slang::IBlob> codeDiag;
    if (SLANG_FAILED(composedProgram->getEntryPointCode(0, 0, spvBlob.writeRef(), codeDiag.writeRef())))
    {
        if (codeDiag && codeDiag->getBufferSize() > 0)
        {
            std::string_view d(static_cast<const char*>(codeDiag->getBufferPointer()),
                               codeDiag->getBufferSize());
            YA_CORE_ERROR("[Slang] Code gen error for {}:\n{}", filePath, d);
        }
        return false;
    }

    // 7. Copy SPIR-V words into outSpv
    const size_t byteSize = spvBlob->getBufferSize();
    if (byteSize == 0 || byteSize % sizeof(ir_t) != 0)
    {
        YA_CORE_ERROR("[Slang] Invalid SPIR-V output size ({} bytes) for: {}", byteSize, filePath);
        return false;
    }
    outSpv.resize(byteSize / sizeof(ir_t));
    std::memcpy(outSpv.data(), spvBlob->getBufferPointer(), byteSize);

    return true;
}

// ---------------------------------------------------------------------------
// SlangProcessor::process
// ---------------------------------------------------------------------------
std::optional<SlangProcessor::stage2spirv_t> SlangProcessor::process(const ShaderDesc& ci)
{
    const auto cacheKey = ci.cacheKey();
    YA_CORE_ASSERT(!cacheKey.empty(), "ShaderDesc cache key cannot be empty");

    stage2spirv_t ret;

    // Helper: read source from VFS and compile one stage
    auto compileStage = [&](EShaderStage::T    stage,
                            const std::string& stagePath,
                            const std::string& entryName) -> bool {
        std::string source;
        if (!VirtualFileSystem::get()->readFileToString(stagePath, source))
        {
            YA_CORE_ERROR("[Slang] Failed to read shader source: {}", stagePath);
            return false;
        }

        std::vector<ir_t> spv;
        if (!compileToSpv(source, stagePath, entryName, stage, ci.defines, spv))
        {
            YA_CORE_ERROR("[Slang] Failed to compile stage {} of: {}", EShaderStage::T2Strings[stage], stagePath);
            return false;
        }

        ret[stage] = std::move(spv);
        return true;
    };

    if (ci.sourceMode == ShaderDesc::ESourceMode::StageFiles)
    {
        // Each StageFile specifies an explicit source file.
        // Entry point convention: "vertMain" / "fragMain" / "geomMain"
        static const std::unordered_map<EShaderStage::T, std::string> stageEntryNames = {
            {EShaderStage::Vertex, "vertMain"},
            {EShaderStage::Fragment, "fragMain"},
            {EShaderStage::Geometry, "geomMain"},
            {EShaderStage::Compute, "compMain"},
        };

        for (const auto& sf : ci.stageFiles)
        {
            if (ret.contains(sf.stage))
            {
                YA_CORE_ERROR("[Slang] Duplicate stage in stageFiles, stage={}", static_cast<int>(sf.stage));
                return {};
            }

            std::filesystem::path stagePath(sf.file);
            if (!stagePath.is_absolute())
                stagePath = stdpath(shaderStoragePath) / stagePath;

            auto              entryIt   = stageEntryNames.find(sf.stage);
            const std::string entryName = (entryIt != stageEntryNames.end()) ? entryIt->second : "main";

            if (!compileStage(sf.stage, stagePath.generic_string(), entryName))
                return {};
        }

        if (!ret.contains(EShaderStage::Vertex) || !ret.contains(EShaderStage::Fragment))
        {
            YA_CORE_ERROR("[Slang] StageFiles mode requires at least vertex and fragment stages: {}", cacheKey);
            return {};
        }

        curFileName = cacheKey;
        curFilePath = stdpath(shaderStoragePath) / cacheKey;
        YA_CORE_INFO("[Slang] Compiled {} stages for: {}", ret.size(), cacheKey);
    }
    else
    {
        // SingleShader mode: one .slang file with both vertMain and fragMain
        std::string shaderName = ci.shaderName;
        YA_CORE_ASSERT(!shaderName.empty(), "SingleShader mode requires shaderName");
        if (!shaderName.ends_with(".slang"))
            shaderName += ".slang";

        curFileName = ut::str::replace(shaderName, ".slang", "");
        curFilePath = stdpath(shaderStoragePath) / shaderName;

        std::string source;
        if (!VirtualFileSystem::get()->readFileToString(curFilePath.generic_string(), source))
        {
            YA_CORE_ERROR("[Slang] Failed to read shader: {}", curFilePath.generic_string());
            return {};
        }

        // Compile vertex stage
        {
            std::vector<ir_t> spv;
            if (!compileToSpv(source, curFilePath.generic_string(), "vertMain", EShaderStage::Vertex, ci.defines, spv))
            {
                YA_CORE_ERROR("[Slang] Failed to compile vertex stage: {}", shaderName);
                return {};
            }
            ret[EShaderStage::Vertex] = std::move(spv);
        }

        // Compile fragment stage
        {
            std::vector<ir_t> spv;
            if (!compileToSpv(source, curFilePath.generic_string(), "fragMain", EShaderStage::Fragment, ci.defines, spv))
            {
                YA_CORE_ERROR("[Slang] Failed to compile fragment stage: {}", shaderName);
                return {};
            }
            ret[EShaderStage::Fragment] = std::move(spv);
        }

        YA_CORE_INFO("[Slang] Compiled {} stages for: {}", ret.size(), shaderName);
    }

    // Validate SPIR-V magic number
    for (const auto& [stage, spirv] : ret)
    {
        if (spirv.empty() || spirv[0] != 0x07230203)
        {
            YA_CORE_ERROR("[Slang] Invalid SPIR-V magic for stage {}: {}",
                          EShaderStage::T2Strings[stage],
                          cacheKey);
            YA_CORE_ASSERT(false, "SPIR-V validation failed");
        }
    }

    return {std::move(ret)};
}

// ============================================================
// ShaderReflection::merge — combine per-stage reflection into
// a single pipeline-layout description
// ============================================================
ShaderReflection::MergedResources ShaderReflection::merge(
    const std::vector<ShaderReflection::ShaderResources>& stageResources)
{
    MergedResources result;

    // -----------------------------------------------------------
    // 1. Push Constants: merge across stages (union stageFlags,
    //    use max size across all stages).
    // -----------------------------------------------------------
    uint32_t        pcMaxSize   = 0;
    EShaderStage::T pcStageFlags = static_cast<EShaderStage::T>(0);
    bool            hasPushConstant = false;

    for (const auto& res : stageResources) {
        for (const auto& pc : res.pushConstantBuffers) {
            hasPushConstant = true;
            pcMaxSize       = std::max(pcMaxSize, pc.size);
            pcStageFlags    = pcStageFlags | res.stage;
        }
    }

    if (hasPushConstant) {
        result.pushConstants.push_back(PushConstantRange{
            .offset     = 0,
            .size       = pcMaxSize,
            .stageFlags = pcStageFlags,
        });
    }

    // -----------------------------------------------------------
    // 2. Descriptor Set Layouts: merge uniform buffers and sampled
    //    images by (set, binding), merging stageFlags when the
    //    same binding appears in multiple stages.
    // -----------------------------------------------------------
    // Key: (set, binding) → accumulated binding info
    struct BindingKey
    {
        uint32_t set;
        uint32_t binding;
        bool operator<(const BindingKey& o) const
        {
            return set < o.set || (set == o.set && binding < o.binding);
        }
    };
    struct BindingInfo
    {
        EPipelineDescriptorType::T type;
        uint32_t                   descriptorCount;
        EShaderStage::T            stageFlags;
        std::string                name;
    };
    std::map<BindingKey, BindingInfo> bindingMap;

    auto mergeBinding = [&](uint32_t set, uint32_t binding,
                            EPipelineDescriptorType::T descType,
                            uint32_t                   count,
                            EShaderStage::T            stage,
                            const std::string&         name) {
        BindingKey key{set, binding};
        auto       it = bindingMap.find(key);
        if (it == bindingMap.end()) {
            bindingMap[key] = BindingInfo{descType, count, stage, name};
        }
        else {
            // Validate type consistency
            if (it->second.type != descType) {
                YA_CORE_ERROR("ShaderReflection::merge: binding (set={}, binding={}) "
                              "has conflicting types: {} vs {}",
                              set, binding,
                              static_cast<int>(it->second.type),
                              static_cast<int>(descType));
            }
            it->second.stageFlags    = it->second.stageFlags | stage;
            it->second.descriptorCount = std::max(it->second.descriptorCount, count);
        }
    };

    for (const auto& res : stageResources) {
        // Uniform buffers → UniformBuffer descriptor type
        for (const auto& ubo : res.uniformBuffers) {
            mergeBinding(ubo.set, ubo.binding,
                         EPipelineDescriptorType::UniformBuffer,
                         1, res.stage, ubo.name);
        }
        // Sampled images → CombinedImageSampler descriptor type
        for (const auto& img : res.sampledImages) {
            mergeBinding(img.set, img.binding,
                         EPipelineDescriptorType::CombinedImageSampler,
                         img.arraySize, res.stage, img.name);
        }
    }

    // Group bindings by set index and build DescriptorSetLayoutDesc
    std::map<uint32_t, DescriptorSetLayoutDesc> setMap;
    for (const auto& [key, info] : bindingMap) {
        auto& dsl = setMap[key.set];
        dsl.set   = static_cast<int32_t>(key.set);
        dsl.label = std::format("AutoDSL_Set{}", key.set);
        dsl.bindings.push_back(DescriptorSetLayoutBinding{
            .binding         = key.binding,
            .descriptorType  = info.type,
            .descriptorCount = info.descriptorCount,
            .stageFlags      = info.stageFlags,
        });
    }

    // Sort by set index and produce final vector
    for (auto& [setIdx, dsl] : setMap) {
        // Sort bindings within each set
        std::ranges::sort(dsl.bindings, [](const auto& a, const auto& b) {
            return a.binding < b.binding;
        });
        result.descriptorSetLayouts.push_back(std::move(dsl));
    }

    // -----------------------------------------------------------
    // 3. Vertex Inputs: extract from the vertex stage
    // -----------------------------------------------------------
    for (const auto& res : stageResources) {
        if (res.stage == EShaderStage::Vertex) {
            result.vertexInputs = res.inputs;
            break;
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// SlangProcessor::reflect  — reuse GLSLProcessor's SPIRV-Cross based reflect
// ---------------------------------------------------------------------------
ShaderReflection::ShaderResources SlangProcessor::reflect(EShaderStage::T          stage,
                                                          const std::vector<ir_t>& spirvData)
{
    // Delegate to the same SPIRV-Cross reflection logic used by GLSLProcessor.
    GLSLProcessor glslProc;
    glslProc.curFileName = curFileName;
    glslProc.curFilePath = curFilePath;
    return glslProc.reflect(stage, spirvData);
}

} // namespace ya