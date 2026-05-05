#include "Runtime/App/Common/ShadowStage.h"

#include "Render/Render.h"
#include "Render/RenderFrameData.h"
#include "imgui.h"

#include <algorithm>

namespace
{
constexpr uint32_t SHADOW_SKINNING_SET_INDEX = 1;
}

namespace ya
{

void ShadowStage::setRenderTarget(const stdptr<IRenderTarget>& rt)
{
    _shadowMapRT = rt;
    if (_shadowMapRT) {
        _shadowExtent = _shadowMapRT->getExtent();
    }
    refreshPipelineFromRenderTarget();
}

void ShadowStage::refreshPipelineFromRenderTarget()
{
    if (!_shadowMapRT) {
        return;
    }

    _shadowExtent = _shadowMapRT->getExtent();
    if (!_render) {
        return;
    }

    const auto& depthDesc = _shadowMapRT->getDepthAttachmentDesc();
    if (!depthDesc.has_value()) {
        return;
    }

    rebuildShadowLayerTextures();
    _pipelineCI.pipelineRenderingInfo.depthAttachmentFormat = depthDesc->format;
    if (_staticVariant.pipeline) {
        auto ci                = _pipelineCI;
        ci.pipelineLayout      = _staticVariant.pipelineLayout.get();
        ci.shaderDesc.vertexBufferDescs = {
            VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)},
        };
        ci.shaderDesc.vertexAttributes = {
            {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
        };
        ci.shaderDesc.defines.clear();
        _staticVariant.pipeline->updateDesc(ci);
    }
    if (_skinnedVariant.pipeline) {
        auto ci                = _pipelineCI;
        ci.pipelineLayout      = _skinnedVariant.pipelineLayout.get();
        ci.shaderDesc.vertexBufferDescs = {
            VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)},
            VertexBufferDescription{.slot = 1, .pitch = sizeof(ya::SkeletonMeshVertex)},
        };
        ci.shaderDesc.vertexAttributes = {
            {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
            {.bufferSlot = 1, .location = 4, .format = EVertexAttributeFormat::Int32x4, .offset = offsetof(ya::SkeletonMeshVertex, boneIDs)},
            {.bufferSlot = 1, .location = 5, .format = EVertexAttributeFormat::Float4, .offset = offsetof(ya::SkeletonMeshVertex, weights)},
        };
        ci.shaderDesc.defines = {"ENABLE_SKINNING 1", "SKINNING_SET_INDEX 1"};
        _skinnedVariant.pipeline->updateDesc(ci);
    }
}

void ShadowStage::rebuildShadowLayerTextures()
{
    _directionalDepthTexture.reset();
    _directionalDepthView.reset();
    for (auto& faceViews : _pointFaceDepthViews) {
        for (auto& faceView : faceViews) {
            faceView.reset();
        }
    }
    for (auto& faceTextures : _pointFaceDepthTextures) {
        for (auto& faceTexture : faceTextures) {
            faceTexture.reset();
        }
    }

    if (!_render || !_shadowMapRT) {
        return;
    }

    auto* frameBuffer  = _shadowMapRT->getCurFrameBuffer();
    auto* depthTexture = frameBuffer ? frameBuffer->getDepthTexture() : nullptr;
    if (!depthTexture) {
        return;
    }

    auto shadowImage = depthTexture->getImageShared();
    if (!shadowImage) {
        return;
    }

    auto textureFactory = _render->getTextureFactory();
    _directionalDepthView = textureFactory->createImageView(
        shadowImage,
        ImageViewCreateInfo{
            .label          = "ShadowStage_DirectionalDepthView",
            .viewType       = EImageViewType::View2D,
            .aspectFlags    = EImageAspect::Depth,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        });
    if (_directionalDepthView) {
        _directionalDepthTexture = Texture::wrap(shadowImage, _directionalDepthView, "ShadowStage_DirectionalDepthTexture");
    }

    for (uint32_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; ++lightIndex) {
        for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
            auto view = textureFactory->createImageView(
                shadowImage,
                ImageViewCreateInfo{
                    .label          = std::format("ShadowStage_Point[{}]_Face[{}]_DepthView", lightIndex, faceIndex),
                    .viewType       = EImageViewType::View2D,
                    .aspectFlags    = EImageAspect::Depth,
                    .baseMipLevel   = 0,
                    .levelCount     = 1,
                    .baseArrayLayer = 1 + lightIndex * 6 + faceIndex,
                    .layerCount     = 1,
                });
            _pointFaceDepthViews[lightIndex][faceIndex] = view;
            if (view) {
                _pointFaceDepthTextures[lightIndex][faceIndex] = Texture::wrap(
                    shadowImage,
                    view,
                    std::format("ShadowStage_Point[{}]_Face[{}]_DepthTexture", lightIndex, faceIndex));
            }
        }
    }
}

