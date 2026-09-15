#include "../WorkbenchDemoPages.h"
#include "DemoPageCommon.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/UIAnimation.h"

#include <cstddef>
#include <memory>
#include <string>

namespace guiworkbench
{

namespace
{

/// Demo-only widget proving the extension side of the animation seam: it
/// declares its own animatable property (fill) by extending the base table,
/// so the framework tween drives it exactly like a built-in property. No
/// framework file is touched to add this channel.
class FAnimFillGauge final : public ya::UIElement
{
  public:
    explicit FAnimFillGauge(std::string name) : ya::UIElement(std::move(name)) {}

    void setFill(float value)
    {
        value = glm::clamp(value, 0.0f, 1.0f);
        if (_fill == value) {
            return;
        }
        _fill = value;
        invalidateProperty(ya::EUIPropertyImpact::Paint);
    }
    [[nodiscard]] float getFill() const { return _fill; }

    [[nodiscard]] const ya::FUIAnimPropertyTable* getAnimatableProperties() const override { return &kTable; }

  protected:
    void paintSelf(ya::UIFrameBuilder& builder) override
    {
        const ya::Rect2D rect = getLayoutRect();
        builder.addRoundedRect(rect, {0.16f, 0.18f, 0.23f, 1.0f}, 4.0f);
        if (_fill > 0.0f) {
            ya::Rect2D bar = rect;
            bar.extent.x   = rect.extent.x * _fill;
            builder.addRoundedRect(bar, {0.36f, 0.74f, 0.50f, 1.0f}, 4.0f);
        }
    }

  private:
    static ya::FUIAnimValue readFill(const ya::UIElement& owner)
    {
        return ya::FUIAnimValue::fromFloat(static_cast<const FAnimFillGauge&>(owner).getFill());
    }
    static bool writeFill(ya::UIElement& owner, const ya::FUIAnimValue& value)
    {
        if (value.type != ya::EUIAnimValueType::Float) {
            return false;
        }
        static_cast<FAnimFillGauge&>(owner).setFill(value.asFloat());
        return true;
    }

