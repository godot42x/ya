#include "UnlitMaterialSystem.h"
#include "Core/Math/Geometry.h"
#include "ECS/Component/CameraComponent.h"
#include "ECS/Component/MeshComponent.h"

#include "Runtime/App/App.h"

#include "Render/Core/Buffer.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/Swapchain.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/Material/UnlitMaterial.h"
#include "Render/Render.h"
#include "Render/RenderDefines.h"

#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "Scene/Scene.h"



namespace ya
{

void UnlitMaterialSystem::onInitImpl(const InitParams& initParams)
{
    IRender* render = getRender();

    // MARK: layout
    PipelineLayoutDesc pipelineLayout{
        .label         = "UnlitMaterialSystem_PipelineLayout",
        .pushConstants = {
            PushConstantRange{
                .offset     = 0,
                .size       = sizeof(UnlitMaterialSystem::PushConstant),
                .stageFlags = EShaderStage::Vertex,
            },
        },
        .descriptorSetLayouts = {
            DescriptorSetLayoutDesc{
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
            DescriptorSetLayoutDesc{
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
            DescriptorSetLayoutDesc{
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

    _materialFrameUboDSL = IDescriptorSetLayout::create(render, pipelineLayout.descriptorSetLayouts[0]);
    _materialParamDSL    = IDescriptorSetLayout::create(render, pipelineLayout.descriptorSetLayouts[1]);
    _materialResourceDSL = IDescriptorSetLayout::create(render, pipelineLayout.descriptorSetLayouts[2]);

    // Use factory method to create pipeline layout
    std::vector<std::shared_ptr<IDescriptorSetLayout>> dslVec = {_materialFrameUboDSL, _materialParamDSL, _materialResourceDSL};
    _pipelineLayout = IPipelineLayout::create(render, pipelineLayout.label, pipelineLayout.pushConstants, dslVec);

    // MARK: pipeline
    _pipelineDesc = GraphicsPipelineCreateInfo{
        .subPassRef            = 0,
        .renderPass            = initParams.renderPass,
        .pipelineRenderingInfo = initParams.pipelineRenderingInfo,
        .pipelineLayout        = _pipelineLayout.get(),

        .shaderDesc = ShaderDesc{
            .shaderName        = "Test/Unlit.glsl",
            .vertexBufferDescs = {
                VertexBufferDescription{
                    .slot  = 0,
                    .pitch = sizeof(ya::Vertex),
                },
            },
            .vertexAttributes = {
                VertexAttribute{
                    .bufferSlot = 0,
                    .location   = 0,
                    .format     = EVertexAttributeFormat::Float3,
                    .offset     = offsetof(ya::Vertex, position),
                },
                VertexAttribute{
                    .bufferSlot = 0,
                    .location   = 1,
                    .format     = EVertexAttributeFormat::Float2,
                    .offset     = offsetof(ya::Vertex, texCoord0),
                },
                VertexAttribute{
                    .bufferSlot = 0,
                    .location   = 2,
                    .format     = EVertexAttributeFormat::Float3,
                    .offset     = offsetof(ya::Vertex, normal),
                },
            },
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Scissor,
                               EPipelineDynamicFeature::Viewport},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = RasterizationState{
            .polygonMode = EPolygonMode::Fill,
            .frontFace   = EFrontFaceType::CounterClockWise,
        },
        .multisampleState = MultisampleState{
            .sampleCount          = ESampleCount::Sample_1,
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
                    .width    = static_cast<float>(render->getSwapchain()->getExtent().width),
                    .height   = static_cast<float>(render->getSwapchain()->getExtent().height),
                    .minDepth = 0.0f,
                    .maxDepth = 1.0f,
                },
            },
            .scissors = {Scissor{
                .offsetX = 0,
                .offsetY = 0,
                .width   = render->getSwapchain()->getExtent().width,
                .height  = render->getSwapchain()->getExtent().height,
            }},
        },
    };
    _pipeline = IGraphicsPipeline::create(render);
    _pipeline->recreate(_pipelineDesc);

    // frames descriptor set pool
    DescriptorPoolCreateInfo poolCI{
        .maxSets   = MAX_FRAME_SLOTS,
        .poolSizes = {
            DescriptorPoolSize{
                .type            = EPipelineDescriptorType::UniformBuffer,
                .descriptorCount = MAX_FRAME_SLOTS,
            },
        },
    };
    _frameDSP = IDescriptorPool::create(render, poolCI);
    std::vector<ya::DescriptorSetHandle> sets;
    _frameDSP->allocateDescriptorSets(_materialFrameUboDSL, MAX_FRAME_SLOTS, sets);
    for (uint32_t i = 0; i < MAX_FRAME_SLOTS; ++i) {
        _frameDSs[i] = sets[i];
    }

    // Initialize material descriptor pool using MaterialDescPool
    constexpr uint32_t textureCount = 2; // BaseColor0 + BaseColor1
    _matPool.init(
        render,
        _materialParamDSL,
        _materialResourceDSL,
        [textureCount](uint32_t n) -> std::vector<DescriptorPoolSize> {
            return {
                DescriptorPoolSize{.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = n},
                DescriptorPoolSize{.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = n * textureCount},
            };
        },
        NUM_MATERIAL_BATCH);
    _bDescriptorPoolRecreated = true;

    for (uint32_t i = 0; i < MAX_FRAME_SLOTS; ++i) {
        _frameUBOs[i] = ya::IBuffer::create(
            render,
            ya::BufferCreateInfo{
                .label         = std::format("Unlit_Frame_UBO_{}", i),
                .usage         = ya::EBufferUsage::UniformBuffer,
                .size          = sizeof(FrameUBO),
                .memoryUsage = ya::EMemoryUsage::CpuToGpu,
            });
    }
}

void UnlitMaterialSystem::preTick(float deltaTime, const FrameContext* ctx)
{
    (void)deltaTime;
    (void)ctx;

    // Has move to ResourceResolveSystem
    // Phase 1: Resolve components that need it
    // Scene* scene = getActiveScene();
    // if (scene) {
    //     auto view = scene->getRegistry().view<UnlitMaterialComponent>();
    //     for (auto entity : view) {
    //         auto& umc = view.get<UnlitMaterialComponent>(entity);
    //         if (umc.needsResolve()) {
    //             umc.resolve();
    //         }
    //     }
    // }

    // Phase 2: Ensure descriptor pool capacity
    uint32_t materialCount = MaterialFactory::get()->getMaterialSize<UnlitMaterial>();
    if (_matPool.ensureCapacity(materialCount)) {
        _bDescriptorPoolRecreated = true;
    }
}


void UnlitMaterialSystem::onRender(ICommandBuffer* cmdBuf, const FrameContext* ctx)
{
    Scene* scene = getActiveScene();
    if (!scene) {
        return;
    }
    auto view = scene->getRegistry().view<UnlitMaterialComponent, MeshComponent, TransformComponent>();
    if (view.begin() == view.end()) {
        return;
    }

    // Pre-tick: resolve components and ensure pool capacity
    preTick(0.0f, ctx);

    cmdBuf->bindPipeline(_pipeline.get());

    uint32_t width  = ctx->extent.width;
    uint32_t height = ctx->extent.height;

    float viewportY      = 0.0f;
    float viewportHeight = static_cast<float>(height);
    if (bReverseViewportY) {
        viewportY      = static_cast<float>(height);
        viewportHeight = -static_cast<float>(height);
    }

    cmdBuf->setViewport(0.0f, viewportY, static_cast<float>(width), viewportHeight, 0.0f, 1.0f);
    cmdBuf->setScissor(0, 0, width, height);

    updateFrameDS(ctx);

    uint32_t          materialCount = MaterialFactory::get()->getMaterialSize<UnlitMaterial>();
    std::vector<bool> updatedMaterial(materialCount);

    for (entt::entity entity : view)
    {
        const auto& [umc, meshComp, tc] = view.get(entity);

        // Get runtime material from component
        UnlitMaterial* material = umc.getMaterial();
        if (!material || material->getIndex() < 0) {
            Entity* entityPtr = scene->getEntityByEnttID(entity);
            YA_CORE_WARN("UnlitMaterialSystem: Entity '{}' has no valid material",
                         entityPtr ? entityPtr->getName() : "Unknown");
            continue;
        }

        Entity* entityPtr  = scene->getEntityByEnttID(entity);
        _ctxEntityDebugStr = std::format("{} (Mat: {})",
                                         entityPtr ? entityPtr->getName() : "Unknown",
                                         material->getLabel());

        uint32_t            materialInstanceIndex = material->getIndex();
        DescriptorSetHandle paramDS               = _matPool.paramDS(materialInstanceIndex);
        DescriptorSetHandle resourceDS            = _matPool.resourceDS(materialInstanceIndex);

        if (!updatedMaterial[materialInstanceIndex]) {
            _matPool.flushDirty(
                material,
                _bDescriptorPoolRecreated,
                [&](IBuffer* ubo, UnlitMaterial* mat) {
                    updateMaterialParamUBO(ubo, mat);
                },
                [&](DescriptorSetHandle ds, UnlitMaterial* mat) {
                    updateMaterialResourceDS(ds, mat);
                });

            updatedMaterial[materialInstanceIndex] = true;
        }

        // bind descriptor set
        std::vector<DescriptorSetHandle> descSets{
            _frameDSs[getSlot()],
            paramDS,
            resourceDS,
        };
        cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, descSets);

        // update push constant
        UnlitMaterialSystem::PushConstant pushConst{
            .modelMatrix = tc.getTransform(),
        };
        cmdBuf->pushConstants(_pipelineLayout.get(),
                              EShaderStage::Vertex,
                              0,
                              sizeof(UnlitMaterialSystem::PushConstant),
                              &pushConst);

        // draw mesh from MeshComponent
        Mesh* mesh = meshComp.getMesh();
        if (mesh) {
            mesh->draw(cmdBuf);
        }
    }

    // Reset force update flag after rendering
    _bDescriptorPoolRecreated = false;
    advanceSlot();
}


void UnlitMaterialSystem::updateFrameDS(const FrameContext* ctx)
{
    auto app    = getApp();
    auto render = getRender();

    FrameUBO ubo{};
    ubo.projMat    = ctx->projection;
    ubo.viewMat    = ctx->view;
    ubo.resolution = glm::ivec2(ctx->extent.width, ctx->extent.height);
    ubo.frameIdx   = app->getFrameIndex();
    ubo.time       = (float)app->getElapsedTimeMS() / 1000.0f;

    uint32_t slot = getSlot();
    _frameUBOs[slot]->writeData(&ubo, sizeof(ubo), 0);

    DescriptorBufferInfo bufferInfo(BufferHandle(_frameUBOs[slot]->getHandle()), 0, sizeof(FrameUBO));

    auto* descriptorHelper = render->getDescriptorHelper();
    descriptorHelper->updateDescriptorSets(
        {
            IDescriptorSetHelper::genBufferWrite(
                _frameDSs[slot],
                0,
                0,
                EPipelineDescriptorType::UniformBuffer,
                {bufferInfo}),
        },
        {});
}

void UnlitMaterialSystem::updateMaterialParamUBO(IBuffer* paramUBO, UnlitMaterial* material)
{
    auto render = getRender();

    const auto& params = material->getParams();
    paramUBO->writeData(&params, sizeof(material_param_t), 0);
}

void UnlitMaterialSystem::updateMaterialResourceDS(DescriptorSetHandle ds, UnlitMaterial* material)
{
    auto render = getRender();

    YA_CORE_ASSERT(ds.ptr != nullptr, "descriptor set is null: {}", _ctxEntityDebugStr);

    DescriptorImageInfo img0(material->getImageViewHandle(UnlitMaterial::BaseColor0),
                             material->getSamplerHandle(UnlitMaterial::BaseColor0),
                             EImageLayout::ShaderReadOnlyOptimal);
    DescriptorImageInfo img1(material->getImageViewHandle(UnlitMaterial::BaseColor1),
                             material->getSamplerHandle(UnlitMaterial::BaseColor1),
                             EImageLayout::ShaderReadOnlyOptimal);

    render
        ->getDescriptorHelper()
        ->updateDescriptorSets(
            {
                IDescriptorSetHelper::genImageWrite(ds, 0, 0, EPipelineDescriptorType::CombinedImageSampler, {img0}),
                IDescriptorSetHelper::genImageWrite(ds, 1, 0, EPipelineDescriptorType::CombinedImageSampler, {img1}),
            },
            {});
}

} // namespace ya