void ShadowStage::recreatePipelines()
{
    _staticVariant.pipelineLayout = IPipelineLayout::create(
        _render,
        "ShadowStage_Static_PPL",
        {PushConstantRange{.offset = 0, .size = sizeof(ModelPushConstant), .stageFlags = EShaderStage::Vertex}},
        {_frameDSL});

    _skinnedVariant.pipelineLayout = IPipelineLayout::create(
        _render,
        "ShadowStage_Skinned_PPL",
        {PushConstantRange{.offset = 0, .size = sizeof(ModelPushConstant), .stageFlags = EShaderStage::Vertex}},
        {_frameDSL, _skinningDSL});

    auto staticCI                    = _pipelineCI;
    staticCI.pipelineLayout          = _staticVariant.pipelineLayout.get();
    staticCI.shaderDesc.vertexBufferDescs = {
        VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)},
    };
    staticCI.shaderDesc.vertexAttributes = {
        {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
    };
    staticCI.shaderDesc.defines.clear();
    _staticVariant.pipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_staticVariant.pipeline && _staticVariant.pipeline->recreate(staticCI), "Failed to create static ShadowStage pipeline");

    auto skinnedCI                    = _pipelineCI;
    skinnedCI.pipelineLayout          = _skinnedVariant.pipelineLayout.get();
    skinnedCI.shaderDesc.vertexBufferDescs = {
        VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)},
        VertexBufferDescription{.slot = 1, .pitch = sizeof(ya::SkeletonMeshVertex)},
    };
    skinnedCI.shaderDesc.vertexAttributes = {
        {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
        {.bufferSlot = 1, .location = 4, .format = EVertexAttributeFormat::Int32x4, .offset = offsetof(ya::SkeletonMeshVertex, boneIDs)},
        {.bufferSlot = 1, .location = 5, .format = EVertexAttributeFormat::Float4, .offset = offsetof(ya::SkeletonMeshVertex, weights)},
    };
    skinnedCI.shaderDesc.defines = {"ENABLE_SKINNING 1", "SKINNING_SET_INDEX 1"};
    _skinnedVariant.pipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_skinnedVariant.pipeline && _skinnedVariant.pipeline->recreate(skinnedCI), "Failed to create skinned ShadowStage pipeline");
}

void ShadowStage::ensureSkinningCapacity(uint32_t paletteCount)
{
    const uint32_t requiredCount = std::max(1u, paletteCount);
    if (_dsp && requiredCount <= _skinningCapacity && _skinningDS[0]) {
        return;
    }

    _skinningCapacity = std::max(requiredCount, _skinningCapacity == 0 ? 16u : _skinningCapacity);
    while (_skinningCapacity < requiredCount) {
        _skinningCapacity *= 2;
    }

    const uint32_t bufferSize = static_cast<uint32_t>(static_cast<uint64_t>(_skinningCapacity) * sizeof(RenderSkinningPalette));
    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        _skinningSSBO[i] = IBuffer::create(
            _render,
            BufferCreateInfo{
                .label       = std::format("ShadowStage_Skinning_SSBO_{}", i),
                .usage       = EBufferUsage::StorageBuffer,
                .size        = bufferSize,
                .memoryUsage = EMemoryUsage::CpuToGpu,
            });

        if (!_skinningDS[i]) {
            _skinningDS[i] = _dsp->allocateDescriptorSets(_skinningDSL);
        }

        _render->getDescriptorHelper()->updateDescriptorSets(
            {IDescriptorSetHelper::genSingleBufferWrite(_skinningDS[i], 0, EPipelineDescriptorType::StorageBuffer, _skinningSSBO[i].get())},
            {});
    }
}

