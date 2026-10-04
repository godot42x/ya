#include "Render3D/Common/Sprite2DStage.h"

#include "Core/Log.h"
#include "Render2D/TextureTableBatch.h"
#include "Render3D/Common/RenderFeatures.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Resource/Mesh/PrimitiveMeshCache.h"
#include "RHI/Backend/TextureLibrary.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "RHI/Render.h"

#include <algorithm>
#include <format>
#include <utility>
#include <vector>

namespace ya
{

namespace
{

constexpr EFormat::T kPlaceholderColorFormat = EFormat::R16G16B16A16_SFLOAT;
constexpr EFormat::T kPlaceholderDepthFormat = EFormat::D32_SFLOAT;
constexpr uint32_t   kInstanceUploadAlignment = 256u;

TextureBinding whiteBinding()
{
    return TextureBinding{
        .texture = TextureLibrary::get().getWhiteTexture(),
        .sampler = TextureLibrary::get().getDefaultSampler(),
    };
}

} // namespace

void Sprite2DStage::init(IRender* render)
{
    _render   = render;
    _quadMesh = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Quad);
    YA_CORE_ASSERT(_quadMesh != nullptr, "Sprite2DStage requires the primitive quad mesh");

    auto* factory = _render->getResourceFactory();
    YA_CORE_ASSERT(factory != nullptr, "Sprite2DStage requires a resource factory for the instance buffer");
    const uint32_t flights = std::max(1u, _render->framesInFlight());
    _instanceUploads = std::make_unique<FrameUploadArena>(
        *factory,
        flights,
        64u * 1024u,
        EBufferUsage::VertexBuffer,
        "SceneSprite.Instances");
    _textureSetFlights.assign(flights, {});

    _frameDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "SceneSprite_Frame_DSL",
            .set      = 0,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex}},
        });

    _textureDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "SceneSprite_Texture_DSL",
            .set      = 1,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = kTextureTableSize, .stageFlags = EShaderStage::Fragment}},
        });

    _pipelineLayout = IPipelineLayout::create(
        _render,
        "SceneSprite_PPL",
        {},
        {_frameDSL, _textureDSL});

    // Formats are placeholders until the View's attachments are known; the
    // pipeline is rebuilt by refreshPipelineFormats before the first draw.
    _pipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_pipeline && _pipeline->recreate(makePipelineCreateInfo()),
                   "Failed to create the scene sprite pipeline");
}

