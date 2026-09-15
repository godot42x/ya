#include "../WorkbenchDemoPages.h"
#include "DemoPageCommon.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/UIAnimation.h"

#include <cstddef>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace guiworkbench
{

namespace
{

/// Typed handle for the gauge's own animatable channel (published next to the
/// widget, exactly like a built-in channel).
inline constexpr ya::TUIAnimProperty<float> kAnimFill{"fill"};

/// Demo-only widget proving the extension side of the animation seam: it
/// declares its own animatable property (fill) by extending the base table and
/// publishing a typed handle, so the framework tween drives it exactly like a
/// built-in channel. No framework file is touched to add this channel.
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
    {kAnimFill.id, ya::EUIAnimValueType::Float, &FAnimFillGauge::readFill, &FAnimFillGauge::writeFill},
};
const ya::FUIAnimPropertyTable FAnimFillGauge::kTable{
    FAnimFillGauge::kEntries, std::size(FAnimFillGauge::kEntries), &ya::uiElementAnimatableProperties()};

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

    // === Case 1: one-shot tweens on a leaf card (paint-only overlay) ===
    auto card = std::make_shared<ya::UIBorder>("AnimationCard");
    card->setColor({0.18f, 0.24f, 0.34f, 1.0f});
    card->setCornerRadius(10.0f);

    auto pop = ya::ui::animate(*card, 0.45f);
    pop->fade(0.0f, 1.0f, ya::EUIAnimEase::OutCubic)
        .scale({0.85f, 0.85f}, {1.0f, 1.0f}, ya::EUIAnimEase::OutBack);

    auto slide = ya::ui::animate(*card, 0.4f);
    slide->slide({-60.0f, 0.0f}, {0.0f, 0.0f}, ya::EUIAnimEase::OutCubic);

    // Pulse: the end callback alternates play()/playReverse(), i.e. a ping-pong
    // built from the one-shot primitive instead of a framework loop mode (a
    // looping clock would jump from the end back to the start).
    auto pulse          = ya::ui::animate(*card, 0.5f);
    auto bNextReverse   = std::make_shared<bool>(true);
    pulse->fade(0.35f, 1.0f, ya::EUIAnimEase::InOutQuad)
        .setOnFinished(
            [pulse, bNextReverse]
            {
                if (*bNextReverse) {
                    pulse->playReverse();
                }
                else {
                    pulse->play();
                }
                *bNextReverse = !*bNextReverse;
            });

    // === Case 2: subtree transition (one overlay animates a whole group) ===
    auto groupTween = std::make_shared<ya::UITweenBehavior>();
    auto group      = ya::ui::row("AnimationGroup").setSpacing(8.0f);
    for (int i = 0; i < 3; ++i)
    {
        group.child(ya::ui::border(std::format("AnimationGroupCard{}", i))
                        .setColor({0.22f, 0.30f, 0.40f, 1.0f})
                        .setCornerRadius(6.0f),
                    ya::ui::boxSlot().preferredSize({90.0f, 40.0f}));
    }
    auto groupRef = group.share();
    groupTween->addFloatTrack("opacity", 1.0f, 0.15f, ya::EUIAnimEase::InOutQuad).setDuration(0.25f);
    groupRef->addBehavior(groupTween);

    // === Case 3: a sequence (stagger) composed in the app layer ===
    auto staggerRow = ya::ui::row("AnimationStagger").setSpacing(6.0f);
    std::vector<std::shared_ptr<ya::UIBorder>> staggerCards;
    std::vector<std::shared_ptr<ya::UITweenBehavior>> staggerTweens;
    for (int i = 0; i < 5; ++i)
    {
        auto staggerCard = std::make_shared<ya::UIBorder>(std::format("AnimationStaggerCard{}", i));
        staggerCard->setColor({0.26f, 0.34f, 0.44f, 1.0f});
        staggerCard->setCornerRadius(6.0f);
        auto step = ya::ui::animate(*staggerCard, 0.3f);
        step->fade(0.0f, 1.0f, ya::EUIAnimEase::OutCubic).slide({0.0f, 18.0f}, {0.0f, 0.0f}, ya::EUIAnimEase::OutCubic);
        staggerCards.push_back(staggerCard);
        staggerTweens.push_back(step);
        staggerRow.child(staggerCard, ya::ui::boxSlot().preferredSize({60.0f, 36.0f}));
    }
    // Chaining: each step starts the next one on its own end callback. This is
    // the app-layer composition the Game UI clip player will formalise.
    for (size_t i = 0; i + 1 < staggerTweens.size(); ++i)
    {
        staggerTweens[i]->setOnFinished([next = staggerTweens[i + 1]] { next->play(); });
    }

    // === Case 4: a default-animated control (UISwitch) ===
    auto fastSwitch    = ya::ui::toggle("AnimSwitchFast").setText("Fast (0.12s)");
    auto slowSwitch    = ya::ui::toggle("AnimSwitchSlow").setText("Slow (0.45s)");
    auto instantSwitch = ya::ui::toggle("AnimSwitchInstant").setText("Instant (none)");
    auto onSwitch      = ya::ui::toggle("AnimSwitchOn").setText("Built already on").setChecked(true);
    slowSwitch.setTransitionSeconds(0.45f);
    instantSwitch.setTransitionSeconds(0.0f);
    onSwitch.setTransitionSeconds(0.2f);

    // === Case 5: an app widget's own animatable property ===
    auto gauge     = std::make_shared<FAnimFillGauge>("AnimationGauge");
    auto fillTween = ya::ui::animate(*gauge, 1.2f);
    fillTween->track(kAnimFill, 0.0f, 1.0f, ya::EUIAnimEase::InOutCubic);

    auto form = ya::ui::column("AnimationForm").setPadding({16.0f, 12.0f}).setSpacing(10.0f);
    form.child(header("AnimationTitle",
                      "Animation — framework clock/tween over animatable properties (paint-only overlay)"));
    form.child(body("AnimationHint",
                    "Layer split: this page uses the framework tween (clock + easing + widget properties). "
                    "Track/keyframe clips belong to the future Game UI layer and drive the same properties."));

    form.child(header("AnimationLeafTitle", "Leaf tweens — opacity / scale / translation / tint"));
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
                                  [pulse, card, bNextReverse, log]
                                  {
                                      if (pulse->isPlaying()) {
                                          pulse->stop();
                                          card->setRenderOpacity(1.0f);
                                          *bNextReverse = true;
                                          log("tween: pulse stopped");
                                      }
                                      else {
                                          *bNextReverse = true;
                                          pulse->play();
                                          log("tween: pulse loop via onFinished -> playReverse");
                                      }
                                  }),
                          ya::ui::boxSlot().preferredSize({170.0f, 26.0f}))
                   .child(demoButton("AnimReset", "Reset")
                              .setOnClick(
                                  [card, pop, slide, pulse, log]
                                  {
                                      pulse->stop();
                                      card->setRenderOpacity(1.0f);
                                      card->setRenderScale({1.0f, 1.0f});
                                      card->setRenderTranslation({0.0f, 0.0f});
                                      pop->stop();
                                      slide->stop();
                                      log("tween: card overlay reset (setters, not field pokes)");
                                  }),
                          ya::ui::boxSlot().preferredSize({90.0f, 26.0f})));
    form.child(card, ya::ui::boxSlot().preferredSize({0.0f, 80.0f}));

    form.child(header("AnimationGroupTitle", "Subtree transition — one overlay drives a whole group"));
    form.child(ya::ui::row("AnimationGroupButtons")
                   .setSpacing(8.0f)
                   .child(demoButton("AnimGroupHide", "Hide group")
                              .setOnClick(
                                  [groupTween, log]
                                  {
                                      groupTween->playToward(ya::EUIAnimDirection::Forward);
                                      log("tween: group opacity 1->0.15 (subtree repaint)");
                                  }),
                          ya::ui::boxSlot().preferredSize({110.0f, 26.0f}))
                   .child(demoButton("AnimGroupShow", "Show group")
                              .setOnClick(
                                  [groupTween, log]
                                  {
                                      groupTween->playToward(ya::EUIAnimDirection::Backward);
                                      log("tween: group opacity 0.15->1 (reverse, no snap)");
                                  }),
                          ya::ui::boxSlot().preferredSize({110.0f, 26.0f})));
    form.child(group);

    form.child(header("AnimationStaggerTitle", "Sequence — a stagger composed by chaining end callbacks"));
    form.child(ya::ui::row("AnimationStaggerButtons")
                   .setSpacing(8.0f)
                   .child(demoButton("AnimStagger", "Play sequence")
                              .setOnClick(
                                  [staggerTweens, log]
                                  {
                                      if (!staggerTweens.empty()) {
                                          staggerTweens.front()->play();
                                      }
                                      log("tween: 5 cards, each starting the next on finished");
                                  }),
                          ya::ui::boxSlot().preferredSize({130.0f, 26.0f})));
    form.child(staggerRow);

    form.child(header("AnimationSwitchTitle",
                      "Default-animated control — UISwitch animates its own state change"));
    form.child(body("AnimationSwitchHint",
                    "The switch owns one tween: the knob travels while the track colour blends. "
                    "Flipping mid-flight reverses from the current position."));
    form.child(fastSwitch);
    form.child(slowSwitch);
    form.child(instantSwitch);
    form.child(onSwitch);
    form.child(ya::ui::row("AnimationSwitchButtons")
                   .setSpacing(8.0f)
                   .child(demoButton("AnimToggleAll", "Toggle all")
                              .setOnClick(
                                  [fastRef = fastSwitch.share(),
                                   slowRef = slowSwitch.share(),
                                   instantRef = instantSwitch.share(),
                                   log]
                                  {
                                      const bool next = !(fastRef->isChecked() && slowRef->isChecked());
                                      fastRef->setChecked(next);
                                      slowRef->setChecked(next);
                                      instantRef->setChecked(next);
                                      log(next ? "switches -> on" : "switches -> off");
                                  }),
                          ya::ui::boxSlot().preferredSize({110.0f, 26.0f})));

    form.child(header("AnimationGaugeTitle", "Extension — an app widget's own animatable property"));
    form.child(body("AnimationGaugeHint",
                    "FAnimFillGauge declares 'fill' next to its own table and publishes a typed handle; "
                    "the framework tween drives it without knowing the widget class."));
    form.child(gauge, ya::ui::boxSlot().preferredSize({0.0f, 16.0f}));
    form.child(ya::ui::row("AnimationGaugeButtons")
                   .setSpacing(8.0f)
                   .child(demoButton("AnimFill", "Fill gauge")
                              .setOnClick(
                                  [fillTween, log]
                                  {
                                      fillTween->play();
                                      log("tween: custom property 'fill' 0->1 (1.2s)");
                                  }),
                          ya::ui::boxSlot().preferredSize({110.0f, 26.0f})));

    auto page = ya::ui::border("AnimationDemo").setColor(kPanelColor).child(std::move(form), ya::ui::contentSlot().fill());
    (void)ya::ui::attach(tree, parent, std::move(page).release(), ya::ui::canvasSlot().fill());

    // Page entry: the card pops in as soon as the tree ticks.
    pop->play();
    (void)state;
}

} // namespace guiworkbench
