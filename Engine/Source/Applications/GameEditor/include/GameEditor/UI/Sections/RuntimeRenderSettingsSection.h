#pragma once

#include "GUI/Widgets/CompoundWidget.h"

#include <array>
#include <memory>

namespace ya
{

struct App;
struct UIText;
struct UIButton;
struct UICheckBox;
struct UIComboBox;
struct UIDragFloat;
struct UIRadioButton;
struct UIContainer;
struct UIExpander;
struct IRenderSurfaceContext;

class RuntimeRenderSettingsSection final : public UICompoundWidget
{
  public:
    explicit RuntimeRenderSettingsSection(std::string name = "RuntimeRenderSettings",
                                          App* app = nullptr,
                                          IRenderSurfaceContext* presentSurface = nullptr);
    void sync(const App* app, IRenderSurfaceContext* presentSurface);

  protected:
    void construct() override;

  private:
    App* _app = nullptr;
    IRenderSurfaceContext* _presentSurface = nullptr;
    bool _bSyncing = false;

    std::shared_ptr<UIComboBox> _pipeline;
    std::shared_ptr<UIText> _pipelinePending;
    std::shared_ptr<UIDragFloat> _renderScale;
    std::shared_ptr<UICheckBox> _vsync;
    std::shared_ptr<UIText> _presentState;
    std::shared_ptr<UIComboBox> _presentMode;
    std::shared_ptr<UIButton> _reload;

    std::shared_ptr<UICheckBox> _inversion;
    std::shared_ptr<UIComboBox> _grayscale;
    std::shared_ptr<UIComboBox> _kernel;
    std::shared_ptr<UIDragFloat> _kernelOffset;
    std::shared_ptr<UICheckBox> _toneMapping;
    std::shared_ptr<UIComboBox> _toneCurve;
    std::shared_ptr<UIDragFloat> _exposure;
    std::shared_ptr<UICheckBox> _gammaCorrection;
    std::shared_ptr<UIDragFloat> _gamma;
    std::shared_ptr<UICheckBox> _grain;
    std::shared_ptr<UIDragFloat> _grainStrength;
    std::shared_ptr<UICheckBox> _bloom;
    std::shared_ptr<UIDragFloat> _bloomThreshold;
    std::shared_ptr<UIDragFloat> _bloomSoftKnee;
    std::shared_ptr<UIDragFloat> _bloomExtract;
    std::shared_ptr<UIDragFloat> _bloomPasses;
    std::shared_ptr<UIDragFloat> _bloomStrength;

    std::shared_ptr<UICheckBox> _shadowEnable;
    std::shared_ptr<UIContainer> _shadowBody;
    std::shared_ptr<UIComboBox> _shadowQuality;
    std::shared_ptr<UIDragFloat> _shadowResolution;
    std::shared_ptr<UIDragFloat> _shadowBias;
    std::shared_ptr<UIDragFloat> _shadowNormalBias;
    std::shared_ptr<UIComboBox> _shadowFilter;
    std::shared_ptr<UIContainer> _directionalBody;
    std::shared_ptr<UIContainer> _directionalDetails;
    std::shared_ptr<UIText> _directionalUnavailable;
    std::shared_ptr<UICheckBox> _directionalEnable;
    std::shared_ptr<UIRadioButton> _shadowSingleMap;
    std::shared_ptr<UIRadioButton> _shadowCSM;
    std::shared_ptr<UIDragFloat> _shadowDistance;
    std::shared_ptr<UICheckBox> _shadowStableFit;
    std::shared_ptr<UIDragFloat> _shadowCascades;
    std::array<std::shared_ptr<UIDragFloat>, 3> _shadowSplits{};
    std::shared_ptr<UIDragFloat> _shadowZRange;
    std::shared_ptr<UICheckBox> _pointEnable;
    std::shared_ptr<UICheckBox> _pointIndirect;
    std::shared_ptr<UICheckBox> _pointCull;
    std::shared_ptr<UIDragFloat> _pointMaxShadows;

    std::shared_ptr<UIExpander> _deferredExpander;
    std::shared_ptr<UIText> _deferredUnavailable;
    std::shared_ptr<UIContainer> _deferredBody;
    std::shared_ptr<UICheckBox> _reverseY;
    std::shared_ptr<UICheckBox> _iblDiffuse;
    std::shared_ptr<UICheckBox> _iblSpecular;
    std::shared_ptr<UICheckBox> _ssaoEnable;
    std::shared_ptr<UIContainer> _ssaoBody;
    std::shared_ptr<UIDragFloat> _ssaoRadius;
    std::shared_ptr<UIDragFloat> _ssaoBias;
    std::shared_ptr<UIDragFloat> _ssaoPower;
    std::shared_ptr<UIDragFloat> _ssaoIntensity;

    void bindCallbacks();
};
} // namespace ya
