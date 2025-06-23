#pragma once


#include "Render/RenderManager.h"
#include <vector>
#include <vulkan/vulkan.h>


/*
Render Pass主导资源声明
Pipeline 需要兼容/依赖 Render Pass 的资源声明
*/
class VulkanRenderPass : public RenderPass
{
  private:
    std::shared_ptr<GLSLScriptProcessor> _shaderProcessor;

    VkDevice         m_logicalDevice        = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice       = VK_NULL_HANDLE;
    VkFormat         m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
    VkFormat         m_depthFormat          = VK_FORMAT_UNDEFINED;

    VkRenderPass               m_renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;

    // Render pass configuration
    struct RenderPassConfig
    {
        VkSampleCountFlagBits samples      = VK_SAMPLE_COUNT_1_BIT;
        VkAttachmentLoadOp    colorLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        VkAttachmentStoreOp   colorStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkAttachmentLoadOp    depthLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        VkAttachmentStoreOp   depthStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    } m_config;


    VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;

  public:
    VulkanRenderPass()  = default;
    ~VulkanRenderPass() = default;

    // Initialize the render pass with device and format information
    void initialize(VkDevice logicalDevice, VkPhysicalDevice physicalDevice, VkFormat swapChainImageFormat);

    // Create the render pass
    void createRenderPass(std::string path);

    // Create framebuffers for the render pass
    void createFramebuffers(const std::vector<VkImageView> &swapChainImageViews,
                            VkImageView                     depthImageView,
                            VkExtent2D                      swapChainExtent);

    // Cleanup resources
    void cleanup();

    // Recreate render pass and framebuffers (for swap chain recreation)
    void recreate(const std::vector<VkImageView> &swapChainImageViews,
                  VkImageView                     depthImageView,
                  VkExtent2D                      swapChainExtent);

    // Getters
    VkRenderPass                      getRenderPass() const { return m_renderPass; }
    const std::vector<VkFramebuffer> &getFramebuffers() const { return m_framebuffers; }
    VkFormat                          getDepthFormat() const { return m_depthFormat; }

    // Begin render pass
    void beginRenderPass(VkCommandBuffer commandBuffer, uint32_t frameBufferIndex, VkExtent2D extent);

    // End render pass
    void endRenderPass(VkCommandBuffer commandBuffer);


    VkShaderModule createShaderModule(const std::vector<uint32_t> &spv_binary)
    {
        VkShaderModuleCreateInfo shaderModuleCreateInfo = {
            .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = spv_binary.size() * 4, // VUID-VkShaderModuleCreateInfo-codeSize-08735
            .pCode    = spv_binary.data(),
        };

        VkShaderModule shaderModule;
        auto           ret = vkCreateShaderModule(m_logicalDevice, &shaderModuleCreateInfo, nullptr, &shaderModule);
        NE_ASSERT(ret == VK_SUCCESS, "failed to create shader module {}", (int)ret);
        return shaderModule;
    }

    void createGraphicsPipeline(std::string path)
    {
        auto stage2Spirv = _shaderProcessor->process(path).value();

        /************************** Shader Stages ***********************************************/

        /* Shader modules define the programmable pipeline stages */

        // Compile Module (Code)
        auto vertShaderModule = createShaderModule(stage2Spirv[EShaderStage::Vertex]);
        auto fragShaderModule = createShaderModule(stage2Spirv[EShaderStage::Fragment]);

        VkPipelineShaderStageCreateInfo vertShaderStageInfo = {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertShaderModule,
            .pName  = "main",
        };
        VkPipelineShaderStageCreateInfo fragShaderStageInfo = {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragShaderModule,
            .pName  = "main",
        };

        // Shader Stage createInfo
        VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};


        auto vertexReflectInfo = _shaderProcessor->reflect(EShaderStage::Vertex, stage2Spirv[EShaderStage::Vertex]);

        std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;

        auto spirvType2VulkanFormat = [](const spirv_cross::SPIRType &type) {
            switch (type.basetype) {
            case spirv_cross::SPIRType::Float:
            {
                if (type.vecsize == 1 && type.columns == 1) {
                    return VK_
                }
            } break;
            };
        };

        for (auto &input : vertexReflectInfo.inputs) {
            VkVertexInputAttributeDescription attributeDescription = {};
            attributeDescription.binding                           = 0;              // Binding index, must match the binding in the vertex input state
            attributeDescription.location                          = input.location; // Location index, must match the location in the shader
            attributeDescription.format                            = input.format;   // Format of the attribute (e.g., VK_FORMAT_R32G32B32_SFLOAT)
            attributeDescription.offset                            = input.offset;   // Offset in bytes from the start of the vertex structure
            vertexAttributeDescriptions.push_back(attributeDescription);
        }
        // VertexInput v_input(0);
        v_input.AddAttribute(VK_FORMAT_R32G32B32_SFLOAT, "postion")
            .AddAttribute(VK_FORMAT_R32G32B32_SFLOAT, "color")
            .AddAttribute(VK_FORMAT_R32G32_SFLOAT, "texture_coord");

