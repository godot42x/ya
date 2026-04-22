#include "Render/Shader.h"

#include <algorithm>
#include <format>
#include <map>

namespace ya
{

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
        size = sizeof(uint32_t) * type.vecsize * type.columns;
        break;
    default:
        break;
    }

    return size;
}

uint32_t SPIRVHelper::getVertexAlignedOffset(uint32_t currentOffset, const spirv_cross::SPIRType& type)
{
    (void)type;
    constexpr uint32_t alignment = 4;
    return (currentOffset + alignment) % alignment == 0 ? currentOffset : currentOffset + alignment;
}

namespace ShaderReflection
{

DataType SpirType2DataType(const spirv_cross::SPIRType& type)
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
            if (type.columns == 3 && type.vecsize == 3) {
                return DataType::Mat3;
            }
            if (type.columns == 4 && type.vecsize == 4) {
                return DataType::Mat4;
            }
        }
        else {
            if (type.vecsize == 1) {
                return DataType::Float;
            }
            if (type.vecsize == 2) {
                return DataType::Vec2;
            }
            if (type.vecsize == 3) {
                return DataType::Vec3;
            }
            if (type.vecsize == 4) {
                return DataType::Vec4;
            }
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
        return sizeof(uint32_t);
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
        return sizeof(uint32_t);
    default:
        return 0;
    }
}

ShaderResources reflectSpirvCross(EShaderStage::T stage, const std::vector<uint32_t>& spirvData, std::string_view debugName)
{
    std::vector<uint32_t>        spirvIr(spirvData.begin(), spirvData.end());
    spirv_cross::Compiler        compiler(spirvIr);
    spirv_cross::ShaderResources spirvResources = compiler.get_shader_resources();

    ShaderResources resources;
    resources.stage          = stage;
    resources.spirvResources = spirvResources;

    YA_CORE_TRACE("===============================================================================");
    YA_CORE_TRACE("ShaderReflection:reflectSpirvCross {} -> {}", debugName, EShaderStage::T2Strings[stage]);
    YA_CORE_TRACE("\t {} uniform buffers ", spirvResources.uniform_buffers.size());
    YA_CORE_TRACE("\t {} storage buffers ", spirvResources.storage_buffers.size());
    YA_CORE_TRACE("\t {} stage inputs ", spirvResources.stage_inputs.size());
    YA_CORE_TRACE("\t {} stage outputs ", spirvResources.stage_outputs.size());
    YA_CORE_TRACE("\t {} storage images ", spirvResources.storage_images.size());
    YA_CORE_TRACE("\t {} resources ", spirvResources.sampled_images.size());

    YA_CORE_TRACE("Stage Inputs (with alignment information):");
    uint32_t structOffset = 0;
    for (const auto& input : spirvResources.stage_inputs) {
        uint32_t location = compiler.get_decoration(input.id, spv::DecorationLocation);
        uint32_t offset   = compiler.get_decoration(input.id, spv::DecorationOffset);

        const spirv_cross::SPIRType& type = compiler.get_type(input.type_id);
        uint32_t alignedOffset = SPIRVHelper::getVertexAlignedOffset(structOffset, type);
        uint32_t typeSize      = SPIRVHelper::getSpirvTypeSize(type);
        structOffset           = alignedOffset + typeSize;

        StageIOData inputData{};
        inputData.name     = input.name;
        inputData.type     = SpirType2DataType(type);
        inputData.location = location;
        inputData.offset   = alignedOffset;
        inputData.size     = typeSize;
        inputData.vecsize  = type.vecsize;
        inputData.basetype = static_cast<uint32_t>(type.basetype);
        resources.inputs.push_back(inputData);

        YA_CORE_TRACE("\t(name: {0}, location: {1}, shader offset: {2}, aligned offset: {3}, size: {4}, type: {5}, {6}",
                      input.name,
                      location,
                      offset,
                      alignedOffset,
                      typeSize,
                      DataType2Strings[inputData.type],
                      type);
    }

    YA_CORE_TRACE("Stage Outputs:");
    for (const auto& output : spirvResources.stage_outputs) {
        uint32_t                     location = compiler.get_decoration(output.id, spv::DecorationLocation);
        uint32_t                     offset   = compiler.get_decoration(output.id, spv::DecorationOffset);
        const spirv_cross::SPIRType& type     = compiler.get_type(output.type_id);

        StageIOData outputData{};
        outputData.name     = output.name;
        outputData.type     = SpirType2DataType(type);
        outputData.location = location;
        outputData.offset   = offset;
        outputData.size     = SPIRVHelper::getSpirvTypeSize(type);
        outputData.vecsize  = type.vecsize;
        outputData.basetype = static_cast<uint32_t>(type.basetype);
        resources.outputs.push_back(outputData);

        YA_CORE_TRACE("\t(name: {0}, location: {1}, offset: {2}, type: {3})", output.name, location, offset, DataType2Strings[outputData.type]);
    }

    YA_CORE_TRACE("Uniform buffers:");
    for (const auto& resource : spirvResources.uniform_buffers) {
        const auto& bufferType  = compiler.get_type(resource.base_type_id);
        uint32_t    bufferSize  = compiler.get_declared_struct_size(bufferType);
        uint32_t    binding     = compiler.get_decoration(resource.id, spv::DecorationBinding);
        int         memberCount = static_cast<int>(bufferType.member_types.size());
        uint32_t    set         = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);

        UniformBuffer uniformBuffer;
        uniformBuffer.name    = resource.name;
        uniformBuffer.binding = binding;
        uniformBuffer.set     = set;
        uniformBuffer.size    = bufferSize;

        YA_CORE_TRACE("Buffer Name:  {0}", resource.name);
        YA_CORE_TRACE("\tSize = {0}", bufferSize);
        YA_CORE_TRACE("\tBinding = {0}", binding);
        YA_CORE_TRACE("\tSet = {0}", set);
        YA_CORE_TRACE("\tMembers = {0}", memberCount);
        YA_CORE_TRACE("\tMembers:");

        for (int memberIndex = 0; memberIndex < memberCount; ++memberIndex) {
            const std::string memberName   = compiler.get_member_name(bufferType.self, memberIndex);
            const auto&       memberType   = compiler.get_type(bufferType.member_types[memberIndex]);
            uint32_t          memberOffset = compiler.type_struct_member_offset(bufferType, memberIndex);
            uint32_t          memberSize   = compiler.get_declared_struct_member_size(bufferType, memberIndex);

            UniformBufferMember member{};
            member.name   = memberName;
            member.type   = SpirType2DataType(memberType);
            member.offset = memberOffset;
            member.size   = memberSize;
            uniformBuffer.members.push_back(member);

            YA_CORE_TRACE("\t\t-Member {0} (offset: {1}, size: {2}, type: {3})",
                          memberName,
                          memberOffset,
                          memberSize,
                          DataType2Strings[member.type]);
        }

        resources.uniformBuffers.push_back(uniformBuffer);
    }

    YA_CORE_TRACE("Sampled images:");
    for (const auto& resource : spirvResources.sampled_images) {
        uint32_t    binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
        uint32_t    set     = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
        const auto& type    = compiler.get_type(resource.type_id);

        uint32_t arraySize = 1;
        if (!type.array.empty()) {
            arraySize = type.array[0];
            if (arraySize == 0) {
                arraySize = 1;
            }
        }

        Resource sampledImage{};
        sampledImage.name      = resource.name;
        sampledImage.binding   = binding;
        sampledImage.set       = set;
        sampledImage.type      = SpirType2DataType(type);
        sampledImage.arraySize = arraySize;
        resources.sampledImages.push_back(sampledImage);

        YA_CORE_TRACE("\tSampled Image: {0} (binding: {1}, set: {2}, type: {3}, arraySize: {4})", resource.name, binding, set, DataType2Strings[sampledImage.type], arraySize);
    }

    YA_CORE_TRACE("Push constant buffers:");
    for (const auto& resource : spirvResources.push_constant_buffers) {
        const auto& bufferType = compiler.get_type(resource.base_type_id);
        uint32_t    bufferSize = compiler.get_declared_struct_size(bufferType);

        PushConstantBuffer pcBuffer{};
        pcBuffer.name = resource.name;
        pcBuffer.size = bufferSize;
        resources.pushConstantBuffers.push_back(pcBuffer);

        YA_CORE_TRACE("\tPush Constant: {0} (size: {1})", resource.name, bufferSize);
    }

    return resources;
}

