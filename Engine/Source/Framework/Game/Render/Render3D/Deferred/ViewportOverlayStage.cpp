#include "ViewportOverlayStage.h"

#include "Core/Profiling/Instrumentor.h"

#include "Core/Math/Geometry.h"
#include "Core/Math/Math.h"
#include "RHI/Backend/TextureLibrary.h"
#include "Gameplay/Systems/Components/DirectionComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "Scene3D/TransformComponent.h"
#include "Render3D/Material/SimpleMaterial.h"
#include "Render/Adapters/GameplayResourceBinding.h"
#include "RHI/Backend/Vulkan/VulkanRender.h"
#include "RHI/Core/RenderResourceFactory.h"

#include "Resource/Mesh/PrimitiveMeshCache.h"

#include "Scene/Core/Scene.h"


#include <algorithm>
#include <format>
#include <glm/gtc/matrix_transform.hpp>

namespace ya
{

namespace
{

constexpr uint32_t BILLBOARD_TEXTURE_SET_SIZE = 16;

bool hasDebugSkinningDrawItem(const std::vector<RenderDrawItem>& items)
{
    return std::ranges::any_of(items, [](const RenderDrawItem& item)
                               { return item.mesh && item.mesh->hasSkinningVertexBuffer(); });
}

void drawDebugSkinningItems(DebugSkinning&                     debugSkinning,
                            ICommandBuffer*                    cmdBuf,
                            const std::vector<RenderDrawItem>& items,
                            uint32_t                           vpW,
                            uint32_t                           vpH,
                            const RenderFrameData&             fd)
{
    for (const auto& item : items) {
        if (!item.mesh || !item.mesh->hasSkinningVertexBuffer()) continue;
        debugSkinning.draw(cmdBuf,
                           item.mesh,
                           vpW,
                           vpH,
                           fd.projection,
                           fd.view,
                           item.worldMatrix);
    }
}

} // namespace

void ViewportOverlayStage::setDebugRenderSystem(DebugRenderSystem* debugRenderSystem)
{
    _debugRenderSystem = debugRenderSystem;
}

void ViewportOverlayStage::refreshPipelineFormats(const DeferredAttachmentFormats& formats)
{
    if (!formats.hasColor()) {
        return;
    }
    const auto colorFormat = formats.colorFormats.front();
    const auto depthFormat = formats.depthFormat.value_or(EFormat::Undefined);

    if (_skyboxPipeline) {
        auto ci                                         = _skyboxPipeline->getDesc();
        ci.pipelineRenderingInfo.colorAttachmentFormats = {colorFormat};
        ci.pipelineRenderingInfo.depthAttachmentFormat  = depthFormat;
        _skyboxPipeline->updateDesc(std::move(ci));
    }

    if (_billboardPipeline) {
        auto ci                                         = _billboardPipeline->getDesc();
        ci.pipelineRenderingInfo.colorAttachmentFormats = {colorFormat};
        ci.pipelineRenderingInfo.depthAttachmentFormat  = depthFormat;
        _billboardPipeline->updateDesc(std::move(ci));
    }

    if (_overlayPipeline) {
        auto ci                                         = _overlayPipeline->getDesc();
        ci.pipelineRenderingInfo.colorAttachmentFormats = {colorFormat};
        ci.pipelineRenderingInfo.depthAttachmentFormat  = depthFormat;
        _overlayPipeline->updateDesc(std::move(ci));
    }

    if (_debugRenderSystem) {
        _debugRenderSystem->refreshPipelineFormats(formats);
    }
    _debugSkinning.refreshPipelineFormats(formats);
}

// ═══════════════════════════════════════════════════════════════════════
// Init
// ═══════════════════════════════════════════════════════════════════════

void ViewportOverlayStage::init(IRender* render, stdptr<IDescriptorSetLayout> skyboxFrameDSL)
{
    _render = render;
    _billboardMesh = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Quad);
    _directionCone = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cone);
    _directionCylinder = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cylinder);
    YA_CORE_ASSERT(_billboardMesh != nullptr, "ViewportOverlayStage requires billboard quad mesh");
    YA_CORE_ASSERT(_directionCone != nullptr, "ViewportOverlayStage requires direction cone mesh");
    YA_CORE_ASSERT(_directionCylinder != nullptr, "ViewportOverlayStage requires direction cylinder mesh");
    YA_CORE_ASSERT(_debugRenderSystem != nullptr, "ViewportOverlayStage requires debug render system instance");
    initSkybox(std::move(skyboxFrameDSL));
    initBillboards();
    initOverlay();
    _debugRenderSystem->init(_render);
    _debugRenderSystem->setReverseViewportY(bReverseViewportY);
    _debugSkinning.init(_render);
    _debugSkinning.bReverseViewportY = bReverseViewportY;
}

