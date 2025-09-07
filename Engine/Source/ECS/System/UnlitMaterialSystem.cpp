#include "UnlitMaterialSystem.h"
#include "ECS/Component/CameraComponent.h"
#include "Math/Geometry.h"


#include <vulkan/vulkan.hpp>

#include "Core/App/App.h"

#include "Render/Core/Material.h"
#include "Render/Core/RenderTarget.h"
#include "Render/RenderDefines.h"


#include "Platform/Render/Vulkan/VulkanBuffer.h"
#include "Platform/Render/Vulkan/VulkanDescriptorSet.h"
#include "Platform/Render/Vulkan/VulkanPipeline.h"
#include "Platform/Render/Vulkan/VulkanRender.h"



#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "Scene/Scene.h"



namespace ya

{

void UnlitMaterialSystem::onInit(VulkanRenderPass *renderPass)
{
    VulkanRender *vkRender = getVulkanRender();

    auto _sampleCount = ESampleCount::Sample_1;

    PipelineLayout pipelineLayout{
        .label         = "UnlitMaterialSystem_PipelineLayout",
        .pushConstants = {
            PushConstantRange{
                .offset     = 0,
                .size       = sizeof(material::ModelPushConstant),
                .stageFlags = EShaderStage::Vertex,
            },
        },
        .descriptorSetLayouts = {
            DescriptorSetLayout{
                .label    = "UnlitMaterial_FrameDSL",
                .set      = 0,
                .bindings = {
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Vertex | EShaderStage::Fragment,
                    },
                },
            },
            DescriptorSetLayout{
                .label    = "UnlitMaterial_ParamDSL",
                .set      = 1,
                .bindings = {
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                },

            },
            DescriptorSetLayout{
                .label    = "UnlitMaterial_ResourceDSL",
                .set      = 2,
                .bindings = {
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                    DescriptorSetLayoutBinding{
                        .binding         = 1,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                },
            },
        },
    };

    _materialFrameUboDSL = makeShared<VulkanDescriptorSetLayout>(vkRender, pipelineLayout.descriptorSetLayouts[0]);
    _materialParamDSL    = makeShared<VulkanDescriptorSetLayout>(vkRender, pipelineLayout.descriptorSetLayouts[1]);
    _materialResourceDSL = makeShared<VulkanDescriptorSetLayout>(vkRender, pipelineLayout.descriptorSetLayouts[2]);



    _pipelineLayout = makeShared<VulkanPipelineLayout>(vkRender, pipelineLayout.label);
    _pipelineLayout->create(
        pipelineLayout.pushConstants,
        {
            _materialFrameUboDSL->getHandle(),
            _materialParamDSL->getHandle(),
            _materialResourceDSL->getHandle(),
        });


    GraphicsPipelineCreateInfo pipelineCI{
        .subPassRef = 0,
        // .pipelineLayout   = pipelineLayout,
        .shaderCreateInfo = ShaderCreateInfo{
            .shaderName        = "Test/Unlit.glsl",
            .bDeriveFromShader = false,
            .vertexBufferDescs = {
                VertexBufferDescription{
                    .slot  = 0,
                    .pitch = sizeof(ya::Vertex),
                },
            },
            .vertexAttributes = {
                // (location=0) in vec3 aPos,
                VertexAttribute{
                    .bufferSlot = 0,
                    .location   = 0,
                    .format     = EVertexAttributeFormat::Float3,
                    .offset     = offsetof(ya::Vertex, position),
                },
                //  texcoord
                VertexAttribute{
                    .bufferSlot = 0, // same buffer slot
                    .location   = 1,
                    .format     = EVertexAttributeFormat::Float2,
                    .offset     = offsetof(ya::Vertex, texCoord0),
                },
                // normal
                VertexAttribute{
                    .bufferSlot = 0, // same buffer slot
                    .location   = 2,
                    .format     = EVertexAttributeFormat::Float3,
                    .offset     = offsetof(ya::Vertex, normal),
                },
            },
        },
        // define what state need to dynamically modified in render pass execution
        .dynamicFeatures = EPipelineDynamicFeature::Scissor | // the imgui required this feature as I did not set the dynamical render feature
                           EPipelineDynamicFeature::Viewport,
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = RasterizationState{
            .polygonMode = EPolygonMode::Fill,
            .frontFace   = EFrontFaceType::CounterClockWise, // GL
        },
        .multisampleState = MultisampleState{
            .sampleCount          = _sampleCount,
            .bSampleShadingEnable = false,
        },
        .depthStencilState = DepthStencilState{
            .bDepthTestEnable       = true,
            .bDepthWriteEnable      = true,
            .depthCompareOp         = ECompareOp::Less,
            .bDepthBoundsTestEnable = false,
            .bStencilTestEnable     = false,
            .minDepthBounds         = 0.0f,
            .maxDepthBounds         = 1.0f,
        },
        .colorBlendState = ColorBlendState{
            .attachments = {
                ColorBlendAttachmentState{
                    // 0 is the final present color attachment
                    .index               = 0,
                    .bBlendEnable        = false,
                    .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                    .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                    .colorBlendOp        = EBlendOp::Add,
                    .srcAlphaBlendFactor = EBlendFactor::One,
                    .dstAlphaBlendFactor = EBlendFactor::Zero,
                    .alphaBlendOp        = EBlendOp::Add,
                    .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                },
            },
        },
        .viewportState = ViewportState{
            .viewports = {
                {
                    .x        = 0,
                    .y        = 0,
                    .width    = static_cast<float>(vkRender->getSwapChain()->getWidth()),
                    .height   = static_cast<float>(vkRender->getSwapChain()->getHeight()),
                    .minDepth = 0.0f,
                    .maxDepth = 1.0f,
                },
            },
            .scissors = {Scissor{
                .offsetX = 0,
                .offsetY = 0,
                .width   = static_cast<uint32_t>(vkRender->getSwapChain()->getWidth()),
                .height  = static_cast<uint32_t>(vkRender->getSwapChain()->getHeight()),
            }},
        },
    };
    _pipeline = std::make_shared<VulkanPipeline>(vkRender, renderPass, _pipelineLayout.get());
    _pipeline->recreate(pipelineCI);


    DescriptorPoolCreateInfo poolCI{
        .maxSets   = 1,
        .poolSizes = {
            DescriptorPoolSize{
                .type            = EPipelineDescriptorType::UniformBuffer,
                .descriptorCount = 1,
            },
        },
    };
    _frameDSP = makeShared<VulkanDescriptorPool>(vkRender, poolCI);
    std::vector<VkDescriptorSet> sets(1);
    _frameDSP->allocateDescriptorSetN(_materialFrameUboDSL, 1, sets);
    _frameDS = sets[0];

    recreateMaterialDescPool(NUM_MATERIAL_BATCH);

    _frameUBO = VulkanBuffer::create(
        vkRender,
        BufferCreateInfo{
            .usage         = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            .size          = sizeof(ya::FrameUBO),
            .memProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            .debugName     = "Unlit_Frame_UBO",
        });
}

void UnlitMaterialSystem::onRender(void *cmdBuf, RenderTarget *rt)
{
    Scene *scene = getScene();
    if (!scene) {
        return;
    }
    auto view = scene->getRegistry().view<TagComponent, UnlitMaterialComponent, TransformComponent>();
    if (view.begin() == view.end()) {
        return;
    }

    VkCommandBuffer curCmdBuf = static_cast<VkCommandBuffer>(cmdBuf);
    _pipeline->bind(curCmdBuf);

    uint32_t width  = rt->getFrameBuffer()->getWidth();
    uint32_t height = rt->getFrameBuffer()->getHeight();

    VkViewport viewport{
        .x        = 0,
        .y        = 0,
        .width    = (float)width,
        .height   = (float)height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(curCmdBuf, 0, 1, &viewport);
    VkRect2D scissor{
        .offset = {0, 0},
        .extent = {width, height},
    };
    vkCmdSetScissor(curCmdBuf, 0, 1, &scissor);

    updateFrameDS(rt);

    //
    bool     bShouldForceUpdateMaterial = false;
    uint32_t materialCount              = MaterialFactory::get()->getMaterialSize<UnlitMaterial>();
    if (materialCount > _lastMaterialDSCount) {
        recreateMaterialDescPool(materialCount);
        bShouldForceUpdateMaterial = true;
    }

    std::vector<bool> updatedMaterial(materialCount);

    for (entt::entity entity : view)
    {
        const auto &[tag, umc, tc] = view.get(entity);
        for (const auto &[material, meshIDs] : umc.getMaterial2MeshIDs()) {

            _ctxEntityDebugStr = std::format("{} (Mat: {})", tag._tag, material->getLabel());
            if (!material || material->getIndex() < 0) {
                YA_CORE_WARN("default material for none or error material");
                continue;
            }

            // update each material instance's descriptor set if dirty
            uint32_t        materialID = material->getIndex();
            VkDescriptorSet paramDS    = _materialParamDSs[materialID];
            VkDescriptorSet resourceDS = _materialResourceDSs[materialID];

            // update the resource set when:
            // 1. this has updated, multiple entity using the same material(not material instance?)
            // 2. material count changed
            // 3. this material's resources(such as texture) changed(dirty)
            if (!updatedMaterial[materialID]) {
                if (bShouldForceUpdateMaterial || material->isParamDirty())
                {
                    updateMaterialParamDS(paramDS, material);
                    material->setParamDirty(false);
                }
                if (bShouldForceUpdateMaterial || material->isResourceDirty())
                {
                    updateMaterialResourceDS(resourceDS, material);
                    material->setResourceDirty(false);
                }
                updatedMaterial[materialID] = true;
            }

            // bind descriptor set
            std::array<VkDescriptorSet, 3> descSets{
                _frameDS,
                _materialParamDSs[materialID],
                _materialResourceDSs[materialID],
            };
            vkCmdBindDescriptorSets(
                curCmdBuf,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                _pipelineLayout->getHandle(),
                0,
                static_cast<uint32_t>(descSets.size()),
                descSets.data(),
                0,
                nullptr);

            // update push constant
            material::ModelPushConstant pushConst{
                .modelMatrix = tc.getTransform(),
            };
            vkCmdPushConstants(curCmdBuf,
                               _pipelineLayout->getHandle(),
                               VK_SHADER_STAGE_VERTEX_BIT,
                               0,
                               sizeof(material::ModelPushConstant),
                               &pushConst);

            // draw each mesh
            for (const auto &meshIndex : meshIDs)
            {
                auto mesh = umc.getMesh(meshIndex);
                if (mesh) {
                    mesh->draw(curCmdBuf);
                }
            }
        }
    }
}

void UnlitMaterialSystem::recreateMaterialDescPool(uint32_t _materialCount)
{
    auto vkRender = getVulkanRender();
    YA_CORE_ASSERT(vkRender != nullptr, "VulkanRender is null");
    // 1. calculate how many set needed
    uint32_t newDescriptorSetCount = std::max<uint32_t>(1, _lastMaterialDSCount);
    if (_lastMaterialDSCount == 0) {
        _lastMaterialDSCount = newDescriptorSetCount;
    }

    while (newDescriptorSetCount < _materialCount) {
        newDescriptorSetCount *= 2;
    }
    if (newDescriptorSetCount > NUM_MATERIAL_BATCH_MAX) {
        YA_CORE_ASSERT(false, "Too many material, exceed the max limit");
        return;
    }

    // 2. destroy old
    _materialParamDSs.clear();
    _materialResourceDSs.clear();
    if (_materialDSP) {
        _materialDSP.reset();
    }

    // 3. recreate pool

    DescriptorPoolCreateInfo poolCI{
        .maxSets   = newDescriptorSetCount * 2, // max(param , resource)
        .poolSizes = {
            DescriptorPoolSize{
                .type            = EPipelineDescriptorType::UniformBuffer,
                .descriptorCount = newDescriptorSetCount,
            },
            DescriptorPoolSize{
                .type            = EPipelineDescriptorType::CombinedImageSampler,
                .descriptorCount = newDescriptorSetCount * 2, // tex0 + tex1 for each material param in one set
            },
        },
    };

    _materialDSP = makeShared<VulkanDescriptorPool>(vkRender, poolCI);

    // 4. allocate new sets
    // 为每一个单独的材质分配描述符集
    _materialDSP->allocateDescriptorSetN(_materialParamDSL, newDescriptorSetCount, _materialParamDSs);
    _materialDSP->allocateDescriptorSetN(_materialResourceDSL, newDescriptorSetCount, _materialResourceDSs);

    // 5. create UBOs
    uint32_t diffCount = newDescriptorSetCount - _materialParamsUBOs.size();
    for (uint32_t i = 0; i < diffCount; i++) {
        auto buffer = VulkanBuffer::create(
            vkRender,
            BufferCreateInfo{
                .usage         = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                .size          = sizeof(ya::UnlitMaterialUBO),
                .memProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                .debugName     = "MaterialParam_UBO",
            });
        _materialParamsUBOs.push_back(buffer);
    }


    _lastMaterialDSCount = newDescriptorSetCount;
}

void UnlitMaterialSystem::updateFrameDS(RenderTarget *rt)
{
    auto app    = getApp();
    auto render = getVulkanRender();

    glm::mat4 proj;
    glm::mat4 view;
    rt->getViewAndProjMatrix(view, proj);

    FrameUBO ubo{
        .projection = proj,
        .view       = view,
        .resolution = {
            rt->getFrameBuffer()->getWidth(),
            rt->getFrameBuffer()->getHeight(),
        },
        .frameIndex = app->getFrameIndex(),
        .time       = (float)app->getElapsedTimeMS() / 1000.0f,
    };

    _frameUBO->writeData(&ubo, sizeof(ubo), 0);

    VkDescriptorBufferInfo bufferInfo{
        .buffer = _frameUBO->getHandle(),
        .offset = 0,
        .range  = sizeof(ya::FrameUBO),
    };

    VulkanDescriptor::updateSets(
        render->getDevice(),
        {
            VulkanDescriptor::genBufferWrite(
                _frameDS,
                0,
                0,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                &bufferInfo,
                1),
        },
        {});
}

void UnlitMaterialSystem::updateMaterialParamDS(VkDescriptorSet ds, UnlitMaterial *material)
{
    auto render = getVulkanRender();

    YA_CORE_ASSERT(ds != VK_NULL_HANDLE, "descriptor set is null: {}", _ctxEntityDebugStr);


    auto &params = material->uMaterial;
    // update param from texture
    const TextureView *tv0 = material->getTextureView(UnlitMaterial::BaseColor0);
    if (tv0) {
        Material::updateTextureParamsByTextureView(tv0, params.textureParam0);
    }
    const TextureView *tv1 = material->getTextureView(UnlitMaterial::BaseColor1);
    if (tv1) {
        Material::updateTextureParamsByTextureView(tv1, params.textureParam1);
    }

    auto paramUBO = _materialParamsUBOs[material->getIndex()].get();
    paramUBO->writeData(&params, sizeof(ya::UnlitMaterialUBO), 0);

    VkDescriptorBufferInfo bufferInfo{
        .buffer = paramUBO->getHandle(),
        .offset = 0,
        .range  = sizeof(ya::UnlitMaterialUBO),
    };

    VulkanDescriptor::updateSets(
        render->getDevice(),
        {
            VulkanDescriptor::genBufferWrite(
                ds,
                0,
                0,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                &bufferInfo,
                1),
        },
        {});
}

void UnlitMaterialSystem::updateMaterialResourceDS(VkDescriptorSet ds, UnlitMaterial *material)
{
    auto render = getVulkanRender();


    YA_CORE_ASSERT(ds != VK_NULL_HANDLE, "descriptor set is null: {}", _ctxEntityDebugStr);
    auto &params = material->uMaterial;
    // TODO: not texture and default texture?
    // update param from texture
    const TextureView *tv0 = material->getTextureView(UnlitMaterial::BaseColor0);
    const TextureView *tv1 = material->getTextureView(UnlitMaterial::BaseColor1);

    auto resourceUBO = _materialParamsUBOs[material->getIndex()].get();
    resourceUBO->writeData(&params, sizeof(ya::UnlitMaterialUBO), 0);

    VkDescriptorImageInfo imageInfo0 = {
        .sampler     = tv0->sampler->getHandle(),
        .imageView   = tv0->texture->getVkImageView(),
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkDescriptorImageInfo imageInfo1 = {
        .sampler     = tv1->sampler->getHandle(),
        .imageView   = tv1->texture->getVkImageView(),
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };


    VulkanDescriptor::updateSets(
        render->getDevice(),
        {
            VulkanDescriptor::genImageWrite(ds, 0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfo0, 1),
            VulkanDescriptor::genImageWrite(ds, 1, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfo1, 1),
        },
        {});
}

} // namespace ya