void ShadowStage::init(IRender* render)
{
    _render = render;

    _frameDSL = IDescriptorSetLayout::create(
        _render,
        {
            DescriptorSetLayoutDesc{
                .label    = "ShadowStage_Frame_DSL",
                .set      = 0,
                .bindings = {{
                    .binding         = 0,
                    .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                    .descriptorCount = 1,
                    .stageFlags      = EShaderStage::Vertex | EShaderStage::Fragment,
                }},
            },
        });

    _skinningDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "ShadowStage_Skinning_DSL",
            .set      = SHADOW_SKINNING_SET_INDEX,
            .bindings = {{
                .binding         = 0,
                .descriptorType  = EPipelineDescriptorType::StorageBuffer,
                .descriptorCount = 1,
                .stageFlags      = EShaderStage::Vertex,
            }},
        });

    _pipelineCI = GraphicsPipelineCreateInfo{
        .pipelineRenderingInfo = {
            .label                 = "Shadow Map Generate",
            .depthAttachmentFormat = EFormat::D32_SFLOAT,
        },
        .shaderDesc     = ShaderDesc{
            .shaderName        = "CombineShadowMappingGenerate.slang",
            .vertexBufferDescs = {},
            .vertexAttributes  = {},
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::Front, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = true, .depthCompareOp = ECompareOp::LessOrEqual},
        .colorBlendState    = {.attachments = {}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    refreshPipelineFromRenderTarget();
    recreatePipelines();

    _dsp = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
        .label     = "ShadowStage_DSP",
        .maxSets   = MAX_FLIGHTS_IN_FLIGHT * (SHADOW_LAYER_COUNT + 1),
        .poolSizes = {
            {.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT * SHADOW_LAYER_COUNT},
            {.type = EPipelineDescriptorType::StorageBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT},
        },
    });

    FrameUBO initialData{};
    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        for (uint32_t layerIndex = 0; layerIndex < SHADOW_LAYER_COUNT; ++layerIndex) {
            _frameUBO[i][layerIndex] = IBuffer::create(
                _render,
                BufferCreateInfo{
                    .label       = std::format("ShadowStage_Frame_UBO_{}_{}", i, layerIndex),
                    .usage       = EBufferUsage::UniformBuffer,
                    .size        = sizeof(FrameUBO),
                    .memoryUsage = EMemoryUsage::CpuToGpu,
                });
            _frameUBO[i][layerIndex]->writeData(&initialData, sizeof(FrameUBO), 0);

            _frameDS[i][layerIndex] = _dsp->allocateDescriptorSets(_frameDSL);
            _render->getDescriptorHelper()->updateDescriptorSets({
                IDescriptorSetHelper::writeOneUniformBuffer(_frameDS[i][layerIndex], 0, _frameUBO[i][layerIndex].get()),
            });
        }
    }
}

void ShadowStage::destroy()
{
    for (auto& perFlightUBOs : _frameUBO) {
        for (auto& ubo : perFlightUBOs) {
            ubo.reset();
        }
    }
    for (auto& ssbo : _skinningSSBO) {
        ssbo.reset();
    }
    _directionalDepthTexture.reset();
    _directionalDepthView.reset();
    for (auto& faceViews : _pointFaceDepthViews) {
        for (auto& faceView : faceViews) {
            faceView.reset();
        }
    }
    for (auto& faceTextures : _pointFaceDepthTextures) {
        for (auto& faceTexture : faceTextures) {
            faceTexture.reset();
        }
    }
    _dsp.reset();
    _skinningDSL.reset();
    _frameDSL.reset();
    _staticVariant  = {};
    _skinnedVariant = {};
    _shadowMapRT.reset();
    _skinningCapacity = 0;
    _render = nullptr;
}

