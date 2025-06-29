#pragma once

#include "Render/RenderManager.h"
#include <memory>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>


// Forward declarations
class GLSLScriptProcessor;
class VulkanResourceManager;

class VulkanPipeline
{
  private:
    std::shared_ptr<GLSLScriptProcessor> _shaderProcessor;

    VkDevice         m_logicalDevice  = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkExtent2D       m_extent;

    VkPipeline            m_pipeline            = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;

    // Descriptor and resource management
    VkDescriptorPool m_descriptorPool        = VK_NULL_HANDLE;
    VkDescriptorSet  m_descriptorSet         = VK_NULL_HANDLE;
    VkSampler        m_defaultTextureSampler = VK_NULL_HANDLE;

    // Texture management
    struct VulkanTexture2D
    {
        VkImage        image     = VK_NULL_HANDLE;
        VkImageView    imageView = VK_NULL_HANDLE;
        VkDeviceMemory memory    = VK_NULL_HANDLE;
    };
    std::vector<std::shared_ptr<VulkanTexture2D>> m_textures;

    int m_maxTextureSlots = -1;

  public:
    VulkanPipeline()  = default;
    ~VulkanPipeline() = default;

    void initialize(VkDevice logicalDevice, VkPhysicalDevice physicalDevice);

    void createGraphicsPipeline(const std::string &shaderPath, VkRenderPass renderPass, VkExtent2D extent)
    {
        m_extent = extent;

        // Create descriptor resources first
        createDescriptorSetLayout();
        createPipelineLayout();
        createDescriptorPool();
        createDescriptorSets();

        auto stage2Spirv_opt = _shaderProcessor->process(shaderPath);
        if (!stage2Spirv_opt.has_value()) {
            NE_CORE_ERROR("Failed to process shader: {}", shaderPath);
            return;
        }
        auto &stage2Spirv = stage2Spirv_opt.value();

        // Create shader modules
        auto vertShaderModule = createShaderModule(stage2Spirv[EShaderStage::Vertex]);
        auto fragShaderModule = createShaderModule(stage2Spirv[EShaderStage::Fragment]);


        VkPipelineShaderStageCreateInfo shaderStages[] = {
            VkPipelineShaderStageCreateInfo{
                .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage  = VK_SHADER_STAGE_VERTEX_BIT,
                .module = vertShaderModule,
                .pName  = "main",
            },
            VkPipelineShaderStageCreateInfo{
                .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = fragShaderModule,
                .pName  = "main",
            },
        };

        // Get vertex input info from shader reflection
        auto vertexReflectInfo = _shaderProcessor->reflect(EShaderStage::Vertex, stage2Spirv[EShaderStage::Vertex]);

        // Convert SPIR-V type to Vulkan format (simplified)
        auto spirvType2VulkanFormat = [](const auto &type) -> VkFormat {
            if (type.vecsize == 3 && type.basetype == 0) { // Float vec3
                return VK_FORMAT_R32G32B32_SFLOAT;
            }
            return VK_FORMAT_R32G32B32_SFLOAT; // Default fallback
        };

        std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;
        for (auto &input : vertexReflectInfo.inputs) {
            vertexAttributeDescriptions.push_back({
                .location = input.location,
                .binding  = 0,
                .format   = spirvType2VulkanFormat(input.format),
                .offset   = input.offset,
            });
        }

        VkVertexInputBindingDescription vertexBindingDescription{
            .binding   = 0,
            .stride    = vertexReflectInfo.inputs.empty() ? 0 : (vertexReflectInfo.inputs.back().offset + vertexReflectInfo.inputs.back().size),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{
            .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount   = vertexAttributeDescriptions.empty() ? 0u : 1u,
            .pVertexBindingDescriptions      = vertexAttributeDescriptions.empty() ? nullptr : &vertexBindingDescription,
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributeDescriptions.size()),
            .pVertexAttributeDescriptions    = vertexAttributeDescriptions.data(),
        };

        // Fixed-function state
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{
            .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE,
        };

        VkViewport viewport{
            .x        = 0.0f,
            .y        = 0.0f,
            .width    = (float)m_extent.width,
            .height   = (float)m_extent.height,
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };

        VkRect2D scissor{
            .offset = {0, 0},
            .extent = m_extent,
        };

        VkPipelineViewportStateCreateInfo viewportState{
            .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .pViewports    = &viewport,
            .scissorCount  = 1,
            .pScissors     = &scissor,
        };

        VkPipelineRasterizationStateCreateInfo rasterizer{
            .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .depthClampEnable        = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode             = VK_POLYGON_MODE_FILL,
            .cullMode                = VK_CULL_MODE_BACK_BIT,
            .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable         = VK_FALSE,
            .lineWidth               = 1.0f,
        };

        VkPipelineMultisampleStateCreateInfo multiSampling{
            .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable  = VK_FALSE,
        };

        VkPipelineColorBlendAttachmentState colorBlendAttachment{
            .blendEnable    = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        };

        VkPipelineColorBlendStateCreateInfo colorBlending{
            .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable   = VK_FALSE,
            .attachmentCount = 1,
            .pAttachments    = &colorBlendAttachment,
        };

        VkPipelineDepthStencilStateCreateInfo depthStencil{
            .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable       = VK_TRUE,
            .depthWriteEnable      = VK_TRUE,
            .depthCompareOp        = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable     = VK_FALSE,
        };

        // Configure dynamic states
        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR};

        VkPipelineDynamicStateCreateInfo dynamicState{
            .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates    = dynamicStates.data()};
        // Create pipeline
        VkGraphicsPipelineCreateInfo pipelineInfo{
            .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount          = 2,
            .pStages             = shaderStages,
            .pVertexInputState   = &vertexInputInfo,
            .pInputAssemblyState = &inputAssembly,
            .pViewportState      = &viewportState,
            .pRasterizationState = &rasterizer,
            .pMultisampleState   = &multiSampling,
            .pDepthStencilState  = &depthStencil,
            .pColorBlendState    = &colorBlending,
            .pDynamicState       = &dynamicState,
            .layout              = m_pipelineLayout,
            .renderPass          = renderPass,
            .subpass             = 0,
            .basePipelineHandle  = VK_NULL_HANDLE,
        };

        VkResult result = vkCreateGraphicsPipelines(m_logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);
        NE_CORE_ASSERT(result == VK_SUCCESS, "Failed to create graphics pipeline!");

        // Cleanup shader modules
        vkDestroyShaderModule(m_logicalDevice, fragShaderModule, nullptr);
        vkDestroyShaderModule(m_logicalDevice, vertShaderModule, nullptr);
    }

    void recreate(VkRenderPass renderPass, VkExtent2D extent);
    void cleanup();

    // Getters
    VkPipeline            getPipeline() const { return m_pipeline; }
    VkPipelineLayout      getPipelineLayout() const { return m_pipelineLayout; }
    VkDescriptorSetLayout getDescriptorSetLayout() const { return m_descriptorSetLayout; }

    // Descriptor management
    void bindDescriptorSets(VkCommandBuffer commandBuffer);
    void updateDescriptorSets();

    // Texture management
    void createTexture(const std::string &path);
    void createDefaultSampler();

  private:
    VkShaderModule createShaderModule(const std::vector<uint32_t> &spv_binary);
    void           createDescriptorSetLayout();
    void           createPipelineLayout();
    void           createDescriptorPool();
    void           createDescriptorSets();

    void queryPhysicalDeviceLimits();
};