void ViewportOverlayStage::initSkybox(stdptr<IDescriptorSetLayout> skyboxFrameDSL)
{
    _skyboxFrameDSL = std::move(skyboxFrameDSL);
    YA_CORE_ASSERT(_skyboxFrameDSL != nullptr, "ViewportOverlayStage requires skybox frame DSL");
    _skyboxResourceDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "SkyboxOverlay_Resource_DSL",
            .set      = 1,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment}},
        });

    // Pipeline layout
    _skyboxPPL = IPipelineLayout::create(_render, "SkyboxOverlay_PPL", {}, {_skyboxFrameDSL, _skyboxResourceDSL});

    // Pipeline
    GraphicsPipelineCreateInfo ci{
        .pipelineRenderingInfo = {
            .label                  = "Deferred Skybox Overlay",
            .colorAttachmentFormats = {LINEAR_FORMAT},
            .depthAttachmentFormat  = DEPTH_FORMAT,
        },
        .pipelineLayout = _skyboxPPL.get(),
        .shaderDesc     = ShaderDesc{
            .shaderName        = "Skybox.glsl",
            .vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)}},
            .vertexAttributes  = {
                {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
                {.bufferSlot = 0, .location = 1, .format = EVertexAttributeFormat::Float2, .offset = offsetof(ya::Vertex, texCoord0)},
                {.bufferSlot = 0, .location = 2, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, normal)},
            },
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::Front, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = false, .depthCompareOp = ECompareOp::LessOrEqual},
        .colorBlendState    = {.attachments = {{.index = 0, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A}}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    _skyboxPipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_skyboxPipeline && _skyboxPipeline->recreate(ci), "Failed to create Skybox overlay pipeline");

}

void ViewportOverlayStage::initBillboards()
{
    _billboardFrameDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "BillboardOverlay_Frame_DSL",
            .set      = 0,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex}},
        });

    _billboardTextureDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "BillboardOverlay_Texture_DSL",
            .set      = 1,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = BILLBOARD_TEXTURE_SET_SIZE, .stageFlags = EShaderStage::Fragment}},
        });

    _billboardPPL = IPipelineLayout::create(
        _render,
        "BillboardOverlay_PPL",
        {PushConstantRange{.offset = 0, .size = sizeof(BillboardPushConstant), .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment}},
        {_billboardFrameDSL, _billboardTextureDSL});

    GraphicsPipelineCreateInfo ci{
        .pipelineRenderingInfo = {
            .label                  = "Deferred Billboard Overlay",
            .colorAttachmentFormats = {LINEAR_FORMAT},
            .depthAttachmentFormat  = DEPTH_FORMAT,
        },
        .pipelineLayout = _billboardPPL.get(),
        .shaderDesc     = ShaderDesc{
            .shaderName        = "Misc/BillboardWorld.slang",
            .vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)}},
            .vertexAttributes  = {
                {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
                {.bufferSlot = 0, .location = 1, .format = EVertexAttributeFormat::Float2, .offset = offsetof(ya::Vertex, texCoord0)},
                {.bufferSlot = 0, .location = 2, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, normal)},
            },
            .defines = {
                std::format("TEXTURE_SET_SIZE {}", BILLBOARD_TEXTURE_SET_SIZE),
            },
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::None, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = false, .depthCompareOp = ECompareOp::LessOrEqual},
        .colorBlendState    = {.attachments = {{
            .index               = 0,
            .bBlendEnable        = true,
            .srcColorBlendFactor = EBlendFactor::SrcAlpha,
            .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
            .colorBlendOp        = EBlendOp::Add,
            .srcAlphaBlendFactor = EBlendFactor::One,
            .dstAlphaBlendFactor = EBlendFactor::OneMinusSrcAlpha,
            .alphaBlendOp        = EBlendOp::Add,
            .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
        }}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    _billboardPipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_billboardPipeline && _billboardPipeline->recreate(ci), "Failed to create billboard overlay pipeline");

    _billboardDSP = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
                                                      .label     = "BillboardOverlay_DSP",
                                                      .maxSets   = MAX_FLIGHTS_IN_FLIGHT + 1,
                                                      .poolSizes = {
                                                          {.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT},
                                                          {.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = BILLBOARD_TEXTURE_SET_SIZE},
                                                      },
                                                  });

    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        _billboardFrameUBO[i] = _render->getResourceFactory()->createBuffer(BufferCreateInfo{
                                                             .label       = std::format("BillboardOverlay_Frame_UBO_{}", i),
                                                             .usage       = EBufferUsage::UniformBuffer,
                                                             .size        = sizeof(BillboardFrameUBO),
                                                             .memoryUsage = EMemoryUsage::CpuToGpu,
                                                         });
        _billboardFrameDS[i] = _billboardDSP->allocateDescriptorSets(_billboardFrameDSL);
        _render->getDescriptorHelper()->updateDescriptorSets({
            IDescriptorSetHelper::writeOneUniformBuffer(_billboardFrameDS[i], 0, _billboardFrameUBO[i].get()),
        });
    }

    _billboardTextureDS = _billboardDSP->allocateDescriptorSets(_billboardTextureDSL);
}

