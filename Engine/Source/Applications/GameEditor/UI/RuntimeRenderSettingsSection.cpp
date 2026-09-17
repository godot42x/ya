#include "GameEditor/UI/RuntimeRenderSettingsSection.h"

#include "Core/Config/ConfigManager.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/DragFloat.h"
#include "GUI/Widgets/Controls/Expander.h"
#include "GUI/Widgets/Controls/RadioButton.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/UIElement.h"
#include "GameEditor/UI/EditorTheme.h"
#include "GameRuntime/App.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/Core/Swapchain.h"
#include "Render3D/Common/PostProcessingStateConfig.h"
#include "Render3D/Common/Shadow/Common/ShadowSettingsConfig.h"
#include "Render3D/Common/ShadowSettings.h"
#include "Render3D/Deferred/DeferredRenderPipeline.h"
#include "Render3D/Forward/ForwardRenderPipeline.h"
#include "Render3D/RenderDeviceState.h"
#include "RHI/RenderDefines.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

namespace ya
{
namespace
{

[[nodiscard]] FBoxSlotArgs labelSlot()
{
    return {.preferredSize = {editor_density::kLabelColumn, editor_density::kRowHeight}};
}

[[nodiscard]] FBoxSlotArgs controlSlot()
{
    return {
        .sizeRule      = EUIBoxSlotSizeRule::Fill,
        .preferredSize = {0.0f, editor_density::kRowHeight},
    };
}

[[nodiscard]] UIElementRef labeledRow(const char* rowKey, const char* label, const UIElementRef& control)
{
    return ui::row(rowKey)
        .setSpacing(editor_density::kControlSpacing)
        .child(ui::text(std::string(rowKey) + "Label")
                   .setText(label)
                   .setStyleKey("text.muted")
                   .setVAlign(EWidgetAlignV::Center),
               labelSlot())
        .child(control, controlSlot())
        .release();
}

[[nodiscard]] std::shared_ptr<UICheckBox> makeCheck(const char* key, const char* label)
{
    return ui::checkBox(key)
        .child(ui::text(std::string(key) + "Label").setText(label).setStyleKey("text.muted"))
        .share();
}

[[nodiscard]] std::shared_ptr<UIDragFloat> makeDrag(const char* key, float min, float max, float speed, int decimals)
{
    auto drag = std::make_shared<UIDragFloat>(key);
    drag->setStyleKey(editorStyle(StyleKey::DragFloat));
    drag->_min      = min;
    drag->_max      = max;
    drag->_speed    = speed;
    drag->_decimals = decimals;
    return drag;
}

[[nodiscard]] std::shared_ptr<UIComboBox> makeCombo(const char* key, std::vector<std::string> items)
{
    auto combo = std::make_shared<UIComboBox>(key);
    combo->setStyleKey(editorStyle(StyleKey::ComboBox));
    combo->_items = std::move(items);
    return combo;
}

[[nodiscard]] std::shared_ptr<UIRadioButton> makeRadio(const char* key, const char* label)
{
    auto radio = std::make_shared<UIRadioButton>(key);
    radio->_label = label;
    return radio;
}

const char* presentLabel(int value)
{
    switch (value) {
    case EPresentMode::Immediate: {
        return "Immediate";
    }
    case EPresentMode::Mailbox: {
        return "Mailbox";
    }
    case EPresentMode::FIFO: {
        return "FIFO";
    }
    case EPresentMode::FIFO_Relaxed: {
        return "FIFO Relaxed";
    }
    default: {
        return "Unknown";
    }
    }
}

void saveDeferredExtras(const DeferredRenderPipeline::SettingsSnapshot& settings)
{
    ConfigManager::Editor("runtime")
        .set("render.deferred.reverseViewportY", settings.bReverseViewportY)
        .set("render.deferred.ssaoEnabled", settings.bSSAOEnabled)
        .set("render.deferred.ssao.radius", settings.ssaoRadius)
        .set("render.deferred.ssao.bias", settings.ssaoBias)
        .set("render.deferred.ssao.power", settings.ssaoPower)
        .set("render.deferred.ssao.intensity", settings.ssaoIntensity)
        .set("render.deferred.light.enablePBRDiffuseIBL", settings.bPBRDiffuseIBL)
        .set("render.deferred.light.enablePBRSpecularIBL", settings.bPBRSpecularIBL);
}

void persistDeferred(const DeferredRenderPipeline::SettingsSnapshot& settings)
{
    postprocess_settings::saveRuntimeSettings(settings.postProcessing);
    shadow_settings::saveRuntimeSettings(settings.shadow);
    saveDeferredExtras(settings);
}

template <typename Fn>
void mutateDeferred(App* app, Fn&& fn)
{
    if (!app) {
        return;
    }
    auto* runtime = app->getRenderServices().getDeviceState();
    if (!runtime) {
        return;
    }
    auto* deferred = dynamic_cast<DeferredRenderPipeline*>(runtime->getActivePipeline());
    if (!deferred) {
        return;
    }
    auto snapshot = deferred->resolveSettingsSnapshot();
    fn(snapshot);
    deferred->requestSettings(snapshot);
    persistDeferred(snapshot);
}

template <typename Fn>
void mutatePostProcess(App* app, Fn&& fn)
{
    if (!app) {
        return;
    }
    auto* runtime = app->getRenderServices().getDeviceState();
    if (!runtime) {
        return;
    }
    if (auto* deferred = dynamic_cast<DeferredRenderPipeline*>(runtime->getActivePipeline())) {
        auto snapshot = deferred->resolveSettingsSnapshot();
        fn(snapshot.postProcessing);
        deferred->requestSettings(snapshot);
        persistDeferred(snapshot);
        return;
    }
    if (auto* forward = dynamic_cast<ForwardRenderPipeline*>(runtime->getActivePipeline())) {
        auto post = forward->resolvePostProcessSettings();
        fn(post);
        forward->requestPostProcessSettings(post);
        postprocess_settings::saveRuntimeSettings(post);
    }
}

template <typename Fn>
void mutateShadow(App* app, Fn&& fn)
{
    if (!app) {
        return;
    }
    auto* runtime = app->getRenderServices().getDeviceState();
    if (!runtime) {
        return;
    }
    if (auto* deferred = dynamic_cast<DeferredRenderPipeline*>(runtime->getActivePipeline())) {
        auto snapshot = deferred->resolveSettingsSnapshot();
        fn(snapshot.shadow);
        deferred->requestSettings(snapshot);
        persistDeferred(snapshot);
        return;
    }
    if (auto* forward = dynamic_cast<ForwardRenderPipeline*>(runtime->getActivePipeline())) {
        auto shadow = forward->getCurrentShadowSettings();
        fn(shadow);
        forward->requestShadowSettings(shadow);
        shadow_settings::saveRuntimeSettings(shadow);
    }
}

[[nodiscard]] int asInt(float value, int min, int max)
{
    return std::clamp(static_cast<int>(std::lround(value)), min, max);
}

} // namespace

RuntimeRenderSettingsSection::RuntimeRenderSettingsSection(std::string name,
                                                           App* app,
                                                           IRenderSurfaceContext* presentSurface)
    : UICompoundWidget(std::move(name), "panel")
    , _app(app)
    , _presentSurface(presentSurface)
{
}

void RuntimeRenderSettingsSection::construct()
{
    _pipeline = makeCombo("RenderPipeline", {"Forward", "Deferred"});
    _pipelinePending = ui::text("RenderPipelinePending").setStyleKey("text.muted").share();
    _viewportScale = makeDrag("RenderViewportScale", 1.0f, 10.0f, 0.1f, 1);
    _vsync = makeCheck("RenderVsync", "VSync");
    _presentState = ui::text("RenderPresentState").setStyleKey("text.muted").share();
    _presentMode = makeCombo("RenderPresentMode", {"Immediate", "Mailbox", "FIFO", "FIFO Relaxed"});
    _reload = ui::button("RenderReloadPipeline")
                  .child(ui::text("RenderReloadPipelineLabel").setText("Reload Active Pipeline"))
                  .share();

    _inversion = makeCheck("RenderInversion", "Inversion");
    _grayscale = makeCombo("RenderGrayscale", {"None", "Average", "Weighted"});
    _kernel = makeCombo("RenderKernel", {"None", "Sharpen", "Blur", "Edge Detection"});
    _kernelOffset = makeDrag("RenderKernelOffset", 0.0001f, 0.02f, 0.0001f, 5);
    _toneMapping = makeCheck("RenderToneMapping", "Enable Tone Mapping");
    _toneCurve = makeCombo("RenderToneCurve", {"ACES", "Uncharted2"});
    _exposure = makeDrag("RenderExposure", 0.0f, 8.0f, 0.01f, 2);
    _gammaCorrection = makeCheck("RenderGammaCorrection", "Gamma Correction");
    _gamma = makeDrag("RenderGamma", 0.1f, 4.0f, 0.01f, 2);
    _grain = makeCheck("RenderGrain", "Random Grain");
    _grainStrength = makeDrag("RenderGrainStrength", 0.0f, 0.25f, 0.001f, 3);
    _bloom = makeCheck("RenderBloom", "Enable Bloom");
    _bloomThreshold = makeDrag("RenderBloomThreshold", 0.0f, 16.0f, 0.01f, 2);
    _bloomSoftKnee = makeDrag("RenderBloomSoftKnee", 0.0f, 2.0f, 0.01f, 2);
    _bloomExtract = makeDrag("RenderBloomExtract", 0.0f, 8.0f, 0.05f, 2);
    _bloomPasses = makeDrag("RenderBloomPasses", 1.0f, 12.0f, 1.0f, 0);
    _bloomStrength = makeDrag("RenderBloomStrength", 0.0f, 4.0f, 0.05f, 2);

    _shadowEnable = makeCheck("RenderShadowEnable", "Enable Shadow Mapping");
    _shadowQuality = makeCombo("RenderShadowQuality", {"Low", "Medium", "High", "Ultra"});
    _shadowResolution = makeDrag("RenderShadowResolution", 128.0f, 8192.0f, 16.0f, 0);
    _shadowBias = makeDrag("RenderShadowBias", 0.0f, 0.1f, 0.0001f, 5);
    _shadowNormalBias = makeDrag("RenderShadowNormalBias", 0.0f, 0.1f, 0.0001f, 5);
    _shadowFilter = makeCombo("RenderShadowFilter", {"Hard", "PCF Low", "PCF High"});
    _directionalUnavailable = ui::text("RenderDirectionalUnavailable")
                                  .setText("Directional controls unavailable in this pipeline")
                                  .setStyleKey("text.muted")
                                  .setWrap(true)
                                  .share();
    _directionalEnable = makeCheck("RenderDirectionalEnable", "Enabled");
    _shadowSingleMap = makeRadio("RenderShadowSingleMap", "Single Map");
    _shadowCSM = makeRadio("RenderShadowCSM", "CSM");
    _shadowDistance = makeDrag("RenderShadowDistance", 1.0f, 500.0f, 0.5f, 1);
    _shadowStableFit = makeCheck("RenderShadowStableFit", "Stable Fit");
    _shadowCascades = makeDrag("RenderShadowCascades", 2.0f, static_cast<float>(MAX_DIRECTIONAL_CASCADES), 1.0f, 0);
    for (uint32_t i = 0; i < _shadowSplits.size(); ++i) {
        _shadowSplits[i] = makeDrag(std::format("RenderShadowSplit{}", i + 1).c_str(), 0.001f, 0.999f, 0.001f, 3);
    }
    _shadowZRange = makeDrag("RenderShadowZRange", 1.0f, 100.0f, 0.1f, 1);
    _pointEnable = makeCheck("RenderPointEnable", "Enabled");
    _pointIndirect = makeCheck("RenderPointIndirect", "Indirect Draw");
    _pointCull = makeCheck("RenderPointCull", "Indirect Cull");
    _pointMaxShadows = makeDrag("RenderPointMaxShadows", 0.0f, static_cast<float>(MAX_POINT_LIGHTS), 1.0f, 0);

    _deferredUnavailable = ui::text("RenderDeferredUnavailable")
                               .setText("Deferred-only settings are unavailable while the forward pipeline is active.")
                               .setStyleKey("text.muted")
                               .setWrap(true)
                               .share();
    _reverseY = makeCheck("RenderReverseY", "GBuffer Reverse Viewport Y");
    _iblDiffuse = makeCheck("RenderIBLDiffuse", "Enable PBR Diffuse IBL");
    _iblSpecular = makeCheck("RenderIBLSpecular", "Enable PBR Specular IBL");
    _ssaoEnable = makeCheck("RenderSSAOEnable", "Enable SSAO");
    _ssaoRadius = makeDrag("RenderSSAORadius", 0.05f, 5.0f, 0.01f, 3);
    _ssaoBias = makeDrag("RenderSSAOBias", 0.0f, 0.2f, 0.001f, 4);
    _ssaoPower = makeDrag("RenderSSAOPower", 0.1f, 4.0f, 0.01f, 3);
    _ssaoIntensity = makeDrag("RenderSSAOIntensity", 0.0f, 8.0f, 0.05f, 3);

    auto splitRows = ui::column("RenderShadowSplits").setSpacing(editor_density::kRowSpacing);
    for (uint32_t i = 0; i < _shadowSplits.size(); ++i) {
        splitRows.child(labeledRow(std::format("RenderShadowSplitRow{}", i + 1).c_str(),
                                   std::format("Split {}", i + 1).c_str(),
                                   _shadowSplits[i]));
    }

    auto directionalDetails = ui::column("RenderDirectionalDetails")
                                  .setSpacing(editor_density::kRowSpacing)
                                  .child(ui::row("RenderShadowMapMode")
                                             .setSpacing(editor_density::kControlSpacing)
                                             .child(_shadowSingleMap)
                                             .child(_shadowCSM)
                                             .release())
                                  .child(labeledRow("RenderShadowDistanceRow", "Distance", _shadowDistance))
                                  .child(_shadowStableFit)
                                  .child(labeledRow("RenderShadowCascadesRow", "Cascades", _shadowCascades))
                                  .child(splitRows.release())
                                  .child(labeledRow("RenderShadowZRangeRow", "Z Range", _shadowZRange))
                                  .share();
    _directionalDetails = directionalDetails;

    auto directionalControls = ui::column("RenderDirectionalControls")
                                   .setSpacing(editor_density::kRowSpacing)
                                   .child(_directionalEnable)
                                   .child(directionalDetails)
                                   .share();
    _directionalBody = directionalControls;

    auto shadowBody = ui::column("RenderShadowBody")
                          .setSpacing(editor_density::kRowSpacing)
                          .child(labeledRow("RenderShadowQualityRow", "Quality Preset", _shadowQuality))
                          .child(labeledRow("RenderShadowResolutionRow", "Shadow Resolution", _shadowResolution))
                          .child(labeledRow("RenderShadowBiasRow", "Depth Bias", _shadowBias))
                          .child(labeledRow("RenderShadowNormalBiasRow", "Normal Bias", _shadowNormalBias))
                          .child(labeledRow("RenderShadowFilterRow", "Shadow Filter", _shadowFilter))
                          .child(ui::text("RenderDirectionalHeader").setText("Directional").setStyleKey("text.small"))
                          .child(_directionalUnavailable)
                          .child(directionalControls)
                          .child(ui::text("RenderPointHeader").setText("Point").setStyleKey("text.small"))
                          .child(_pointEnable)
                          .child(_pointIndirect)
                          .child(_pointCull)
                          .child(labeledRow("RenderPointMaxRow", "Max Shadows", _pointMaxShadows))
                          .share();
    _shadowBody = shadowBody;

    auto ssaoBody = ui::column("RenderSSAOBody")
                        .setSpacing(editor_density::kRowSpacing)
                        .child(labeledRow("RenderSSAORadiusRow", "Radius", _ssaoRadius))
                        .child(labeledRow("RenderSSAOBiasRow", "Bias", _ssaoBias))
                        .child(labeledRow("RenderSSAOPowerRow", "Power", _ssaoPower))
                        .child(labeledRow("RenderSSAOIntensityRow", "Intensity", _ssaoIntensity))
                        .share();
    _ssaoBody = ssaoBody;

    auto deferredBody = ui::column("RenderDeferredBody")
                            .setSpacing(editor_density::kRowSpacing)
                            .child(_reverseY)
                            .child(ui::text("RenderLightingHeader").setText("Lighting").setStyleKey("text.small"))
                            .child(_iblDiffuse)
                            .child(_iblSpecular)
                            .child(ui::text("RenderAOHeader").setText("Ambient Occlusion").setStyleKey("text.small"))
                            .child(_ssaoEnable)
                            .child(ssaoBody)
                            .share();
    _deferredBody = deferredBody;

    auto deferredExpander = ui::collapsingHeader("RenderDeferredHeader")
                                .setTitle("Deferred")
                                .setSpacing(editor_density::kRowSpacing)
                                .child(_deferredUnavailable)
                                .child(deferredBody)
                                .share();
    _deferredExpander = deferredExpander;

    bindCallbacks();

    addDetachedChild(ui::column("RenderSettingsRows")
                         .setSpacing(editor_density::kSectionSpacing)
                         .child(ui::collapsingHeader("RenderPresentationHeader")
                                    .setTitle("Presentation")
                                    .setSpacing(editor_density::kRowSpacing)
                                    .child(labeledRow("RenderPipelineRow", "Render Pipeline", _pipeline))
                                    .child(_pipelinePending)
                                    .child(labeledRow("RenderViewportScaleRow", "Viewport Scale", _viewportScale))
                                    .child(_vsync)
                                    .child(_presentState)
                                    .child(labeledRow("RenderPresentModeRow", "Present Mode", _presentMode))
                                    .child(_reload, FBoxSlotArgs{.preferredSize = {190.0f, editor_density::kToolbarHeight}})
                                    .release())
                         .child(ui::collapsingHeader("RenderPostProcessHeader")
                                    .setTitle("Post Process")
                                    .setSpacing(editor_density::kRowSpacing)
                                    .child(_inversion)
                                    .child(labeledRow("RenderGrayscaleRow", "Grayscale", _grayscale))
                                    .child(labeledRow("RenderKernelRow", "Kernel", _kernel))
                                    .child(labeledRow("RenderKernelOffsetRow", "Kernel Texel Offset", _kernelOffset))
                                    .child(_toneMapping)
                                    .child(labeledRow("RenderToneCurveRow", "Tone Mapping Curve", _toneCurve))
                                    .child(labeledRow("RenderExposureRow", "Exposure", _exposure))
                                    .child(_gammaCorrection)
                                    .child(labeledRow("RenderGammaRow", "Gamma", _gamma))
                                    .child(_grain)
                                    .child(labeledRow("RenderGrainStrengthRow", "Grain Strength", _grainStrength))
                                    .child(_bloom)
                                    .child(labeledRow("RenderBloomThresholdRow", "Bloom Threshold", _bloomThreshold))
                                    .child(labeledRow("RenderBloomSoftKneeRow", "Bloom Soft Knee", _bloomSoftKnee))
                                    .child(labeledRow("RenderBloomExtractRow", "Bloom Extract Intensity", _bloomExtract))
                                    .child(labeledRow("RenderBloomPassesRow", "Bloom Blur Passes", _bloomPasses))
                                    .child(labeledRow("RenderBloomStrengthRow", "Bloom Strength", _bloomStrength))
                                    .release())
                         .child(ui::collapsingHeader("RenderShadowsHeader")
                                    .setTitle("Shadows")
                                    .setSpacing(editor_density::kRowSpacing)
                                    .child(_shadowEnable)
                                    .child(shadowBody)
                                    .release())
                         .child(deferredExpander)
                         .release());
}

void RuntimeRenderSettingsSection::bindCallbacks()
{
    _pipeline->_onSelectionChanged = [this](int index) {
        if (_bSyncing || !_app) {
            return;
        }
        if (auto* runtime = _app->getRenderServices().getDeviceState()) {
            runtime->setPendingRenderPipeline(static_cast<RenderDeviceState::ERenderPipeline>(index));
        }
    };
    _viewportScale->_onValueChanged = [this](float value) {
        if (_bSyncing || !_app) {
            return;
        }
        _app->getRenderServices().setViewportFrameBufferScale(value);
    };
    _vsync->_onChanged = [this](bool value) {
        if (_bSyncing || !_presentSurface) {
            return;
        }
        if (auto* sc = _presentSurface->getSwapchain()) {
            sc->setVsync(value);
        }
    };
    _presentMode->_onSelectionChanged = [this](int value) {
        if (_bSyncing || !_presentSurface || !_app) {
            return;
        }
        ISwapchain* sc = _presentSurface->getSwapchain();
        if (!sc) {
            return;
        }
        const auto mode = static_cast<EPresentMode::T>(value);
        _app->getTaskManager().registerFrameTask([sc, mode]() { sc->setPresentMode(mode); });
    };
    _reload->_onClick = [this]() {
        if (!_app) {
            return;
        }
        if (auto* runtime = _app->getRenderServices().getDeviceState()) {
            runtime->requestActivePipelineReload();
        }
    };

    _inversion->_onChanged = [this](bool value) {
        if (!_bSyncing) {
            mutatePostProcess(_app, [value](PostProcessingState& post) { post.bEnableInversion = value; });
        }
    };
    _grayscale->_onSelectionChanged = [this](int index) {
        if (!_bSyncing) {
            mutatePostProcess(_app, [index](PostProcessingState& post) {
                post.grayscaleMode = static_cast<PostProcessingState::EGrayscaleMode>(index);
            });
        }
    };
    _kernel->_onSelectionChanged = [this](int index) {
        if (!_bSyncing) {
            mutatePostProcess(_app, [index](PostProcessingState& post) {
                post.kernelMode = static_cast<PostProcessingState::EKernelMode>(index);
            });
        }
    };
    _kernelOffset->_onValueChanged = [this](float value) {
        if (!_bSyncing) {
            mutatePostProcess(_app, [value](PostProcessingState& post) { post.kernelTexelOffset = value; });
        }
    };
    _toneMapping->_onChanged = [this](bool value) {
        if (!_bSyncing) {
            mutatePostProcess(_app, [value](PostProcessingState& post) { post.bEnableToneMapping = value; });
        }
    };
    _toneCurve->_onSelectionChanged = [this](int index) {
        if (!_bSyncing) {
            mutatePostProcess(_app, [index](PostProcessingState& post) {
                post.toneMappingCurve = static_cast<PostProcessingState::EToneMappingCurve>(index);
            });
        }
    };
    _exposure->_onValueChanged = [this](float value) {
        if (!_bSyncing) {
            mutatePostProcess(_app, [value](PostProcessingState& post) { post.exposure = value; });
        }
    };
    _gammaCorrection->_onChanged = [this](bool value) {
        if (!_bSyncing) {
            mutatePostProcess(_app, [value](PostProcessingState& post) { post.bEnableGammaCorrection = value; });
        }
    };
    _gamma->_onValueChanged = [this](float value) {
        if (!_bSyncing) {
            mutatePostProcess(_app, [value](PostProcessingState& post) { post.gamma = value; });
        }
    };
    _grain->_onChanged = [this](bool value) {
        if (!_bSyncing) {
            mutatePostProcess(_app, [value](PostProcessingState& post) { post.bEnableRandomGrain = value; });
        }
    };
    _grainStrength->_onValueChanged = [this](float value) {
        if (!_bSyncing) {
            mutatePostProcess(_app, [value](PostProcessingState& post) { post.randomGrainStrength = value; });
        }
    };
    _bloom->_onChanged = [this](bool value) {
        if (!_bSyncing) {
            mutatePostProcess(_app, [value](PostProcessingState& post) { post.bEnableBloom = value; });
        }
    };
    _bloomThreshold->_onValueChanged = [this](float value) {
        if (!_bSyncing) {
            mutatePostProcess(_app, [value](PostProcessingState& post) { post.bloomThreshold = value; });
        }
    };
    _bloomSoftKnee->_onValueChanged = [this](float value) {
        if (!_bSyncing) {
            mutatePostProcess(_app, [value](PostProcessingState& post) { post.bloomSoftKnee = value; });
        }
    };
    _bloomExtract->_onValueChanged = [this](float value) {
        if (!_bSyncing) {
            mutatePostProcess(_app, [value](PostProcessingState& post) { post.bloomExtractIntensity = value; });
        }
    };
    _bloomPasses->_onValueChanged = [this](float value) {
        if (!_bSyncing) {
            mutatePostProcess(_app, [value](PostProcessingState& post) {
                post.bloomBlurPasses = static_cast<uint32_t>(asInt(value, 1, 12));
            });
        }
    };
    _bloomStrength->_onValueChanged = [this](float value) {
        if (!_bSyncing) {
            mutatePostProcess(_app, [value](PostProcessingState& post) { post.bloomStrength = value; });
        }
    };

    _shadowEnable->_onChanged = [this](bool enabled) {
        if (_bSyncing) {
            return;
        }
        mutateShadow(_app, [enabled](ShadowSettings& shadow) {
            if (enabled) {
                if (shadow.quality == EShadowQuality::Off) {
                    shadow.applyQualityPreset(EShadowQuality::Medium);
                }
            }
            else {
                shadow.quality = EShadowQuality::Off;
            }
        });
    };
    _shadowQuality->_onSelectionChanged = [this](int index) {
        if (!_bSyncing) {
            mutateShadow(_app, [index](ShadowSettings& shadow) {
                shadow.applyQualityPreset(static_cast<EShadowQuality::T>(index + 1));
            });
        }
    };
    _shadowResolution->_onValueChanged = [this](float value) {
        if (!_bSyncing) {
            mutateShadow(_app, [value](ShadowSettings& shadow) {
                shadow.resolution = static_cast<uint32_t>(asInt(value, 128, 8192));
            });
        }
    };
    _shadowBias->_onValueChanged = [this](float value) {
        if (!_bSyncing) {
            mutateShadow(_app, [value](ShadowSettings& shadow) { shadow.bias = value; });
        }
    };
    _shadowNormalBias->_onValueChanged = [this](float value) {
        if (!_bSyncing) {
            mutateShadow(_app, [value](ShadowSettings& shadow) { shadow.normalBias = value; });
        }
    };
    _shadowFilter->_onSelectionChanged = [this](int index) {
        if (!_bSyncing) {
            mutateShadow(_app, [index](ShadowSettings& shadow) {
                shadow.filter = static_cast<EShadowFilter::T>(index);
            });
        }
    };
    _directionalEnable->_onChanged = [this](bool value) {
        if (!_bSyncing) {
            mutateShadow(_app, [value](ShadowSettings& shadow) { shadow.directionalEnabled = value; });
        }
    };
    _shadowSingleMap->_onSelect = [this](UIRadioButton*) {
        if (!_bSyncing) {
            mutateShadow(_app, [](ShadowSettings& shadow) { shadow.directionalCascades = 1; });
        }
    };
    _shadowCSM->_onSelect = [this](UIRadioButton*) {
        if (!_bSyncing) {
            mutateShadow(_app, [](ShadowSettings& shadow) {
                shadow.directionalCascades = MAX_DIRECTIONAL_CASCADES;
                shadow.resetDirectionalCascadeSplitRatios();
            });
        }
    };
    _shadowDistance->_onValueChanged = [this](float value) {
        if (!_bSyncing) {
            mutateShadow(_app, [value](ShadowSettings& shadow) { shadow.directionalDistance = value; });
        }
    };
    _shadowStableFit->_onChanged = [this](bool value) {
        if (!_bSyncing) {
            mutateShadow(_app, [value](ShadowSettings& shadow) { shadow.directionalStableFit = value; });
        }
    };
    _shadowCascades->_onValueChanged = [this](float value) {
        if (!_bSyncing) {
            mutateShadow(_app, [value](ShadowSettings& shadow) {
                shadow.directionalCascades = static_cast<uint32_t>(
                    asInt(value, 2, static_cast<int>(MAX_DIRECTIONAL_CASCADES)));
                shadow.resetDirectionalCascadeSplitRatios();
            });
        }
    };
    for (uint32_t i = 0; i < _shadowSplits.size(); ++i) {
        _shadowSplits[i]->_onValueChanged = [this, i](float value) {
            if (!_bSyncing) {
                mutateShadow(_app, [i, value](ShadowSettings& shadow) {
                    shadow.directionalCascadeSplitRatios[i] = value;
                    shadow.sanitizeDirectionalCascadeSplitRatios();
                });
            }
        };
    }
    _shadowZRange->_onValueChanged = [this](float value) {
        if (!_bSyncing) {
            mutateShadow(_app, [value](ShadowSettings& shadow) { shadow.directionalDepthRangeMultiplier = value; });
        }
    };
    _pointEnable->_onChanged = [this](bool value) {
        if (!_bSyncing) {
            mutateShadow(_app, [value](ShadowSettings& shadow) { shadow.pointLightEnabled = value; });
        }
    };
    _pointIndirect->_onChanged = [this](bool value) {
        if (!_bSyncing) {
            mutateShadow(_app, [value](ShadowSettings& shadow) { shadow.pointLightUseIndirect = value; });
        }
    };
    _pointCull->_onChanged = [this](bool value) {
        if (!_bSyncing) {
            mutateShadow(_app, [value](ShadowSettings& shadow) { shadow.pointLightIndirectCullEnabled = value; });
        }
    };
    _pointMaxShadows->_onValueChanged = [this](float value) {
        if (!_bSyncing) {
            mutateShadow(_app, [value](ShadowSettings& shadow) {
                shadow.maxPointLightShadows = static_cast<uint32_t>(asInt(value, 0, static_cast<int>(MAX_POINT_LIGHTS)));
            });
        }
    };

    _reverseY->_onChanged = [this](bool value) {
        if (!_bSyncing) {
            mutateDeferred(_app, [value](DeferredRenderPipeline::SettingsSnapshot& snap) {
                snap.bReverseViewportY = value;
            });
        }
    };
    _iblDiffuse->_onChanged = [this](bool value) {
        if (!_bSyncing) {
            mutateDeferred(_app, [value](DeferredRenderPipeline::SettingsSnapshot& snap) {
                snap.bPBRDiffuseIBL = value;
            });
        }
    };
    _iblSpecular->_onChanged = [this](bool value) {
        if (!_bSyncing) {
            mutateDeferred(_app, [value](DeferredRenderPipeline::SettingsSnapshot& snap) {
                snap.bPBRSpecularIBL = value;
            });
        }
    };
    _ssaoEnable->_onChanged = [this](bool value) {
        if (!_bSyncing) {
            mutateDeferred(_app, [value](DeferredRenderPipeline::SettingsSnapshot& snap) {
                snap.bSSAOEnabled = value;
            });
        }
    };
    _ssaoRadius->_onValueChanged = [this](float value) {
        if (!_bSyncing) {
            mutateDeferred(_app, [value](DeferredRenderPipeline::SettingsSnapshot& snap) { snap.ssaoRadius = value; });
        }
    };
    _ssaoBias->_onValueChanged = [this](float value) {
        if (!_bSyncing) {
            mutateDeferred(_app, [value](DeferredRenderPipeline::SettingsSnapshot& snap) { snap.ssaoBias = value; });
        }
    };
    _ssaoPower->_onValueChanged = [this](float value) {
        if (!_bSyncing) {
            mutateDeferred(_app, [value](DeferredRenderPipeline::SettingsSnapshot& snap) { snap.ssaoPower = value; });
        }
    };
    _ssaoIntensity->_onValueChanged = [this](float value) {
        if (!_bSyncing) {
            mutateDeferred(_app, [value](DeferredRenderPipeline::SettingsSnapshot& snap) { snap.ssaoIntensity = value; });
        }
    };
}

void RuntimeRenderSettingsSection::sync(const App* app, IRenderSurfaceContext* presentSurface)
{
    if (presentSurface) {
        _presentSurface = presentSurface;
    }
    if (!app || !_pipeline) {
        return;
    }
    _app = const_cast<App*>(app);
    auto* runtime = app->getRenderServices().getDeviceState();
    if (!runtime) {
        return;
    }

    _bSyncing = true;

    const int pipeline = static_cast<int>(runtime->getPendingRenderPipeline());
    const bool pending = runtime->getPendingRenderPipeline() != runtime->getRenderPipeline();
    _pipeline->setSelectedIndex(pipeline, false);
    _pipelinePending->setText(pending ? "(switch pending)" : "");
    _viewportScale->setValue(app->getRenderServices().getViewportFrameBufferScale(), false);
    if (_presentSurface) {
        if (auto* sc = _presentSurface->getSwapchain()) {
            _vsync->setChecked(sc->getVsync());
            _presentState->setText(std::format("Present Mode: {}", presentLabel(static_cast<int>(sc->getPresentMode()))));
            _presentMode->setSelectedIndex(static_cast<int>(sc->getPresentMode()), false);
        }
    }

    auto* active = runtime->getActivePipeline();
    auto* deferred = dynamic_cast<DeferredRenderPipeline*>(active);
    auto* forward = dynamic_cast<ForwardRenderPipeline*>(active);
    const bool bDeferred = deferred != nullptr;

    PostProcessingState post{};
    ShadowSettings shadow{};
    if (deferred) {
        const auto snapshot = deferred->resolveSettingsSnapshot();
        post    = snapshot.postProcessing;
        shadow  = snapshot.shadow;
        _reverseY->setChecked(snapshot.bReverseViewportY);
        _iblDiffuse->setChecked(snapshot.bPBRDiffuseIBL);
        _iblSpecular->setChecked(snapshot.bPBRSpecularIBL);
        _ssaoEnable->setChecked(snapshot.bSSAOEnabled);
        _ssaoRadius->setValue(snapshot.ssaoRadius, false);
        _ssaoBias->setValue(snapshot.ssaoBias, false);
        _ssaoPower->setValue(snapshot.ssaoPower, false);
        _ssaoIntensity->setValue(snapshot.ssaoIntensity, false);
        _ssaoBody->setEnabled(snapshot.bSSAOEnabled);
    }
    else if (forward) {
        post   = forward->resolvePostProcessSettings();
        shadow = forward->getCurrentShadowSettings();
    }

    _inversion->setChecked(post.bEnableInversion);
    _grayscale->setSelectedIndex(static_cast<int>(post.grayscaleMode), false);
    _kernel->setSelectedIndex(static_cast<int>(post.kernelMode), false);
    _kernelOffset->setValue(post.kernelTexelOffset, false);
    _kernelOffset->setEnabled(post.kernelMode != PostProcessingState::EKernelMode::None);
    _toneMapping->setChecked(post.bEnableToneMapping);
    _toneCurve->setSelectedIndex(static_cast<int>(post.toneMappingCurve), false);
    _toneCurve->setEnabled(post.bEnableToneMapping);
    _exposure->setValue(post.exposure, false);
    _exposure->setEnabled(post.bEnableToneMapping);
    _gammaCorrection->setChecked(post.bEnableGammaCorrection);
    _gamma->setValue(post.gamma, false);
    _gamma->setEnabled(post.bEnableGammaCorrection);
    _grain->setChecked(post.bEnableRandomGrain);
    _grainStrength->setValue(post.randomGrainStrength, false);
    _grainStrength->setEnabled(post.bEnableRandomGrain);
    _bloom->setChecked(post.bEnableBloom);
    _bloomThreshold->setValue(post.bloomThreshold, false);
    _bloomSoftKnee->setValue(post.bloomSoftKnee, false);
    _bloomExtract->setValue(post.bloomExtractIntensity, false);
    _bloomPasses->setValue(static_cast<float>(post.bloomBlurPasses), false);
    _bloomStrength->setValue(post.bloomStrength, false);
    _bloomThreshold->setEnabled(post.bEnableBloom);
    _bloomSoftKnee->setEnabled(post.bEnableBloom);
    _bloomExtract->setEnabled(post.bEnableBloom);
    _bloomPasses->setEnabled(post.bEnableBloom);
    _bloomStrength->setEnabled(post.bEnableBloom);

    const bool bShadowOn = shadow.isEnabled();
    _shadowEnable->setChecked(bShadowOn);
    _shadowBody->setEnabled(bShadowOn);
    if (bShadowOn) {
        _shadowQuality->setSelectedIndex(std::max(0, static_cast<int>(shadow.quality) - 1), false);
    }
    _shadowResolution->setValue(static_cast<float>(shadow.resolution), false);
    _shadowBias->setValue(shadow.bias, false);
    _shadowNormalBias->setValue(shadow.normalBias, false);
    _shadowFilter->setSelectedIndex(static_cast<int>(shadow.filter), false);

    _directionalBody->setEnabled(bDeferred && bShadowOn);
    _directionalDetails->setEnabled(bDeferred && bShadowOn && shadow.directionalEnabled);
    _directionalUnavailable->setVisibility(bDeferred ? EWidgetVisibility::Collapsed : EWidgetVisibility::Visible);
    _directionalEnable->setChecked(shadow.directionalEnabled);
    const bool bCSM = shadow.directionalCascades > 1;
    _shadowSingleMap->setChecked(!bCSM);
    _shadowCSM->setChecked(bCSM);
    _shadowDistance->setValue(shadow.directionalDistance, false);
    _shadowStableFit->setChecked(shadow.directionalStableFit);
    _shadowCascades->setValue(static_cast<float>(shadow.directionalCascades), false);
    _shadowCascades->setEnabled(bCSM);
    for (uint32_t i = 0; i < _shadowSplits.size(); ++i) {
        _shadowSplits[i]->setValue(shadow.directionalCascadeSplitRatios[i], false);
        _shadowSplits[i]->setEnabled(bCSM && shadow.directionalCascades > i + 1);
    }
    _shadowZRange->setValue(shadow.directionalDepthRangeMultiplier, false);
    _shadowZRange->setEnabled(bCSM);

    _pointEnable->setChecked(shadow.pointLightEnabled);
    _pointIndirect->setChecked(shadow.pointLightUseIndirect);
    _pointCull->setChecked(shadow.pointLightIndirectCullEnabled);
    _pointMaxShadows->setValue(static_cast<float>(shadow.maxPointLightShadows), false);
    _pointIndirect->setEnabled(shadow.pointLightEnabled);
    _pointCull->setEnabled(shadow.pointLightEnabled);
    _pointMaxShadows->setEnabled(shadow.pointLightEnabled);

    _deferredExpander->setEnabled(bDeferred);
    _deferredBody->setEnabled(bDeferred);
    _deferredUnavailable->setVisibility(bDeferred ? EWidgetVisibility::Collapsed : EWidgetVisibility::Visible);

    _bSyncing = false;
}

} // namespace ya
