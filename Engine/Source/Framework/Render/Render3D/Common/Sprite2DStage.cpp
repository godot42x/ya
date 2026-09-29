#include "Render3D/Common/Sprite2DStage.h"

#include "Core/Log.h"
#include "Resource/Mesh/PrimitiveMeshCache.h"
#include "RHI/Backend/TextureLibrary.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "Render3D/Common/RenderFeatures.h"

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

bool sameBinding(const TextureBinding& lhs, const TextureBinding& rhs)
{
    return lhs.getImageViewHandle() == rhs.getImageViewHandle() &&
           lhs.getSamplerHandle() == rhs.getSamplerHandle();
}

} // namespace

void Sprite2DStage::init(IRender* render)
{
    _render    = render;
    _quadMesh  = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Quad);
    YA_CORE_ASSERT(_quadMesh != nullptr, "Sprite2DStage requires the primitive quad mesh");

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
        {PushConstantRange{.offset = 0, .size = sizeof(PushConstant), .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment}},
        {_frameDSL, _textureDSL});

    // Formats are placeholders until the View's attachments are known; both
    // pipelines are rebuilt by refreshPipelineFormats before the first draw.
    _opaquePipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_opaquePipeline && _opaquePipeline->recreate(makePipelineCreateInfo(false)),
                   "Failed to create the opaque scene sprite pipeline");
    _translucentPipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_translucentPipeline && _translucentPipeline->recreate(makePipelineCreateInfo(true)),
                   "Failed to create the translucent scene sprite pipeline");
}

GraphicsPipelineCreateInfo Sprite2DStage::makePipelineCreateInfo(bool bTranslucent) const
{
    return GraphicsPipelineCreateInfo{
        .pipelineRenderingInfo = {
            .label                  = bTranslucent ? "Scene Sprite (translucent)" : "Scene Sprite (opaque)",
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
            .vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)}},
            .vertexAttributes  = {
                {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
                {.bufferSlot = 0, .location = 1, .format = EVertexAttributeFormat::Float2, .offset = offsetof(ya::Vertex, texCoord0)},
                {.bufferSlot = 0, .location = 2, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, normal)},
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
            .bDepthWriteEnable = !bTranslucent,
            .depthCompareOp    = ECompareOp::LessOrEqual,
        },
        .colorBlendState = {.attachments = {{
            .index               = 0,
            .bBlendEnable        = bTranslucent,
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
    _opaquePipeline.reset();
    _translucentPipeline.reset();
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

    refresh(_opaquePipeline);
    refresh(_translucentPipeline);
}

Sprite2DStage::FrameData Sprite2DStage::buildFrameData(const RenderStageContext& ctx)
{
    FrameData frameData{};
    if (ctx.frameData) {
        frameData.viewProj = ctx.frameData->viewProjection;
    }
    return frameData;
}

std::vector<TextureBinding> Sprite2DStage::buildTextureTable(const RenderFrameData& frameData)
{
    // Slot 0 is the white fallback for table entries no sprite uses: entries past
    // the last real texture are never sampled, they only have to hold a valid
    // image view. Real textures therefore start at slot 1, and this is the only
    // place that ordering is decided -- writing the descriptor set and resolving
    // a sprite's slot both go through it.
    std::vector<TextureBinding> table;
    table.reserve(kTextureTableSize);
    table.push_back(TextureBinding{
        .texture = TextureLibrary::get().getWhiteTexture(),
        .sampler = TextureLibrary::get().getDefaultSampler(),
    });

    for (const WorldSpriteCandidate& sprite : frameData.worldSprites) {
        if (table.size() >= kTextureTableSize) {
            break;
        }
        const bool bKnown = std::ranges::any_of(table, [&sprite](const TextureBinding& existing)
                                                { return sameBinding(existing, sprite.texture); });
        if (!bKnown) {
            table.push_back(sprite.texture);
        }
    }

    return table;
}

void Sprite2DStage::updateTextures(const RenderFrameData& frameData, Sprite2DPassBindings& bindings)
{
    if (!bindings.textures.set) {
        return;
    }

    const std::vector<TextureBinding> table = buildTextureTable(frameData);

    std::vector<DescriptorImageInfo> imageInfos;
    imageInfos.reserve(kTextureTableSize);
    for (uint32_t index = 0; index < kTextureTableSize; ++index) {
        const TextureBinding& binding = index < table.size() ? table[index] : table.front();
        imageInfos.push_back(DescriptorImageInfo{
            .imageView   = binding.getImageViewHandle(),
            .sampler     = binding.getSamplerHandle(),
            .imageLayout = EImageLayout::ShaderReadOnlyOptimal,
        });
    }

    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::genImageWrite(bindings.textures.set, 0, 0, EPipelineDescriptorType::CombinedImageSampler, std::move(imageInfos)),
    });
}

void Sprite2DStage::drawSprites(const RenderStageContext& ctx, const Sprite2DPassBindings& bindings)
{
    const RenderFrameData& frameData = *ctx.frameData;
    if (frameData.worldSprites.empty()) {
        return;
    }
    if (!_opaquePipeline || !_translucentPipeline || !_pipelineLayout || !_quadMesh) {
        return;
    }
    if (!bindings.frame.set || !bindings.textures.set) {
        return;
    }

    const uint32_t vpW = ctx.viewExtent.width;
    const uint32_t vpH = ctx.viewExtent.height;
    if (vpW == 0 || vpH == 0) {
        return;
    }

    auto* cmdBuf = ctx.cmdBuf;
    cmdBuf->debugBeginLabel("SceneSprites");

    const float viewportY = _bReverseViewportY ? static_cast<float>(vpH) : 0.0f;
    const float viewHeight = _bReverseViewportY ? -static_cast<float>(vpH) : static_cast<float>(vpH);
    cmdBuf->setViewport(0.0f, viewportY, static_cast<float>(vpW), viewHeight);
    cmdBuf->setScissor(0, 0, vpW, vpH);
    cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, {bindings.frame.set, bindings.textures.set});

    // The View's table, resolved by the one implementation that also wrote the
    // descriptor set: a sprite whose texture was dropped by the table cap is
    // skipped rather than sampled from something else.
    const std::vector<TextureBinding> table = buildTextureTable(frameData);
    const auto slotFor = [&table](const TextureBinding& texture) -> uint32_t
    {
        for (uint32_t index = 0; index < table.size(); ++index) {
            if (sameBinding(table[index], texture)) {
                return index;
            }
        }
        return kNoTextureSlot;
    };

    bool bBoundTranslucent = false;
    bool bPipelineBound    = false;
    for (const WorldSpriteCandidate& sprite : frameData.worldSprites) {
        const uint32_t textureSlot = slotFor(sprite.texture);
        if (textureSlot == kNoTextureSlot) {
            continue;
        }

        if (!bPipelineBound || bBoundTranslucent != sprite.bTranslucent) {
            cmdBuf->bindPipeline(sprite.bTranslucent ? _translucentPipeline.get() : _opaquePipeline.get());
            bBoundTranslucent = sprite.bTranslucent;
            bPipelineBound    = true;
        }

        PushConstant pc{};
        pc.worldCenter  = sprite.worldCenter;
        pc.textureIndex = textureSlot;
        pc.axisX        = sprite.axisX;
        pc.axisY        = sprite.axisY;
        pc.uvRect       = sprite.uvRect;
        pc.tint         = sprite.tint;
        cmdBuf->pushConstants(_pipelineLayout.get(), EShaderStage::Vertex | EShaderStage::Fragment, 0, sizeof(pc), &pc);
        _quadMesh->drawStatic(cmdBuf);
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