MergedResources merge(const std::vector<ShaderResources>& stageResources)
{
    MergedResources result;

    uint32_t        pcMaxSize       = 0;
    EShaderStage::T pcStageFlags    = static_cast<EShaderStage::T>(0);
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

    struct BindingKey
    {
        uint32_t set;
        uint32_t binding;

        bool operator<(const BindingKey& other) const
        {
            return set < other.set || (set == other.set && binding < other.binding);
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
    auto mergeBinding = [&](uint32_t set, uint32_t binding, EPipelineDescriptorType::T descType, uint32_t count, EShaderStage::T stage, const std::string& name) {
        BindingKey key{set, binding};
        auto       it = bindingMap.find(key);
        if (it == bindingMap.end()) {
            bindingMap[key] = BindingInfo{descType, count, stage, name};
        }
        else {
            if (it->second.type != descType) {
                YA_CORE_ERROR("ShaderReflection::merge: binding (set={}, binding={}) has conflicting types: {} vs {}",
                              set,
                              binding,
                              static_cast<int>(it->second.type),
                              static_cast<int>(descType));
            }
            it->second.stageFlags      = it->second.stageFlags | stage;
            it->second.descriptorCount = std::max(it->second.descriptorCount, count);
        }
    };

    for (const auto& res : stageResources) {
        for (const auto& ubo : res.uniformBuffers) {
            mergeBinding(ubo.set, ubo.binding, EPipelineDescriptorType::UniformBuffer, 1, res.stage, ubo.name);
        }
        for (const auto& img : res.sampledImages) {
            mergeBinding(img.set, img.binding, EPipelineDescriptorType::CombinedImageSampler, img.arraySize, res.stage, img.name);
        }
    }

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

    for (auto& [setIndex, dsl] : setMap) {
        (void)setIndex;
        std::ranges::sort(dsl.bindings, [](const auto& lhs, const auto& rhs) {
            return lhs.binding < rhs.binding;
        });
        result.descriptorSetLayouts.push_back(std::move(dsl));
    }

    for (const auto& res : stageResources) {
        if (res.stage == EShaderStage::Vertex) {
            result.vertexInputs = res.inputs;
            break;
        }
    }

    return result;
}

} // namespace ShaderReflection

ShaderReflection::ShaderResources GLSLProcessor::reflect(EShaderStage::T stage, const std::vector<ir_t>& spirvData)
{
    return ShaderReflection::reflectSpirvCross(stage, spirvData, curFileName);
}

ShaderReflection::ShaderResources SlangProcessor::reflect(EShaderStage::T stage, const std::vector<ir_t>& spirvData)
{
    return ShaderReflection::reflectSpirvCross(stage, spirvData, curFileName);
}

} // namespace ya