void ShadowStage::prepare(const RenderStageContext& ctx)
{
    if (_staticVariant.pipeline) {
        _staticVariant.pipeline->beginFrame();
    }
    if (_skinnedVariant.pipeline) {
        _skinnedVariant.pipeline->beginFrame();
    }

    if (!ctx.frameData) return;
    const auto& fd = *ctx.frameData;
    const uint32_t fi = ctx.flightIndex;

    const uint32_t pointLightCount = _bEnablePointLightShadow ? std::min(fd.numPointLights, _maxPointLightShadowCount) : 0;

    FrameUBO frameData{
        .directionalLightMatrix = fd.directionalLight.viewProjection,
        .numPointLights         = pointLightCount,
        .hasDirectionalLight    = fd.bHasDirectionalLight ? 1u : 0u,
    };
    _lastPreparedPointLightCount = pointLightCount;

    for (uint32_t i = 0; i < pointLightCount; ++i) {
        const auto& pl = fd.pointLights[i];
        const glm::vec3& pos = pl.position;
        const glm::mat4 faceProj = FMath::perspective(glm::radians(90.0f), 1.0f, pl.nearPlane, pl.farPlane);

        for (int face = ECubeFace::CubeFace_PosX; face < ECubeFace::CubeFace_Count; ++face) {
            glm::mat4 view{};
            const glm::vec3 down = glm::vec3{0, -1, 0};
            const glm::vec3 backward = {0, 0, 1};

            if constexpr (FMath::Vector::IsRightHanded) {
                switch ((ECubeFace)face) {
                case CubeFace_PosX: {
                    view = FMath::lookAt(pos, pos + glm::vec3(1, 0, 0), down);
                    break;
                }
                case CubeFace_NegX: {
                    view = FMath::lookAt(pos, pos + glm::vec3(-1, 0, 0), down);
                    break;
                }
                case CubeFace_PosY: {
                    view = FMath::lookAt(pos, pos + glm::vec3(0, 1, 0), backward);
                    break;
                }
                case CubeFace_NegY: {
                    view = FMath::lookAt(pos, pos + glm::vec3(0, -1, 0), -backward);
                    break;
                }
                case CubeFace_PosZ: {
                    view = FMath::lookAt(pos, pos + glm::vec3(0, 0, 1), down);
                    break;
                }
                case CubeFace_NegZ: {
                    view = FMath::lookAt(pos, pos + glm::vec3(0, 0, -1), down);
                    break;
                }
                case CubeFace_Count: {
                    UNREACHABLE();
                }
                }
            }

            frameData.pointLights[i].matrix[face] = faceProj * view;
            frameData.pointLights[i].pos          = pos;
            frameData.pointLights[i].farPlane     = pl.farPlane;
        }
    }

    _frameUBO[fi][0]->writeData(&frameData, sizeof(FrameUBO), 0);

    for (uint32_t lightIndex = 0; lightIndex < pointLightCount; ++lightIndex) {
        for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
            FrameUBO pointLightFrameData{
                .numPointLights      = 1,
                .hasDirectionalLight = 0,
            };
            pointLightFrameData.pointLights[0] = frameData.pointLights[lightIndex];
            pointLightFrameData.pointLights[0].matrix[0] = frameData.pointLights[lightIndex].matrix[faceIndex];
            _frameUBO[fi][1 + lightIndex * 6 + faceIndex]->writeData(&pointLightFrameData, sizeof(FrameUBO), 0);
        }
    }

    const auto& palettes = fd.skinningPalettes;
    ensureSkinningCapacity(static_cast<uint32_t>(palettes.size()));
    if (!palettes.empty()) {
        _skinningSSBO[fi]->writeData(palettes.data(), palettes.size() * sizeof(RenderSkinningPalette), 0);
        _skinningSSBO[fi]->flush();
    }
}

