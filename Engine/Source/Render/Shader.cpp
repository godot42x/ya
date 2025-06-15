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

#include "utility.cc/string_utils.h"


#include <SDL3/SDL_gpu.h>


#include "Core/FileSystem/FileSystem.h"


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

namespace SPIRVHelper
{

// Helper function to convert SPIRV types to SDL_GPU vertex element formats
SDL_GPUVertexElementFormat spirvType2SDLFormat(const spirv_cross::SPIRType &type)
{
    // Check for alignment - we need to handle different data types
    switch (type.basetype)
    {
    case spirv_cross::SPIRType::Float:
        if (type.vecsize == 1 && type.columns == 1)
            return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
        else if (type.vecsize == 2 && type.columns == 1)
            return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        else if (type.vecsize == 3 && type.columns == 1)
            return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        else if (type.vecsize == 4 && type.columns == 1)
            return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        break;

    case spirv_cross::SPIRType::Int:
        if (type.vecsize == 1 && type.columns == 1)
            return SDL_GPU_VERTEXELEMENTFORMAT_INT;
        else if (type.vecsize == 2 && type.columns == 1)
            return SDL_GPU_VERTEXELEMENTFORMAT_INT2;
        else if (type.vecsize == 3 && type.columns == 1)
            return SDL_GPU_VERTEXELEMENTFORMAT_INT3;
        else if (type.vecsize == 4 && type.columns == 1)
            return SDL_GPU_VERTEXELEMENTFORMAT_INT4;
        break;

    case spirv_cross::SPIRType::UInt:
        if (type.vecsize == 1 && type.columns == 1)
            return SDL_GPU_VERTEXELEMENTFORMAT_UINT;
        else if (type.vecsize == 2 && type.columns == 1)
            return SDL_GPU_VERTEXELEMENTFORMAT_UINT2;
        else if (type.vecsize == 3 && type.columns == 1)
            return SDL_GPU_VERTEXELEMENTFORMAT_UINT3;
        else if (type.vecsize == 4 && type.columns == 1)
            return SDL_GPU_VERTEXELEMENTFORMAT_UINT4;
        break;

    default:
        break;
    }

    return SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
}

// Get size of a SPIRV type with proper alignment for C++ structs
uint32_t getSpirvTypeSize(const spirv_cross::SPIRType &type)
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
uint32_t getVertexAlignedOffset(uint32_t current_offset, const spirv_cross::SPIRType &type)
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
} // namespace SPIRVHelper