void ViewportOverlayStage::initOverlay()
{
    constexpr auto pcSize = sizeof(OverlayPushConstant);
    _overlayPPL           = IPipelineLayout::create(
        _render, "SimpleMaterialOverlay_PPL", {PushConstantRange{.offset = 0, .size = pcSize, .stageFlags = EShaderStage::Vertex}}, {});

    GraphicsPipelineCreateInfo ci{
        .pipelineRenderingInfo = {
            .label                  = "Deferred SimpleMaterial Overlay",
            .colorAttachmentFormats = {LINEAR_FORMAT},
            .depthAttachmentFormat  = DEPTH_FORMAT,
        },
        .pipelineLayout = _overlayPPL.get(),
        .shaderDesc     = ShaderDesc{
            .shaderName        = "Test/SimpleMaterial.glsl",
            .vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)}},
            .vertexAttributes  = {
                {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
                {.bufferSlot = 0, .location = 1, .format = EVertexAttributeFormat::Float2, .offset = offsetof(ya::Vertex, texCoord0)},
                {.bufferSlot = 0, .location = 2, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, normal)},
            },
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.polygonMode = EPolygonMode::Fill, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = true, .depthCompareOp = ECompareOp::Less},
        .colorBlendState    = {.attachments = {{.index = 0, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A}}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    _overlayPipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_overlayPipeline && _overlayPipeline->recreate(ci), "Failed to create SimpleMaterial overlay pipeline");
}

