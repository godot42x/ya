#pragma once

#include "Render3D/Common/ViewPassResources.h"
#include "Render3D/Stage/IRenderStage.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Pipeline.h"

#include "Sprite2DWorld.slang.h"

#include <cstdint>

namespace ya
{

struct Mesh;

/// Scene sprite stage: draws the View's authored sprites (`Sprite2DComponent`
/// candidates) as world-space quads, inside the scene graph's own color and
/// depth attachments.
///
/// A sprite is transform-oriented: its quad lies on the entity's local XY
/// plane, so rotating the entity turns the quad. A camera-facing quad is a
/// billboard and stays in ViewOverlayStage.
///
/// The draw policy comes from the component and is not re-decided here: an
/// opaque sprite (`tint.a` >= 1) depth-tests and depth-writes, so scene
/// geometry occludes it and it occludes whatever is drawn after it; a
/// translucent sprite depth-tests without writing and relies on the View's own
/// order. Running inside the scene graph (after opaque geometry, before bloom)
/// is what makes both true -- it is not an overlay on the finished image.
///
/// The stage holds no per-View state: candidates arrive through
/// `RenderStageContext::frameData`, and the View's bindings are allocated by the
/// pipeline recording it.
struct YA_RENDER_3D_API Sprite2DStage : public IRenderStage
{
    using FrameData    = slang_types::Sprite2DWorld::FrameData;
    using PushConstant = slang_types::Sprite2DWorld::SpritePushConstant;

    /// Textures one View's sprite table can hold. A sprite whose texture does
    /// not fit is not drawn -- sampling a substitute image would be a draw the
    /// component never asked for.
    static constexpr uint32_t kTextureTableSize = 16;
    static constexpr uint32_t kNoTextureSlot    = ~uint32_t{0};

    Sprite2DStage() : IRenderStage("SceneSprites") {}

    void init(IRender* render) override;
    void destroy() override;
    /// IRenderStage conformance; graph passes must use executeSprites.
    void execute(const RenderStageContext& ctx) override;

    /// Record this View's sprites. They come from the View's bucket on
    /// `ctx.frameData`, so recording never walks the Scene's ECS.
    void executeSprites(const RenderStageContext& ctx, const Sprite2DPassBindings& bindings);

    /// Fill one View's sprite texture table from its candidate bucket. Called
    /// while the View's resources are allocated -- before the graph is built,
    /// because a descriptor set must not be written while recording.
    void updateTextures(const RenderFrameData& frameData, Sprite2DPassBindings& bindings);

    /// Rebuild the sprite pipelines against the View's attachment formats.
    void refreshPipelineFormats(EFormat::T colorFormat, EFormat::T depthFormat);

    /// Same convention as the scene's other viewport passes (GBuffer, skybox):
    /// sprites must land in the orientation the depth buffer was rendered in.
    void setReverseViewportY(bool bReverse) { _bReverseViewportY = bReverse; }

    [[nodiscard]] static FrameData buildFrameData(const RenderStageContext& ctx);
    [[nodiscard]] stdptr<IDescriptorSetLayout> getFrameDSL() const { return _frameDSL; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getTextureDSL() const { return _textureDSL; }

  private:
    [[nodiscard]] GraphicsPipelineCreateInfo makePipelineCreateInfo(bool bTranslucent) const;
    /// The View's sprite texture table, in slot order. Built by the same
    /// implementation that writes the descriptor set, so a sprite's slot and the
    /// image written to that slot cannot disagree.
    [[nodiscard]] static std::vector<TextureBinding> buildTextureTable(const RenderFrameData& frameData);
    void drawSprites(const RenderStageContext& ctx, const Sprite2DPassBindings& bindings);

    IRender* _render = nullptr;

    stdptr<IDescriptorSetLayout> _frameDSL;
    stdptr<IDescriptorSetLayout> _textureDSL;
    stdptr<IPipelineLayout>      _pipelineLayout;
    /// Opaque writes depth; translucent only tests it. Two pipelines because
    /// depth-write is pipeline state, not a per-draw argument.
    stdptr<IGraphicsPipeline>    _opaquePipeline;
    stdptr<IGraphicsPipeline>    _translucentPipeline;
    Mesh*                        _quadMesh = nullptr;
    bool                         _bReverseViewportY = true;
};

} // namespace ya