    static const ya::FUIAnimPropertyDesc kEntries[];
    static const ya::FUIAnimPropertyTable kTable;
    float _fill = 0.0f;
};

const ya::FUIAnimPropertyDesc FAnimFillGauge::kEntries[] = {
    {.id="fill", .type=ya::EUIAnimValueType::Float, .read=&FAnimFillGauge::readFill, .write=&FAnimFillGauge::writeFill},
};
const ya::FUIAnimPropertyTable FAnimFillGauge::kTable{
    .entries=FAnimFillGauge::kEntries, .count=std::size(FAnimFillGauge::kEntries), .base=&ya::uiElementAnimatableProperties()};

} // namespace

void buildAnimationDemo(ya::WidgetTree& tree, ya::UICanvasPanel& parent, FDemoState& state,
                        const std::function<void(const std::string&)>& log)
{
    auto header = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kHeaderColor);
    };
    auto body = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kTextColor);
    };
    auto demoButton = [](std::string name, const std::string& label)
    {
        auto button = ya::ui::button(name).child(ya::ui::text(name + "_Label")
                                                     .setText(label)
                                                     .setFontSize(13)
                                                     .setHAlign(ya::EWidgetAlignH::Center)
                                                     .setVAlign(ya::EWidgetAlignV::Center));
        return std::move(button).setContentPadding({12.0f, 4.0f});
    };

    auto card = std::make_shared<ya::UIBorder>("AnimationCard");
    card->setColor({0.18f, 0.24f, 0.34f, 1.0f});
    card->setCornerRadius(10.0f);

    // Pop in: opacity + scale on one clock (paint-only, no layout reflow).
    auto pop = std::make_shared<ya::UITweenBehavior>();
    pop->addFloatTrack("opacity", 0.0f, 1.0f, ya::EUIAnimEase::OutCubic);
    pop->addVec2Track("renderScale", {0.85f, 0.85f}, {1.0f, 1.0f}, ya::EUIAnimEase::OutBack);
    pop->setDuration(0.45f);
    card->addBehavior(pop);

    // Slide in: paint-only translation from -60px to the layout position.
    auto slide = std::make_shared<ya::UITweenBehavior>();
    slide->addVec2Track("renderTranslation", {-60.0f, 0.0f}, {0.0f, 0.0f}, ya::EUIAnimEase::OutCubic);
    slide->setDuration(0.4f);
    card->addBehavior(slide);

    // Pulse: the finished callback chains playReverse(), i.e. a ping-pong
    // without asking the framework for a second clock mode.
    auto pulse      = std::make_shared<ya::UITweenBehavior>();
    auto bPulseDown = std::make_shared<bool>(true);
    pulse->addFloatTrack("opacity", 1.0f, 0.3f, ya::EUIAnimEase::InOutQuad);
    pulse->setDuration(0.5f);
    pulse->setOnFinished(
        [pulse, bPulseDown]
        {
            if (*bPulseDown) {
                *bPulseDown = false;
                pulse->playReverse();
            }
            else {
                *bPulseDown = true;
                pulse->play();
            }
        });
    card->addBehavior(pulse);

    // The gauge is driven through its OWN animatable property ("fill"), which
    // the framework knows nothing about.
    auto gauge     = std::make_shared<FAnimFillGauge>("AnimationGauge");
    auto fillTween = std::make_shared<ya::UITweenBehavior>();
    fillTween->addFloatTrack("fill", 0.0f, 1.0f, ya::EUIAnimEase::InOutCubic);
    fillTween->setDuration(1.2f);
    gauge->addBehavior(fillTween);

    auto form = ya::ui::column("AnimationForm").setPadding({16.0f, 12.0f}).setSpacing(10.0f);
    form.child(header("AnimationTitle",
                      "Animation — framework clock/tween over animatable properties (paint-only overlay)"));
    form.child(body("AnimationHint",
                    "Layer split: this page uses the framework tween (clock + easing + widget properties). "
                    "Track/keyframe clips belong to the future Game UI layer and drive the same properties."));
    form.child(ya::ui::row("AnimationButtons")
                   .setSpacing(8.0f)
                   .child(demoButton("AnimPop", "Pop in")
                              .setOnClick(
                                  [pop, log]
                                  {
                                      pop->play();
                                      log("tween: opacity 0->1 + renderScale 0.85->1 (0.45s)");
                                  }),
                          ya::ui::boxSlot().preferredSize({110.0f, 26.0f}))
                   .child(demoButton("AnimSlide", "Slide in")
                              .setOnClick(
                                  [slide, log]
                                  {
                                      slide->play();
                                      log("tween: renderTranslation -60->0 (0.4s)");
                                  }),
                          ya::ui::boxSlot().preferredSize({110.0f, 26.0f}))
                   .child(demoButton("AnimPulse", "Pulse (ping-pong)")
                              .setOnClick(
                                  [pulse, card, bPulseDown, log]
                                  {
                                      if (pulse->isPlaying()) {
                                          pulse->stop();
                                          card->setRenderOpacity(1.0f);
                                          log("tween: pulse stopped");
                                      }
                                      else {
                                          *bPulseDown = true;
                                          pulse->play();
                                          log("tween: pulse loop via onFinished -> playReverse");
                                      }
                                  }),
                          ya::ui::boxSlot().preferredSize({170.0f, 26.0f}))
                   .child(demoButton("AnimFill", "Fill gauge")
                              .setOnClick(
                                  [fillTween, log]
                                  {
                                      fillTween->play();
                                      log("tween: custom property 'fill' 0->1 (1.2s)");
                                  }),
                          ya::ui::boxSlot().preferredSize({120.0f, 26.0f})));
    form.child(header("AnimationCardTitle", "Target — a plain UIBorder, no animation code in the widget"));
    form.child(card, ya::ui::boxSlot().preferredSize({0.0f, 90.0f}));
    form.child(header("AnimationGaugeTitle", "Extension — an app widget's own animatable property"));
    form.child(gauge, ya::ui::boxSlot().preferredSize({0.0f, 16.0f}));

    auto page = ya::ui::border("AnimationDemo").setColor(kPanelColor).child(std::move(form), ya::ui::contentSlot().fill());
    (void)ya::ui::attach(tree, parent, std::move(page).release(), ya::ui::canvasSlot().fill());

    // Page entry: the card pops in as soon as the tree ticks.
    pop->play();
    (void)state;
}

} // namespace guiworkbench