void ViewportOverlayStage::destroy()
{
    _skyboxPipeline.reset();
    _skyboxPPL.reset();
    _skyboxFrameDSL.reset();
    _skyboxResourceDSL.reset();

    _billboardPipeline.reset();
    _billboardPPL.reset();
    _billboardFrameDSL.reset();
    _billboardTextureDSL.reset();
    _billboardDSP.reset();
    _billboardTextureDS = {};
    _billboardTextureBindings.clear();
    _billboardMesh = nullptr;
    for (auto& ubo : _billboardFrameUBO) ubo.reset();

    _overlayPipeline.reset();
    _overlayPPL.reset();
    _directionCone = nullptr;
    _directionCylinder = nullptr;
    _debugRenderSystem = nullptr;
    _debugSkinning.destroy();
    _render = nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// Prepare
// ═══════════════════════════════════════════════════════════════════════

void ViewportOverlayStage::prepare(const RenderStageContext& ctx)
{
    YA_PROFILE_FUNCTION();
    if (_skyboxPipeline) {
        _skyboxPipeline->beginFrame();
    }
    if (_overlayPipeline) {
        _overlayPipeline->beginFrame();
    }
    if (_billboardPipeline) {
        _billboardPipeline->beginFrame();
    }
    if (_debugRenderSystem) {
        _debugRenderSystem->beginFrame();
    }
    _debugSkinning.beginFrame();

    if (!ctx.frameData) return;

    BillboardFrameUBO billboardUbo{
        .viewProjection = ctx.frameData->projection * ctx.frameData->view,
        .view           = ctx.frameData->view,
    };
    _billboardFrameUBO[ctx.flightIndex]->writeData(&billboardUbo, sizeof(billboardUbo), 0);
}

ViewportOverlayStage::SkyboxFrameUBO ViewportOverlayStage::buildSkyboxFrameData(const RenderStageContext& ctx) const
{
    YA_CORE_ASSERT(ctx.frameData != nullptr, "ViewportOverlayStage requires frame data to build skybox parameters");
    return SkyboxFrameUBO{
        .proj = ctx.frameData->projection,
        .view = FMath::dropTranslation(ctx.frameData->view),
    };
}

// ═══════════════════════════════════════════════════════════════════════
// Execute
// ═══════════════════════════════════════════════════════════════════════

void ViewportOverlayStage::execute(const RenderStageContext& ctx)
{
    YA_CORE_WARN("ViewportOverlayStage::execute(ctx) is a conformance stub; graph passes must use the parameterized overloads");
}

void ViewportOverlayStage::executeSkybox(const RenderStageContext& ctx, const FrameInputs::SkyboxInput& skyboxInput)
{
    if (!ctx.cmdBuf || !ctx.frameData) return;

    drawSkybox(ctx, skyboxInput);
}

void ViewportOverlayStage::executeOverlay(const RenderStageContext& ctx, const FrameInputs& frameInputs)
{
    if (!ctx.cmdBuf || !ctx.frameData) return;

    drawBillboards(ctx, frameInputs);
    drawOverlay(ctx, frameInputs);
}

uint32_t ViewportOverlayStage::resolveBillboardTextureIndex(const TextureBinding& binding)
{
    const auto matches = [&](const TextureBinding& existing)
    {
        return existing.getImageViewHandle() == binding.getImageViewHandle() &&
               existing.getSamplerHandle() == binding.getSamplerHandle();
    };

    for (uint32_t index = 0; index < _billboardTextureBindings.size(); ++index) {
        if (matches(_billboardTextureBindings[index])) {
            return index;
        }
    }

    if (_billboardTextureBindings.size() >= BILLBOARD_TEXTURE_SET_SIZE) {
        return 0;
    }

    _billboardTextureBindings.push_back(binding);
    return static_cast<uint32_t>(_billboardTextureBindings.size() - 1);
}

void ViewportOverlayStage::updateBillboardTextures(const FrameInputs& frameInputs)
{
    _billboardTextureBindings.clear();
    _billboardTextureBindings.push_back(TextureBinding{
        .texture = TextureLibrary::get().getWhiteTexture(),
        .sampler = TextureLibrary::get().getDefaultSampler(),
    });
    if (!_billboardTextureDS) {
        return;
    }

    for (const auto& billboard : frameInputs.billboards) {
        resolveBillboardTextureIndex(billboard.textureBinding);
    }

    std::vector<DescriptorImageInfo> imageInfos;
    imageInfos.reserve(BILLBOARD_TEXTURE_SET_SIZE);
    const TextureBinding fallbackBinding = _billboardTextureBindings.front();
    for (uint32_t index = 0; index < BILLBOARD_TEXTURE_SET_SIZE; ++index) {
        const TextureBinding& binding = index < _billboardTextureBindings.size() ? _billboardTextureBindings[index] : fallbackBinding;
        imageInfos.push_back(DescriptorImageInfo{
            .imageView   = binding.getImageViewHandle(),
            .sampler     = binding.getSamplerHandle(),
            .imageLayout = EImageLayout::ShaderReadOnlyOptimal,
        });
    }

    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::genImageWrite(_billboardTextureDS, 0, 0, EPipelineDescriptorType::CombinedImageSampler, std::move(imageInfos)),
    });
}