GraphicsPipelineCreateInfo Sprite2DStage::makePipelineCreateInfo() const
{
    return GraphicsPipelineCreateInfo{
        .pipelineRenderingInfo = {
            .label                  = "Scene Sprite",
            .colorAttachmentFormats = {kPlaceholderColorFormat},
            .depthAttachmentFormat  = kPlaceholderDepthFormat,
        },
        .pipelineLayout = _pipelineLayout.get(),
        .shaderDesc     = ShaderDesc{
            .sourceMode = ShaderDesc::ESourceMode::StageFiles,
            .stageFiles = {
                ShaderDesc::StageFile{.stage = EShaderStage::Vertex, .file = "Sprite2DWorld.slang", .entryName = "vertWorldMain"},
                ShaderDesc::StageFile{.stage = EShaderStage::Fragment, .file = "Sprite2DWorld.slang", .entryName = "fragWorldMain"},
            },
            .vertexBufferDescs = {
                VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex), .inputRate = EVertexInputRate::Vertex},
                VertexBufferDescription{.slot = 1, .pitch = sizeof(Instance), .inputRate = EVertexInputRate::Instance},
            },
            .vertexAttributes  = {
                {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
                {.bufferSlot = 0, .location = 1, .format = EVertexAttributeFormat::Float2, .offset = offsetof(ya::Vertex, texCoord0)},
                {.bufferSlot = 0, .location = 2, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, normal)},
                {.bufferSlot = 1, .location = 3, .format = EVertexAttributeFormat::Float3, .offset = offsetof(Instance, worldCenter)},
                {.bufferSlot = 1, .location = 4, .format = EVertexAttributeFormat::Uint32, .offset = offsetof(Instance, textureIndex)},
                {.bufferSlot = 1, .location = 5, .format = EVertexAttributeFormat::Float3, .offset = offsetof(Instance, axisX)},
                {.bufferSlot = 1, .location = 6, .format = EVertexAttributeFormat::Float3, .offset = offsetof(Instance, axisY)},
                {.bufferSlot = 1, .location = 7, .format = EVertexAttributeFormat::Float4, .offset = offsetof(Instance, uvRect)},
                {.bufferSlot = 1, .location = 8, .format = EVertexAttributeFormat::Float4, .offset = offsetof(Instance, tint)},
            },
            .defines = {
                std::format("TEXTURE_SET_SIZE {}", kTextureTableSize),
            },
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType      = EPrimitiveType::TriangleList,
        // The shared quad carries both windings. Culling back faces keeps the
        // sprite visible from either side while rasterizing it once; without
        // culling both windings draw and translucent sprites blend twice.
        .rasterizationState = {.polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::Back, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {
            .bDepthTestEnable  = true,
            .bDepthWriteEnable = false,
            .depthCompareOp    = ECompareOp::LessOrEqual,
        },
        .colorBlendState = {.attachments = {{
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
}

void Sprite2DStage::destroy()
{
    _quadMesh = nullptr;
    _textureSetFlights.clear();
    _instanceUploads.reset();
    _pipeline.reset();
    _pipelineLayout.reset();
    _textureDSL.reset();
    _frameDSL.reset();
    _render = nullptr;
}

void Sprite2DStage::execute([[maybe_unused]] const RenderStageContext& ctx)
{
    YA_CORE_WARN("Sprite2DStage::execute(ctx) is a conformance stub; graph passes must use executeSprites");
}

void Sprite2DStage::refreshPipelineFormats(EFormat::T colorFormat, EFormat::T depthFormat)
{
    const auto refresh = [colorFormat, depthFormat](stdptr<IGraphicsPipeline>& pipeline)
    {
        if (!pipeline) {
            return;
        }
        auto ci                                         = pipeline->getDesc();
        ci.pipelineRenderingInfo.colorAttachmentFormats = {colorFormat};
        ci.pipelineRenderingInfo.depthAttachmentFormat  = depthFormat;
        pipeline->updateDesc(std::move(ci));
    };

    refresh(_pipeline);
}

Sprite2DStage::FrameData Sprite2DStage::buildFrameData(const RenderStageContext& ctx)
{
    FrameData frameData{};
    if (ctx.frameData) {
        frameData.viewProj = ctx.frameData->viewProjection;
    }
    return frameData;
}

DescriptorSetHandle Sprite2DStage::acquireTextureSet(RenderSubmission& submission)
{
    const uint32_t flight = submission.flightIndex();
    if (flight >= _textureSetFlights.size() || !_textureDSL) {
        return {};
    }

    TextureSetFlight& lane = _textureSetFlights[flight];
    if (!lane.bHasToken || lane.frameToken != submission.frameToken()) {
        lane.cursor     = 0;
        lane.frameToken = submission.frameToken();
        lane.bHasToken  = true;
    }
    if (lane.cursor >= lane.sets.size()) {
        DescriptorSetHandle created = submission.allocateDescriptorSet(
            _textureDSL,
            kTextureTableSize,
            EPipelineDescriptorType::CombinedImageSampler);
        if (!created) {
            return {};
        }
        lane.sets.push_back(created);
    }
    return lane.sets[lane.cursor++];
}

void Sprite2DStage::updateTextures(RenderSubmission& submission, const RenderFrameData& frameData, Sprite2DPassBindings& bindings)
{
    bindings.instances = {};
    bindings.batches.clear();
    if (frameData.worldSprites.empty() || !_render || !_instanceUploads) {
        return;
    }

    const TextureBinding white = whiteBinding();
    // The candidate's TextureBinding is copied into the batch. That copy, and
    // the candidate itself, keep the texture, its view and the sampler alive
    // until the submission that holds these bindings has retired.
    const auto plan = planInstancedDraws<decltype(frameData.worldSprites), Instance, TextureBinding>(
        frameData.worldSprites,
        white,
        TextureTableKey::fromBinding(white),
        [](const WorldSpriteCandidate& sprite) { return TextureTableKey::fromBinding(sprite.texture); },
        [](const WorldSpriteCandidate& sprite) { return sprite.texture; },
        [](const WorldSpriteCandidate& sprite, uint32_t slot, Instance& instance) {
            instance.worldCenter  = sprite.worldCenter;
            instance.textureIndex = slot;
            instance.axisX        = sprite.axisX;
            instance.axisY        = sprite.axisY;
            instance.uvRect       = sprite.uvRect;
            instance.tint         = sprite.tint;
        },
        kTextureTableSize);

    if (plan.instances.empty()) {
        return;
    }

    const uint32_t flight = submission.flightIndex();
    if (!_instanceUploads->beginFlight(flight, submission.frameToken())) {
        YA_CORE_ERROR("Scene sprite instance upload missed its flight");
        return;
    }
    const uint32_t bytes = static_cast<uint32_t>(plan.instances.size() * sizeof(Instance));
    auto slice = _instanceUploads->allocate(flight, bytes, kInstanceUploadAlignment);
    if (!slice || !slice->write(plan.instances.data(), bytes)) {
        YA_CORE_ERROR("Scene sprite instance upload failed for {} sprites", plan.instances.size());
        return;
    }
    bindings.instances = *slice;

    auto* helper = _render->getDescriptorHelper();
    bindings.batches.reserve(plan.batches.size());
    for (const auto& planned : plan.batches) {
        DescriptorSetHandle set = acquireTextureSet(submission);
        if (!set || !helper) {
            YA_CORE_ERROR("Scene sprite texture table could not be allocated");
            bindings.batches.clear();
            return;
        }

        std::vector<DescriptorImageInfo> imageInfos;
        imageInfos.reserve(kTextureTableSize);
        for (uint32_t index = 0; index < kTextureTableSize; ++index) {
            const TextureBinding& binding = index < planned.slots.size() ? planned.slots[index] : planned.slots.front();
            imageInfos.push_back(DescriptorImageInfo{
                .imageView   = binding.getImageViewHandle(),
                .sampler     = binding.getSamplerHandle(),
                .imageLayout = EImageLayout::ShaderReadOnlyOptimal,
            });
        }
        helper->updateDescriptorSets({
            IDescriptorSetHelper::genImageWrite(set, 0, 0, EPipelineDescriptorType::CombinedImageSampler, std::move(imageInfos)),
        });

        bindings.batches.push_back(SpriteTextureBatch{
            .firstInstance = planned.first,
            .instanceCount = planned.count,
            .set           = set,
            .textures      = planned.slots,
        });
    }
}

void Sprite2DStage::drawSprites(const RenderStageContext& ctx, const Sprite2DPassBindings& bindings)
{
    if (bindings.batches.empty() || !bindings.instances) {
        return;
    }
    if (!_pipeline || !_pipelineLayout || !_quadMesh || !_quadMesh->getVertexBuffer() || !_quadMesh->getIndexBuffer()) {
        return;
    }
    if (!bindings.frame.set) {
        return;
    }

    const uint32_t vpW = ctx.viewExtent.width;
    const uint32_t vpH = ctx.viewExtent.height;
    if (vpW == 0 || vpH == 0) {
        return;
    }

    auto* cmdBuf = ctx.cmdBuf;
    cmdBuf->debugBeginLabel("SceneSprites");

    const float viewportY  = _bReverseViewportY ? static_cast<float>(vpH) : 0.0f;
    const float viewHeight = _bReverseViewportY ? -static_cast<float>(vpH) : static_cast<float>(vpH);
    cmdBuf->setViewport(0.0f, viewportY, static_cast<float>(vpW), viewHeight);
    cmdBuf->setScissor(0, 0, vpW, vpH);
    cmdBuf->bindPipeline(_pipeline.get());
    cmdBuf->bindVertexBuffer(0, _quadMesh->getVertexBuffer(), _quadMesh->getVertexBufferOffset());
    cmdBuf->bindIndexBuffer(_quadMesh->getIndexBufferMut(), _quadMesh->getIndexBufferOffset(), false);

    for (const SpriteTextureBatch& batch : bindings.batches) {
        if (batch.instanceCount == 0 || !batch.set) {
            continue;
        }
        cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, {bindings.frame.set, batch.set});
        const uint64_t instanceOffset = bindings.instances.offset + static_cast<uint64_t>(batch.firstInstance) * sizeof(Instance);
        cmdBuf->bindVertexBuffer(1, bindings.instances.buffer.get(), instanceOffset);
        cmdBuf->drawIndexed(_quadMesh->getIndexCount(), batch.instanceCount, 0, 0, 0);
    }

    cmdBuf->debugEndLabel();
}

void Sprite2DStage::executeSprites(const RenderStageContext& ctx, const Sprite2DPassBindings& bindings)
{
    if (!ctx.cmdBuf || !ctx.frameData) {
        return;
    }

    drawSprites(ctx, bindings);
}

} // namespace ya