namespace ShaderReflection
{

DataType getSpirvBaseType(const spirv_cross::SPIRType &type)
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

SDL_GPUVertexElementFormat dataTypeToSDLFormat(DataType type)
{
    switch (type) {
    case DataType::Float:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
    case DataType::Vec2:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    case DataType::Vec3:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    case DataType::Vec4:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    case DataType::Int:
        return SDL_GPU_VERTEXELEMENTFORMAT_INT;
    case DataType::UInt:
        return SDL_GPU_VERTEXELEMENTFORMAT_UINT;
    default:
        return SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
    }
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

ShaderReflection::ShaderResources GLSLScriptProcessor::reflect(EShaderStage::T stage, const std::vector<ir_t> &spirvData)
{
    std::vector<uint32_t>        spirv_ir(spirvData.begin(), spirvData.end());
    spirv_cross::Compiler        compiler(spirv_ir);
    spirv_cross::ShaderResources spirvResources = compiler.get_shader_resources();

    // Create our custom shader resources structure
    ShaderReflection::ShaderResources resources;
    resources.stage          = stage;
    resources.spirvResources = spirvResources; // Store original spirv resources

    NE_CORE_TRACE("===============================================================================");
    NE_CORE_TRACE("OpenGLShader:Reflect {} -> {}", tempProcessingPath, EShaderStage::T2Strings[stage]);
    NE_CORE_TRACE("\t {} uniform buffers ", spirvResources.uniform_buffers.size());
    NE_CORE_TRACE("\t {} storage buffers ", spirvResources.storage_buffers.size());
    NE_CORE_TRACE("\t {} stage inputs ", spirvResources.stage_inputs.size());
    NE_CORE_TRACE("\t {} stage outputs ", spirvResources.stage_outputs.size());
    NE_CORE_TRACE("\t {} storage images ", spirvResources.storage_images.size());
    NE_CORE_TRACE("\t {} resources ", spirvResources.sampled_images.size());


    // Process stage inputs with alignment information
    NE_CORE_TRACE("Stage Inputs (with alignment information):");
    uint32_t   struct_offset      = 0;
    const bool IS_CPP_STRUCT_PACK = true;
    for (const auto &input : spirvResources.stage_inputs) {

        uint32_t location = compiler.get_decoration(input.id, spv::DecorationLocation);
        uint32_t offset   = compiler.get_decoration(input.id, spv::DecorationOffset);

        const auto &type = compiler.get_type(input.type_id);

        // Calculate aligned offset
        uint32_t aligned_offset = SPIRVHelper::getVertexAlignedOffset(struct_offset, type);
        uint32_t type_size      = SPIRVHelper::getSpirvTypeSize(type);
        struct_offset           = aligned_offset + type_size;

        SDL_GPUVertexElementFormat sdl_format = SPIRVHelper::spirvType2SDLFormat(type);

        // Create stage input data
        ShaderReflection::StageIOData inputData;
        inputData.name     = input.name;
        inputData.type     = ShaderReflection::getSpirvBaseType(type);
        inputData.location = location;
        inputData.offset   = aligned_offset;
        inputData.size     = type_size;
        inputData.format   = sdl_format;

        // Add to our resources
        resources.inputs.push_back(inputData);

        NE_CORE_TRACE("\t(name: {0}, location: {1}, shader offset: {2}, aligned offset: {3}, size: {4}, type: {5}, SDL format: {6})",
                      input.name,
                      location,
                      offset,
                      aligned_offset,
                      type_size,
                      ShaderReflection::DataType2Strings[inputData.type],
                      (int)sdl_format);
    }

    // Process stage outputs
    NE_CORE_TRACE("Stage Outputs:");
    for (const auto &output : spirvResources.stage_outputs) {
        uint32_t    location = compiler.get_decoration(output.id, spv::DecorationLocation);
        uint32_t    offset   = compiler.get_decoration(output.id, spv::DecorationOffset);
        const auto &type     = compiler.get_type(output.type_id);

        // Create stage output data
        ShaderReflection::StageIOData outputData;
        outputData.name     = output.name;
        outputData.type     = ShaderReflection::getSpirvBaseType(type);
        outputData.location = location;
        outputData.offset   = offset;
        outputData.size     = SPIRVHelper::getSpirvTypeSize(type);
        outputData.format   = SPIRVHelper::spirvType2SDLFormat(type);

        // Add to our resources
        resources.outputs.push_back(outputData);

        NE_CORE_TRACE("\t(name: {0}, location: {1}, offset: {2}, type: {3})", output.name, location, offset, ShaderReflection::DataType2Strings[outputData.type]);
    }

    // Process uniform buffers
    NE_CORE_TRACE("Uniform buffers:");
    for (const auto &resource : spirvResources.uniform_buffers) {
        const auto &buffer_type  = compiler.get_type(resource.base_type_id);
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

        NE_CORE_TRACE("Buffer Name:  {0}", resource.name);
        NE_CORE_TRACE("\tSize = {0}", bufferSize);
        NE_CORE_TRACE("\tBinding = {0}", binding);
        NE_CORE_TRACE("\tSet = {0}", set);
        NE_CORE_TRACE("\tMembers = {0}", member_count);

        // Process each member of the uniform buffer with alignment information
        NE_CORE_TRACE("\tMembers with alignment:");
        uint32_t struct_offset = 0;

        for (int i = 0; i < member_count; i++) {
            const std::string memberName   = compiler.get_member_name(buffer_type.self, i);
            const auto       &memberType   = compiler.get_type(buffer_type.member_types[i]);
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

            NE_CORE_TRACE("\t\t-Member {0} (shader offset: {1}, C++ aligned offset: {2}, size: {3}, type: {4})",
                          memberName,
                          memberOffset,
                          -1,
                          memberSize,
                          ShaderReflection::DataType2Strings[member.type]);

            // Check for alignment mismatches between shader and C++
            // if (memberOffset != aligned_offset) {
            //     NE_CORE_WARN("\t\t  ⚠️ ALIGNMENT MISMATCH: Shader offset {0} != C++ aligned offset {1} for member {2}",
            //                  memberOffset,
            //                  aligned_offset,
            //                  memberName);
            // }
        }

        // Add uniform buffer to resources
        resources.uniformBuffers.push_back(uniformBuffer);
    }

    // Process sampled images
    NE_CORE_TRACE("Sampled images:");
    for (const auto &resource : spirvResources.sampled_images) {
        uint32_t    binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
        uint32_t    set     = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
        const auto &type    = compiler.get_type(resource.type_id);

        // Create resource
        ShaderReflection::Resource sampledImage;
        sampledImage.name    = resource.name;
        sampledImage.binding = binding;
        sampledImage.set     = set;
        sampledImage.type    = ShaderReflection::getSpirvBaseType(type);

        // Add to resources
        resources.sampledImages.push_back(sampledImage);

        NE_CORE_TRACE("\tSampled Image: {0} (binding: {1}, set: {2}, type: {3})", resource.name, binding, set, ShaderReflection::DataType2Strings[sampledImage.type]);
    }

    return resources;
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
                std::format("{} ({})", fullPath, EShaderStage::T2Strings[stage]).c_str(),
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

    // Validate SPIR-V magic number
    for (auto &[stage, spirv] : ret) {
        if (spirv.empty() || spirv[0] != 0x07230203) {
            NE_CORE_ERROR("Invalid SPIR-V module for stage {}: Missing or incorrect magic number.", EShaderStage::T2Strings[stage]);
            NE_CORE_ASSERT(false, "SPIR-V validation failed for stage {}", EShaderStage::T2Strings[stage]);
        }
    }


    return {std::move(ret)};
}