void ViewportOverlayStage::drawBillboards(const RenderStageContext& ctx, const FrameInputs& frameInputs)
{
    if (frameInputs.billboards.empty() || !_billboardPipeline || !_billboardPPL || !_billboardMesh) {
        return;
    }

    auto* cmdBuf = ctx.cmdBuf;
    const auto vpW = ctx.viewportExtent.width;
    const auto vpH = ctx.viewportExtent.height;
    if (vpW == 0 || vpH == 0) {
        return;
    }

    cmdBuf->debugBeginLabel("BillboardOverlay");
    cmdBuf->bindPipeline(_billboardPipeline.get());

    float viewportY      = 0.0f;
    float viewportHeight = static_cast<float>(vpH);
    if (bReverseViewportY) {
        viewportY      = static_cast<float>(vpH);
        viewportHeight = -static_cast<float>(vpH);
    }
    cmdBuf->setViewport(0.0f, viewportY, static_cast<float>(vpW), viewportHeight);
    cmdBuf->setScissor(0, 0, vpW, vpH);
    cmdBuf->bindDescriptorSets(_billboardPPL.get(), 0, {_billboardFrameDS[ctx.flightIndex], _billboardTextureDS});

    for (const auto& billboard : frameInputs.billboards) {
        BillboardPushConstant pc{};
        pc.worldCenter    = billboard.worldCenter;
        pc.worldDirection = billboard.worldDirection;
        pc.worldSize      = billboard.worldSize;
        pc.tint           = billboard.tint;
        pc.textureIndex   = resolveBillboardTextureIndex(billboard.textureBinding);

        cmdBuf->pushConstants(_billboardPPL.get(), EShaderStage::Vertex | EShaderStage::Fragment, 0, sizeof(pc), &pc);
        _billboardMesh->drawStatic(cmdBuf);
    }

    cmdBuf->debugEndLabel();
}

void ViewportOverlayStage::drawSkybox(const RenderStageContext& ctx, const FrameInputs::SkyboxInput& skyboxInput)
{
    auto* cmdBuf = ctx.cmdBuf;
    auto  vpW    = ctx.viewportExtent.width;
    auto  vpH    = ctx.viewportExtent.height;
    if (vpW == 0 || vpH == 0) return;

    // Check if skybox is available
    if (!skyboxInput.bAvailable || !skyboxInput.mesh || !skyboxInput.frameDescriptorSet) return;

    cmdBuf->debugBeginLabel("Skybox");

    cmdBuf->bindPipeline(_skyboxPipeline.get());

    float viewportY      = 0.0f;
    float viewportHeight = static_cast<float>(vpH);
    if (bReverseViewportY) {
        viewportY      = static_cast<float>(vpH);
        viewportHeight = -static_cast<float>(vpH);
    }
    cmdBuf->setViewport(0.0f, viewportY, static_cast<float>(vpW), viewportHeight, 0.0f, 1.0f);
    cmdBuf->setScissor(0, 0, vpW, vpH);

    cmdBuf->bindDescriptorSets(_skyboxPPL.get(), 0, {skyboxInput.frameDescriptorSet, skyboxInput.descriptorSet});
    skyboxInput.mesh->draw(cmdBuf);

    cmdBuf->debugEndLabel();
}

