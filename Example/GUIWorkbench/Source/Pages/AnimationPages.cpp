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
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setStyleKey(std::string(ya::StyleKey::TextMuted));
    };
    auto body = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setStyleKey(std::string(ya::StyleKey::Text));
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
    auto group = ya::ui::row("AnimationGroup").setSpacing(8.0f);
    for (int i = 0; i < 3; ++i)
    {
        group.child(ya::ui::border(std::format("AnimationGroupCard{}", i))
                        .setColor({0.22f, 0.30f, 0.40f, 1.0f})
                        .setCornerRadius(6.0f),
                    ya::ui::boxSlot().preferredSize({90.0f, 40.0f}));
    }
    auto groupRef   = group.share();
    auto groupTween = ya::ui::animate(*groupRef, 0.25f);
    groupTween->addFloatTrack("opacity", 1.0f, 0.15f, ya::EUIAnimEase::InOutQuad);

    // === Case 3: a sequence (stagger) composed in the app layer ===
    auto staggerRow = ya::ui::row("AnimationStagger").setSpacing(6.0f);
    std::vector<std::shared_ptr<ya::UIBorder>> staggerCards;
    std::vector<std::shared_ptr<ya::UITween>> staggerTweens;
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

    // === Case 4: one property, several keyframes (a toast) ===
    // Slide in -> hold -> slide out, driven by ONE clock: the curve form of a
    // track expresses multi-step motion without chaining tweens per step.
    auto toastCard = std::make_shared<ya::UIBorder>("AnimationToastCard");
    toastCard->setColor({0.20f, 0.32f, 0.26f, 1.0f});
    toastCard->setCornerRadius(8.0f);
    auto toastTween = ya::ui::animate(*toastCard, 1.6f);
    toastTween->curve(ya::ui::anim::opacity,
                      {ya::animKey(0.0f, 0.0f),
                       ya::animKey(0.25f, 1.0f, ya::EUIAnimEase::OutCubic),
                       ya::animKey(0.75f, 1.0f),                       // hold
                       ya::animKey(1.0f, 0.0f, ya::EUIAnimEase::InCubic)})
        .curve(ya::ui::anim::translation,
               {ya::animKey(0.0f, glm::vec2(0.0f, 28.0f)),
                ya::animKey(0.25f, glm::vec2(0.0f, 0.0f), ya::EUIAnimEase::OutBack),
                ya::animKey(0.75f, glm::vec2(0.0f, 0.0f)),
                ya::animKey(1.0f, glm::vec2(0.0f, -16.0f), ya::EUIAnimEase::InCubic)});
    // A second, independent curve channel on the same widget: the stripe walks
    // through three positions with different easings per segment.
    auto stripe     = std::make_shared<ya::UIBorder>("AnimationToastStripe");
    stripe->setColor({0.85f, 0.92f, 0.72f, 1.0f});
    auto stripeTween = ya::ui::animate(*stripe, 1.6f);
    stripeTween->curve(ya::ui::anim::translation,
                       {ya::animKey(0.0f, glm::vec2(-24.0f, 0.0f)),
                        ya::animKey(0.25f, glm::vec2(0.0f, 0.0f), ya::EUIAnimEase::OutBack),
                        ya::animKey(0.5f, glm::vec2(0.0f, 0.0f)),
                        ya::animKey(1.0f, glm::vec2(24.0f, 0.0f), ya::EUIAnimEase::InOutQuad)});
    toastCard->addDetachedChild(stripe, [](ya::UIElement&, ya::UISlot& slot) {
        if (auto* box = dynamic_cast<ya::UIBoxSlot*>(&slot)) {
            box->setPreferredSize({40.0f, 10.0f});
        }
    });

    // === Case 5: a default-animated control (UISwitch) ===
    auto fastSwitch    = ya::ui::toggle("AnimSwitchFast").setText("Fast (0.12s)");
    auto slowSwitch    = ya::ui::toggle("AnimSwitchSlow").setText("Slow (0.45s)");
    auto instantSwitch = ya::ui::toggle("AnimSwitchInstant").setText("Instant (none)");
    auto onSwitch      = ya::ui::toggle("AnimSwitchOn").setText("Built already on").setChecked(true);
    // Handles next to their builders: mounting an lvalue no longer consumes the
    // builder, so taking the handle after `form.child(...)` would work too.
    auto fastRef       = fastSwitch.share();
    auto slowRef       = slowSwitch.share();
    auto instantRef    = instantSwitch.share();
    slowSwitch.setTransitionSeconds(0.45f);
    instantSwitch.setTransitionSeconds(0.0f);
    onSwitch.setTransitionSeconds(0.2f);

    // === Case 6: an app widget's own animatable property ===
    auto gauge     = std::make_shared<FAnimFillGauge>("AnimationGauge");
    auto fillTween = ya::ui::animate(*gauge, 1.2f);
    fillTween->track(kAnimFill, 0.0f, 1.0f, ya::EUIAnimEase::InOutCubic);

    auto form = ya::ui::column("AnimationForm").setPadding({16.0f, 12.0f}).setSpacing(10.0f);
    form.child(header("AnimationTitle",
                      "Animation — framework clock/tween over animatable properties (paint-only overlay)"));
    form.child(body("AnimationHint",
                    "Layer split: this page uses the framework tween (clock + easing + widget properties). "
                    "Track/keyframe clips belong to the future Game UI layer and drive the same properties."));

    // Two columns keep every case above the fold: the tween cases on the left,
    // the widget-level cases (switch, app property) on the right.
    auto left  = ya::ui::column("AnimationCases").setSpacing(8.0f);
    auto right = ya::ui::column("AnimationWidgets").setSpacing(8.0f);

    left.child(header("AnimationLeafTitle", "Leaf tweens — opacity / scale / translation / tint"));
    left.child(ya::ui::row("AnimationButtons")
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
    left.child(card, ya::ui::boxSlot().preferredSize({0.0f, 72.0f}));

    left.child(header("AnimationCurveTitle",
                      "Keyframe curve — one property, several keys (slide in → hold → out)"));
    left.child(body("AnimationCurveHint",
                    "A curve is a track sampled at N keys on the same clock: no per-step chaining. "
                    "Key times are normalized (0..1), so setDuration retimes the whole curve."));
    left.child(ya::ui::row("AnimationCurveButtons")
                   .setSpacing(8.0f)
                   .child(demoButton("AnimToast", "Play toast")
                              .setOnClick(
                                  [toastTween, stripeTween, log]
                                  {
                                      toastTween->play();
                                      stripeTween->play();
                                      log("curve: opacity 0->1 (hold) ->0, translation 28->0 ->-16 (1.6s)");
                                  }),
                          ya::ui::boxSlot().preferredSize({110.0f, 26.0f})));
    left.child(toastCard, ya::ui::boxSlot().preferredSize({0.0f, 44.0f}));

    left.child(header("AnimationGroupTitle", "Subtree transition — one overlay drives a whole group"));
    left.child(ya::ui::row("AnimationGroupButtons")
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
    left.child(group);

    left.child(header("AnimationStaggerTitle", "Sequence — a stagger composed by chaining end callbacks"));
    left.child(ya::ui::row("AnimationStaggerButtons")
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
    left.child(staggerRow);

    right.child(header("AnimationSwitchTitle",
                      "Default-animated control — UISwitch animates its own state change"));
    right.child(body("AnimationSwitchHint",
                    "The switch owns one tween: the knob travels while the track colour blends. "
                    "Flipping mid-flight reverses from the current position."));
    right.child(fastSwitch);
    right.child(slowSwitch);
    right.child(instantSwitch);
    right.child(onSwitch);
    right.child(ya::ui::row("AnimationSwitchButtons")
                   .setSpacing(8.0f)
                   .child(demoButton("AnimToggleAll", "Toggle all")
                              .setOnClick(
                                  [fastRef, slowRef, instantRef, log]
                                  {
                                      const bool next = !(fastRef->isChecked() && slowRef->isChecked());
                                      fastRef->setChecked(next);
                                      slowRef->setChecked(next);
                                      instantRef->setChecked(next);
                                      log(next ? "switches -> on" : "switches -> off");
                                  }),
                          ya::ui::boxSlot().preferredSize({110.0f, 26.0f})));

    right.child(header("AnimationGaugeTitle", "Extension — an app widget's own animatable property"));
    right.child(body("AnimationGaugeHint",
                    "FAnimFillGauge declares 'fill' next to its own table and publishes a typed handle; "
                    "the framework tween drives it without knowing the widget class."));
    right.child(gauge, ya::ui::boxSlot().preferredSize({0.0f, 16.0f}));
    right.child(ya::ui::row("AnimationGaugeButtons")
                   .setSpacing(8.0f)
                   .child(demoButton("AnimFill", "Fill gauge")
                              .setOnClick(
                                  [fillTween, log]
                                  {
                                      fillTween->play();
                                      log("tween: custom property 'fill' 0->1 (1.2s)");
                                  }),
                          ya::ui::boxSlot().preferredSize({110.0f, 26.0f})));

    form.child(ya::ui::row("AnimationBody")
                   .setSpacing(16.0f)
                   .child(std::move(left), ya::ui::boxSlot().preferredSize({520.0f, 0.0f}))
                   .child(std::move(right), ya::ui::boxSlot().preferredSize({380.0f, 0.0f})));

    auto page = ya::ui::border("AnimationDemo").setStyleKey(std::string(ya::StyleKey::Panel)).child(std::move(form), ya::ui::contentSlot().fill());
    (void)ya::ui::attach(tree, parent, std::move(page).release(), ya::ui::canvasSlot().fill());

    // Page entry: the card pops in as soon as the tree ticks.
    pop->play();
    (void)state;
}

} // namespace guiworkbench
