#include "SkyBoxSystem.h"
#include "Core/App/App.h"
#include "Core/Debug/Instrumentor.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/Swapchain.h"
#include "Render/Render.h"
#include "glm/glm.hpp"

#include "Platform/Render/Vulkan/VulkanRender.h"

namespace ya
{

void SkyBoxSystem::onInit(IRenderPass* renderPass, const PipelineRenderingInfo& pipelineRenderingInfo)
{
    auto render = App::get()->getRender();

    // MARK: Create Descriptor Set Layouts
    auto DSLs    = IDescriptorSetLayout::create(render, _pipelineLayoutDesc.descriptorSetLayouts);
    _dslPerFrame = DSLs[0];
    _dslResource = DSLs[1];

    // MARK: Create Pipeline Layout
    _pipelineLayout = IPipelineLayout::create(render,
                                              _pipelineLayoutDesc.label,
                                              _pipelineLayoutDesc.pushConstants,
                                              DSLs);

    // MARK: Create Graphics Pipeline
    GraphicsPipelineCreateInfo ci = {
        .renderPass            = renderPass,
        .pipelineRenderingInfo = pipelineRenderingInfo,
        .pipelineLayout        = _pipelineLayout.get(),
        .shaderDesc            = {

            .shaderName        = "Skybox.glsl",
            .bDeriveFromShader = false,

            // also a cube mesh
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
        .dynamicFeatures = {
            EPipelineDynamicFeature::Viewport,
            EPipelineDynamicFeature::Scissor,
        },
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = RasterizationState{
            .polygonMode = EPolygonMode::Fill,
            .cullMode    = ECullMode::Front, // cull front face, we in the sky box
            .frontFace   = EFrontFaceType::CounterClockWise,
        },
        .depthStencilState = DepthStencilState{
            .bDepthTestEnable       = true,
            .bDepthWriteEnable      = false,
            .depthCompareOp         = ECompareOp::LessOrEqual,
            .bDepthBoundsTestEnable = false,
            .bStencilTestEnable     = false,
        },
        .colorBlendState = ColorBlendState{
            .attachments = {
                ColorBlendAttachmentState{
                    .index          = 0,
                    .bBlendEnable   = false,
                    .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                },
            },
        },
        .viewportState = ViewportState{
            .viewports = {Viewport::defaults()},
            .scissors  = {Scissor::defaults()},
        },
    };
    _pipeline = IGraphicsPipeline::create(render);
    bool ok   = _pipeline->recreate(ci);
    YA_CORE_ASSERT(ok, "Failed to create Skybox graphics pipeline");

    // MARK: Create Descriptor Pool
    _DSP = IDescriptorPool::create(
        render,
        DescriptorPoolCreateInfo{
            .maxSets   = SKYBOX_PER_FRAME_SET /*+ 1*/,
            .poolSizes = {
                DescriptorPoolSize{
                    .type            = EPipelineDescriptorType::UniformBuffer,
                    .descriptorCount = SKYBOX_PER_FRAME_SET,
                },
                // DescriptorPoolSize{
                //     .type            = EPipelineDescriptorType::CombinedImageSampler,
                //     .descriptorCount = 1,
                // },
            },
        });

    // MARK: Allocate Descriptor Sets
    std::vector<ya::DescriptorSetHandle> frameSets;
    // std::vector<ya::DescriptorSetHandle> resourceSets;
    _DSP->allocateDescriptorSets(_dslPerFrame, 4, frameSets);
    // _DSP->allocateDescriptorSets(_dslResource, 1, resourceSets);


    // MARK: Initial descriptor set updates (once during init)
    SkyboxFrameUBO initialFrameData{
        .projection = glm::mat4(1.0f),
        .view       = glm::mat4(1.0f),
    };

    std::vector<WriteDescriptorSet> writes;
    writes.resize(4);
    for (uint32_t i = 0; i < 4; i++) {
        _dsPerFrame[i] = frameSets[i];
        render->as<VulkanRender>()->setDebugObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET,
                                                       _dsPerFrame[i].ptr,
                                                       std::format("Skybox_PerFrame_DS_{}", i).c_str());

        // MARK: Create Frame UBO
        _frameUBO[i] = IBuffer::create(
            render,
            ya::BufferCreateInfo{
                .label         = std::format("Skybox_Frame_UBO_{}", i),
                .usage         = ya::EBufferUsage::UniformBuffer,
                .size          = sizeof(SkyboxFrameUBO),
                .memProperties = ya::EMemoryProperty::HostVisible | ya::EMemoryProperty::HostCoherent,
            });
        _frameUBO[i]->writeData(&initialFrameData, sizeof(SkyboxFrameUBO), 0);

        writes[i] = IDescriptorSetHelper::genSingleBufferWrite(
            _dsPerFrame[i],
            0,
            EPipelineDescriptorType::UniformBuffer,
            _frameUBO[i].get());
    }
    render->getDescriptorHelper()->updateDescriptorSets(writes, {});


    // _cubeMapDS = resourceSets[0];
    // render->as<VulkanRender>()->setDebugObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET, _dsResource.ptr, "Skybox_Resource_DS");

    _sampler3D = Sampler::create(SamplerDesc{
        .label        = "SkyboxSampler",
        .addressModeU = ESamplerAddressMode::Repeat,
        .addressModeV = ESamplerAddressMode::Repeat,
        .addressModeW = ESamplerAddressMode::Repeat,
    });
}

void SkyBoxSystem::onDestroy()
{
}

void SkyBoxSystem::preload()
{
    auto scene = App::get()->getSceneManager()->getActiveScene();
    for (const auto& [entity, sc, mc] :
         scene->getRegistry().view<SkyboxComponent, MeshComponent>().each())
    {
        if (sc.bDirty) {
            TextureView tv = TextureView::create(sc.cubemapTexture, _sampler3D);

            auto render = App::get()->getRender();
            render->getDescriptorHelper()->updateDescriptorSets(
                {
                    IDescriptorSetHelper::genSingleTextureViewWrite(
                        _cubeMapDS,
                        0,
                        EPipelineDescriptorType::CombinedImageSampler,
                        &tv),
                },
                {});
        }
    }
}

void SkyBoxSystem::tick(ICommandBuffer* cmdBuf, float deltaTime, const FrameContext& ctx)
{
    auto             scene      = App::get()->getSceneManager()->getActiveScene();
    SkyboxComponent* skyboxComp = nullptr;
    MeshComponent*   meshComp   = nullptr;
    for (const auto& [entity, sc, mc] :
         scene->getRegistry().view<SkyboxComponent, MeshComponent>().each())
    {
        skyboxComp = &sc;
        meshComp   = &mc;
        break;
    }
    if (!skyboxComp || !meshComp) {
        return;
    }

    if (skyboxComp->bDirty) {
        skyboxComp->bDirty = false;
        TextureView tv     = TextureView::create(skyboxComp->cubemapTexture, _sampler3D);

        auto render = App::get()->getRender();
        render->getDescriptorHelper()->updateDescriptorSets(
            {
                IDescriptorSetHelper::genSingleTextureViewWrite(
                    _cubeMapDS,
                    0,
                    EPipelineDescriptorType::CombinedImageSampler,
                    &tv),
            },
            {});
    }


    // MARK: Set Viewport and Scissor
    uint32_t width  = ctx.extent.width;
    uint32_t height = ctx.extent.height;
    if (width == 0 || height == 0) {
        return;
    }
    float viewportY      = 0.0f;
    float viewportHeight = static_cast<float>(height);

    if (1 /*bReverseViewportY*/) {
        viewportY      = static_cast<float>(height);
        viewportHeight = -static_cast<float>(height);
    }


    // MARK: Bind Pipeline
    cmdBuf->bindPipeline(_pipeline.get());

    cmdBuf->setViewport(0.0f, viewportY, (float)width, viewportHeight, 0.0f, 1.0f);
    cmdBuf->setScissor(0, 0, width, height);

    // MARK: Update UBO data (buffer only, no descriptor set update)
    SkyboxFrameUBO frameData{
        .projection = ctx.projection,
        .view       = FMath::dropTranslation(ctx.view),
    };
    _frameUBO[_index]->writeData(&frameData, sizeof(SkyboxFrameUBO), 0);



    // MARK: Bind descriptor sets (bound sets already initialized)
    cmdBuf->bindDescriptorSets(_pipelineLayout.get(),
                               0,
                               {
                                   _dsPerFrame[_index],
                                   _cubeMapDS,
                               });

    meshComp->getMesh()->draw(cmdBuf);

    advance();
}

} // namespace ya