        // the description of vertex(declaration) should compatible with the  shader module
        // auto bindingDescription    = v_input.GetBindingDescription();
        // auto attributeDescriptions = v_input.GetAttributeDescriptions();

        // vertex input State createInfo
        VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
            //     .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            //     .vertexBindingDescriptionCount   = 1,
            //     .pVertexBindingDescriptions      = &bindingDescription,
            //     .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
            //     .pVertexAttributeDescriptions    = attributeDescriptions.data(),
        };
        /************************** Shader Stages ***********************************************/


        /****************************** Fix-function State *****************************************/


        // Assembly state creatInfo ( Triangle format in this example)
        VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
        {
            inputAssembly.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssembly.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            inputAssembly.primitiveRestartEnable = VK_FALSE;
        }

        // Viewport defines the transformation mapping from image to framebuffer region, corresponding to xy coordinates and hw dimensions, minmax
        VkViewport viewport = {};
        {
            viewport.x        = 0.0f;
            viewport.y        = 0.0f;
            viewport.width    = (float)m_SwapChainExtent.width;
            viewport.height   = (float)m_SwapChainExtent.height;
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
        }

        // Scissor clipping defines which pixels will be discarded
        VkRect2D scissor = {};
        {
            scissor.offset = {0, 0};
            scissor.extent = m_SwapChainExtent; // Keep the clipping region consistent
        }

        // Viewport State createInfo combined with clipping regions and viewports
        VkPipelineViewportStateCreateInfo viewportState = {};
        {
            viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.pViewports    = &viewport;
            viewportState.scissorCount  = 1;
            viewportState.pScissors     = &scissor;
        }


        // Rasterization State createInfo handles geometry transmission to fragment shader, including depth testing, scissor and clipping tests, wireframe or filled primitive rendering
        VkPipelineRasterizationStateCreateInfo rasterizer = {};
        {
            rasterizer.sType            = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.depthClampEnable = VK_FALSE;

            // Enable depth clamping and discard primitives that exceed the near/far planes, affecting image rendering, requires GPU support; otherwise enable primitive discard beyond framebuffer
            rasterizer.rasterizerDiscardEnable = VK_FALSE; // polygonMode controls how geometry is filled. Available modes:
            // VK_POLYGON_MODE_FILL: fill polygons with fragments
            // VK_POLYGON_MODE_LINE : draw polygon edges as lines
            // VK_POLYGON_MODE_POINT : draw polygon vertices as points
            // Using any mode other than fill requires enabling a GPU feature.
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;

            rasterizer.lineWidth = 1.0f;
            rasterizer.cullMode  = VK_CULL_MODE_BACK_BIT; // culling/font faces/call back faces/all ��ü��ķ�ʽ
            // rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE; // ˳ʱ��/��ʱ�� �������˳��
            rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; /// ��ת y ��������ʱ�����

            rasterizer.depthBiasEnable         = VK_FALSE;
            rasterizer.depthBiasConstantFactor = 0.0f; // Optional
            rasterizer.depthBiasClamp          = 0.0f; // Optional
            rasterizer.depthBiasSlopeFactor    = 0.0f; // Optional
        }

        // Multisample State createInfo ���ز��� ���ﲻʹ��
        VkPipelineMultisampleStateCreateInfo multisample_state_create_info{
            .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable   = VK_FALSE,
            .minSampleShading      = 1.0f,     // Optional
            .pSampleMask           = nullptr,  // Optional
            .alphaToCoverageEnable = VK_FALSE, // Optional
            .alphaToOneEnable      = VK_FALSE, // Optional
        };

        // Blender ��fragment �������ɫ��ɵ� framebuffer �д��ڵ���ɫ���л�ϣ� ��ϣ��������Ϊ�µ���ɫ���� λ���� , ������õ�һ�ַ�ʽ
        VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
        {
            colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            colorBlendAttachment.blendEnable    = VK_FALSE;

            if (colorBlendAttachment.blendEnable) {
                // finalcolor = newcolor & color writemask
                colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;  // Optional
                colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
                colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;      // Optional
                colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;  // Optional
                colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
                colorBlendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;      // Optional
            }
            else {
                // alpha blending: finalcolor.rgb = newAlpha*newColor + (1-newAlpha)*oldColor; finalcolor.a = newAlpha.a
                colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; // blend method
                colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
                colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                colorBlendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;
            }
        }

        // color blend createInfo
        VkPipelineColorBlendStateCreateInfo colorBlendingCreateInfo = {};
        {
            colorBlendingCreateInfo.sType             = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlendingCreateInfo.logicOpEnable     = VK_FALSE; // ���õڶ��ַ�ʽ��Ҫ Ϊ true, �����Ʋ����� logicOp �ֶ���ָ��
            colorBlendingCreateInfo.attachmentCount   = 1;
            colorBlendingCreateInfo.pAttachments      = &colorBlendAttachment;
            colorBlendingCreateInfo.blendConstants[0] = 0.0f; // Optional
            colorBlendingCreateInfo.blendConstants[1] = 0.0f; // Optional
            colorBlendingCreateInfo.blendConstants[2] = 0.0f; // Optional
            colorBlendingCreateInfo.blendConstants[3] = 0.0f; // Optional
        }

        // ����ʱ��̬�޸�
        VkDynamicState dynamicState[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_LINE_WIDTH};
        VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo = {};
        {
            dynamicStateCreateInfo.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamicStateCreateInfo.dynamicStateCount = 2; // same to above
            dynamicStateCreateInfo.pDynamicStates    = dynamicState;
        }

        // ��Ȳ��������ͼ
        VkPipelineDepthStencilStateCreateInfo depthStencil = {};
        {
            depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

            depthStencil.depthTestEnable  = VK_TRUE; // �Ƿ��µ�depth����ȥ��ɵıȽϣ���ȷ���Ƿ���
            depthStencil.depthWriteEnable = VK_TRUE; // �µ� depth buffer �Ƿ�ʵ��д��

            depthStencil.depthCompareOp = VK_COMPARE_OP_LESS; // ��������Ƭ�εıȽ�ϸ�ڣ�less��ζ�Ž����µ����ֵӦ�ø���

            depthStencil.stencilTestEnable = VK_FALSE;
            depthStencil.front             = {}; // Optional
            depthStencil.back              = {}; // Optional
        }

        /********************************* Fixed-function State ********************************************/


        /************************************* Pipeline Layout ********************************************/

        /* Pipeline layout defines uniform variables and push constants that can be accessed by shaders during each drawing call */

        // pipeLayout Createinfo
        VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
        {
            pipelineLayoutCreateInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutCreateInfo.setLayoutCount         = 1;
            pipelineLayoutCreateInfo.pSetLayouts            = &m_descriptorSetLayout; // ʹ�ó�Ա�������е�layout(createDescriptorSetLayout())
            pipelineLayoutCreateInfo.pushConstantRangeCount = 0;                      // Optional
            pipelineLayoutCreateInfo.pPushConstantRanges    = 0;                      // Optional
        }
        if (vkCreatePipelineLayout(m_LogicalDevice, &pipelineLayoutCreateInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
            NE_CORE_ASSERT(false, "filed to create pipeline layout!");
        }
        /************************************* Pipeline Layout ********************************************/
        /************************************* Render Pass ********************************************/
        /*   NOTE: createRenderPass() moved to VulkanRenderPass class          */
        /************************************* Render Pass ********************************************/


        // Pipeline createInfo
        VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
        {
            pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

            // pStages pointer to state create info array
            pipelineCreateInfo.stageCount = 2; // vertex AND fragment State
            pipelineCreateInfo.pStages    = shaderStages;

            pipelineCreateInfo.pVertexInputState   = &vertexInputInfo;
            pipelineCreateInfo.pInputAssemblyState = &inputAssembly;
            pipelineCreateInfo.pViewportState      = &viewportState;
            pipelineCreateInfo.pRasterizationState = &rasterizer;
            pipelineCreateInfo.pMultisampleState   = &multisample_state_create_info;
            pipelineCreateInfo.pDepthStencilState  = nullptr; // Depth  Optional
            pipelineCreateInfo.pColorBlendState    = &colorBlendingCreateInfo;
            pipelineCreateInfo.pDynamicState       = nullptr; // Dynamicstage Optional

            pipelineCreateInfo.layout     = m_pipelineLayout;
            pipelineCreateInfo.renderPass = m_renderPass.getRenderPass(); // bind renderPass
            pipelineCreateInfo.subpass    = 0;                            // subpass index

            /*
            Pipeline derivation allows creating new graphics pipelines based on existing ones.
            This can reduce creation time when pipelines share common state and may allow
            driver optimizations and better cache usage.
            You can specify base pipeline using basePipelineHandle or basePipelineIndex.
            Currently we only have one pipeline, so we just set these to null/invalid values.
            These values are only used when VK_PIPELINE_CREATE_DERIVATIVE_BIT flag is set.
            */
            pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE; //  Optional
            pipelineCreateInfo.basePipelineIndex  = -1;             //  Optional

            pipelineCreateInfo.pDepthStencilState = &depthStencil;
        } // 1. Create graphics pipeline and save to member variable, can later reference multiple pipelineCreateInfo objects
        // 2. Second parameter is pipeline cache for storing and reusing data across multiple calls to VkCreateGraphicsPipelines,
        //    can be saved to file and loaded later to speed up pipeline creation
        if (vkCreateGraphicsPipelines(m_LogicalDevice, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_graphicsPipeLine) != VK_SUCCESS)
        {
            panic("failed to create graphics pipeline!");
        }


        // Destroy shaders modules to release resources after complete createPipeline
        vkDestroyShaderModule(m_LogicalDevice, fragShaderModule, nullptr);
        vkDestroyShaderModule(m_LogicalDevice, vertShaderModule, nullptr);
    }


  private:
    // Find suitable depth format
    VkFormat
    findDepthFormat();

    // Find supported format from candidates
    VkFormat findSupportedFormat(const std::vector<VkFormat> &candidates,
                                 VkImageTiling                tiling,
                                 VkFormatFeatureFlags         features);
};