void ViewportOverlayStage::drawOverlay(const RenderStageContext& ctx, const FrameInputs& frameInputs)
{
    auto* cmdBuf = ctx.cmdBuf;
    auto  vpW    = ctx.viewportExtent.width;
    auto  vpH    = ctx.viewportExtent.height;
    if (vpW == 0 || vpH == 0) return;

    const auto& fd                     = *ctx.frameData;
    if (!_debugRenderSystem) {
        return;
    }
    auto& debugSystem = *_debugRenderSystem;
    debugSystem.setReverseViewportY(bReverseViewportY);
    _debugSkinning.bReverseViewportY   = bReverseViewportY;

    // Simple material entities (from snapshot)
    const auto& staticBuckets  = fd.drawBuckets.staticMeshes;
    const auto& skinnedBuckets = fd.drawBuckets.skinnedMeshes;
    bool hasSimple = !staticBuckets.simpleDrawItems.empty() || !skinnedBuckets.simpleDrawItems.empty();

    bool hasDebugSkinning = _debugSkinning.bEnabled &&
                            (hasDebugSkinningDrawItem(skinnedBuckets.phongDrawItems) ||
                             hasDebugSkinningDrawItem(skinnedBuckets.simpleDrawItems) ||
                             hasDebugSkinningDrawItem(skinnedBuckets.fallbackDrawItems) ||
                             hasDebugSkinningDrawItem(skinnedBuckets.pbrDrawItems));

    const bool hasDirection = !frameInputs.directionGizmos.empty();

    if (!hasSimple && !hasDirection && !hasDebugSkinning) return;

    cmdBuf->debugBeginLabel("ForwardOverlay");

    cmdBuf->bindPipeline(_overlayPipeline.get());

    float viewportY      = 0.0f;
    float viewportHeight = static_cast<float>(vpH);
    if (bReverseViewportY) {
        viewportY      = static_cast<float>(vpH);
        viewportHeight = -static_cast<float>(vpH);
    }
    cmdBuf->setViewport(0.0f, viewportY, static_cast<float>(vpW), viewportHeight);
    cmdBuf->setScissor(0, 0, vpW, vpH);

    _overlayPC.view       = fd.view;
    _overlayPC.projection = fd.projection;

    // Draw simple material entities from snapshot
    auto drawSimpleBucket = [&](const std::vector<RenderDrawItem>& items, bool bSkinned)
    {
        for (const auto& item : items) {
            if (!item.mesh || !item.material) continue;
            auto* mat            = static_cast<SimpleMaterial*>(item.material);
            _overlayPC.model     = item.worldMatrix;
            _overlayPC.colorType = mat->colorType;
            cmdBuf->pushConstants(_overlayPPL.get(), EShaderStage::Vertex, 0, sizeof(OverlayPushConstant), &_overlayPC);
            if (bSkinned) {
                item.mesh->drawSkinned(cmdBuf);
            }
            else {
                item.mesh->drawStatic(cmdBuf);
            }
        }
    };
    drawSimpleBucket(staticBuckets.simpleDrawItems, false);
    drawSimpleBucket(skinnedBuckets.simpleDrawItems, true);

    if (_debugSkinning.bEnabled) {
        drawDebugSkinningItems(_debugSkinning, cmdBuf, skinnedBuckets.phongDrawItems, vpW, vpH, fd);
        drawDebugSkinningItems(_debugSkinning, cmdBuf, skinnedBuckets.simpleDrawItems, vpW, vpH, fd);
        drawDebugSkinningItems(_debugSkinning, cmdBuf, skinnedBuckets.fallbackDrawItems, vpW, vpH, fd);
        drawDebugSkinningItems(_debugSkinning, cmdBuf, skinnedBuckets.pbrDrawItems, vpW, vpH, fd);
    }

    // Draw direction cones/cylinders from setup snapshot
    if (hasDirection && (!_directionCone || !_directionCylinder)) return;

    _overlayPC.colorType = _defaultColorType;
    for (const auto& gizmo : frameInputs.directionGizmos) {
        _overlayPC.model = gizmo.coneModel;
        cmdBuf->pushConstants(_overlayPPL.get(), EShaderStage::Vertex, 0, sizeof(OverlayPushConstant), &_overlayPC);
        _directionCone->draw(cmdBuf);

        _overlayPC.model = gizmo.cylinderModel;
        cmdBuf->pushConstants(_overlayPPL.get(), EShaderStage::Vertex, 0, sizeof(OverlayPushConstant), &_overlayPC);
        _directionCylinder->draw(cmdBuf);

        debugSystem.addConeImmediate(gizmo.coneModel,
                                     glm::vec4(0.9f, 0.6f, 0.1f, 1.0f));
        debugSystem.addCylinderImmediate(gizmo.cylinderModel,
                                         glm::vec4(0.1f, 0.9f, 0.9f, 1.0f));
        debugSystem.addLineImmediate(gizmo.lineStart, gizmo.lineEnd, glm::vec4(1.0f, 0.2f, 0.2f, 1.0f));
    }

    debugSystem.draw(cmdBuf, vpW, vpH, fd.projection, fd.view);

    cmdBuf->debugEndLabel();
}

// ═══════════════════════════════════════════════════════════════════════
// GUI
// ═══════════════════════════════════════════════════════════════════════

} // namespace ya