void ShadowStage::execute(const RenderStageContext& ctx)
{
    if (!ctx.cmdBuf || !ctx.frameData) {
        return;
    }

    auto* cmdBuf        = ctx.cmdBuf;
    const uint32_t fi   = ctx.flightIndex;
    const auto& fd      = *ctx.frameData;
    const bool bRenderDirectional = fd.bHasDirectionalLight && static_cast<bool>(_directionalDepthTexture);
    const uint32_t pointLightCount = std::min(_lastPreparedPointLightCount, static_cast<uint32_t>(MAX_POINT_LIGHTS));

    cmdBuf->debugBeginLabel("ShadowStage");

    auto bindViewportScissor = [&]()
    {
        if (_bAutoBindViewportScissor) {
            cmdBuf->setViewport(0.0f, 0.0f, static_cast<float>(_shadowExtent.width), static_cast<float>(_shadowExtent.height), 0.0f, 1.0f);
            cmdBuf->setScissor(0, 0, _shadowExtent.width, _shadowExtent.height);
        }
    };

    auto drawItems = [&](const std::vector<RenderDrawItem>& items, uint32_t layerIndex)
    {
        if (items.empty()) {
            return;
        }

        cmdBuf->bindPipeline(_staticVariant.pipeline.get());
        cmdBuf->bindDescriptorSets(_staticVariant.pipelineLayout.get(), 0, {_frameDS[fi][layerIndex]});
        for (const auto& item : items) {
            if (!item.mesh) {
                continue;
            }
            ModelPushConstant pc{.modelMat = item.worldMatrix, .skinningPaletteIndex = -1};
            cmdBuf->pushConstants(_staticVariant.pipelineLayout.get(), EShaderStage::Vertex, 0, sizeof(ModelPushConstant), &pc);
            item.mesh->drawStatic(cmdBuf);
        }
    };

    auto drawSkinnedItems = [&](const std::vector<RenderDrawItem>& items, uint32_t layerIndex)
    {
        if (items.empty()) {
            return;
        }

        cmdBuf->bindPipeline(_skinnedVariant.pipeline.get());
        cmdBuf->bindDescriptorSets(_skinnedVariant.pipelineLayout.get(), 0, {_frameDS[fi][layerIndex], _skinningDS[fi]});
        for (const auto& item : items) {
            if (!item.mesh) {
                continue;
            }
            ModelPushConstant pc{.modelMat = item.worldMatrix, .skinningPaletteIndex = item.skinningPaletteIndex};
            cmdBuf->pushConstants(_skinnedVariant.pipelineLayout.get(), EShaderStage::Vertex, 0, sizeof(ModelPushConstant), &pc);
            item.mesh->drawSkinned(cmdBuf);
        }
    };

    auto drawBucketSet = [&](const RenderShadingDrawBuckets& buckets, bool bSkinned, uint32_t layerIndex)
    {
        if (bSkinned) {
            drawSkinnedItems(buckets.pbrDrawItems, layerIndex);
            drawSkinnedItems(buckets.phongDrawItems, layerIndex);
            drawSkinnedItems(buckets.unlitDrawItems, layerIndex);
            drawSkinnedItems(buckets.simpleDrawItems, layerIndex);
            drawSkinnedItems(buckets.fallbackDrawItems, layerIndex);
            return;
        }

        drawItems(buckets.pbrDrawItems, layerIndex);
        drawItems(buckets.phongDrawItems, layerIndex);
        drawItems(buckets.unlitDrawItems, layerIndex);
        drawItems(buckets.simpleDrawItems, layerIndex);
        drawItems(buckets.fallbackDrawItems, layerIndex);
    };

    auto renderLayer = [&](Texture* depthTexture, uint32_t layerIndex, const char* label)
    {
        if (!depthTexture) {
            return;
        }

        RenderingInfo::ImageSpec depthSpec{
            .texture       = depthTexture,
            .loadOp        = EAttachmentLoadOp::Clear,
            .storeOp       = EAttachmentStoreOp::Store,
            .initialLayout = EImageLayout::DepthStencilAttachmentOptimal,
            .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
        };
        RenderingInfo renderInfo{
            .label           = label,
            .renderArea      = Rect2D{.pos = {0.0f, 0.0f}, .extent = _shadowExtent.toVec2()},
            .layerCount      = 1,
            .depthClearValue = ClearValue(1.0f, 0),
            .depthAttachment = &depthSpec,
        };

        cmdBuf->beginRendering(renderInfo);
        bindViewportScissor();
        drawBucketSet(fd.drawBuckets.staticMeshes, false, layerIndex);
        drawBucketSet(fd.drawBuckets.skinnedMeshes, true, layerIndex);
        cmdBuf->endRendering(renderInfo);
    };

    if (bRenderDirectional) {
        renderLayer(_directionalDepthTexture.get(), 0, "ShadowStage_Directional");
    }

    for (uint32_t lightIndex = 0; lightIndex < pointLightCount; ++lightIndex) {
        for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
            auto& depthTexture = _pointFaceDepthTextures[lightIndex][faceIndex];
            if (!depthTexture) {
                continue;
            }
            const uint32_t layerIndex = 1 + lightIndex * 6 + faceIndex;
            renderLayer(depthTexture.get(), layerIndex, "ShadowStage_PointFace");
        }
    }

    cmdBuf->debugEndLabel();
}

void ShadowStage::renderGUI()
{
    if (!ImGui::TreeNode("ShadowStage")) {
        return;
    }
    ImGui::Text("RT: %ux%u", _shadowExtent.width, _shadowExtent.height);
    ImGui::Text("Prepared point lights: %u", _lastPreparedPointLightCount);
    ImGui::Text("Point shadow budget: %u", _maxPointLightShadowCount);
    ImGui::Checkbox("Auto Viewport/Scissor", &_bAutoBindViewportScissor);
    ImGui::DragFloat("Receiver Depth Bias", &_bias, 0.0001f, 0.0f, 0.1f, "%.5f");
    ImGui::DragFloat("Receiver Normal Bias", &_normalBias, 0.0001f, 0.0f, 0.1f, "%.5f");
    if (_staticVariant.pipeline) {
        _staticVariant.pipeline->renderGUI();
    }
    if (_skinnedVariant.pipeline) {
        _skinnedVariant.pipeline->renderGUI();
    }
    ImGui::TreePop();
}

} // namespace ya
