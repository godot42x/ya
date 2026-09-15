// Framework-layer GUI animation contract:
//   * the animatable-property seam resolves ids against a widget type's
//     declared table (own entries + inherited base entries);
//   * every write goes through the widget's changed-only setter, so animation
//     is a normal invalidation source (a finished tween stops dirtying);
//   * the render transform (opacity/translation/scale/tint) is paint-only and
//     inherits to the subtree, resolved into draw items at emit time;
//   * a downstream widget type adds its own animatable property without
//     touching the framework or the tween driver (open/closed seam).

#include "GUI/Widgets/UIAnimation.h"
#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Container.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace ya
{

namespace
{

/// Downstream-module widget used by the seam tests: it declares its own
/// animatable property (gauge) by extending the base table, exactly like a
/// game-facing control would - no framework change involved.
class UIAnimProbeWidget final : public UIElement
{
  public:
    explicit UIAnimProbeWidget(std::string name) : UIElement(std::move(name)) {}

    void setGauge(float value)
    {
        value = glm::clamp(value, 0.0f, 1.0f);
        if (_gauge == value) {
            return;
        }
        _gauge = value;
        invalidateProperty(EUIPropertyImpact::Paint);
    }
    [[nodiscard]] float getGauge() const { return _gauge; }

    const FUIAnimPropertyTable* getAnimatableProperties() const override { return &kTable; }

  private:
    static FUIAnimValue readGauge(const UIElement& owner)
    {
        return FUIAnimValue::fromFloat(static_cast<const UIAnimProbeWidget&>(owner).getGauge());
    }
    static bool writeGauge(UIElement& owner, const FUIAnimValue& value)
    {
        if (value.type != EUIAnimValueType::Float) {
            return false;
        }
        static_cast<UIAnimProbeWidget&>(owner).setGauge(value.asFloat());
        return true;
    }

    static const FUIAnimPropertyDesc kEntries[];
    static const FUIAnimPropertyTable kTable;
    float _gauge = 0.0f;
};

const FUIAnimPropertyDesc UIAnimProbeWidget::kEntries[] = {
    {"gauge", EUIAnimValueType::Float, &UIAnimProbeWidget::readGauge, &UIAnimProbeWidget::writeGauge},
};
const FUIAnimPropertyTable UIAnimProbeWidget::kTable{
    UIAnimProbeWidget::kEntries, std::size(UIAnimProbeWidget::kEntries), &uiElementAnimatableProperties()};

} // namespace

TEST(GuiAnimationTest, SeamListsOwnThenInheritedProperties)
{
    UIAnimProbeWidget probe("Probe");
    UIElement         plain("Plain");

    // The probe inherits the four base paint-transform properties after its own.
    const auto ids = probe.collectAnimatablePropertyIds();
    ASSERT_EQ(ids.size(), 5u);
    EXPECT_EQ(ids[0], std::string_view("gauge"));
    EXPECT_EQ(ids[1], std::string_view("opacity"));
    EXPECT_EQ(ids[2], std::string_view("renderTranslation"));
    EXPECT_EQ(ids[3], std::string_view("renderScale"));
    EXPECT_EQ(ids[4], std::string_view("tint"));

    // Unknown ids stay unknown on both widget types.
    EXPECT_EQ(plain.findAnimatableProperty("gauge"), nullptr);
    EXPECT_EQ(probe.findAnimatableProperty("notAProperty"), nullptr);
}

TEST(GuiAnimationTest, ApplyGoesThroughChangedOnlySetter)
{
    UIElement widget("W");
    EXPECT_FLOAT_EQ(widget.getRenderOpacity(), 1.0f);

    EXPECT_TRUE(widget.applyAnimatableProperty("opacity", FUIAnimValue::fromFloat(0.4f)));
    EXPECT_FLOAT_EQ(widget.getRenderOpacity(), 0.4f);
    // The overlay setter invalidates the subtree so cached draw items cannot
    // go stale under an inherited overlay.
    EXPECT_TRUE(widget.isPaintDirty());

    // A wrong value type must be rejected, never reinterpreted.
    EXPECT_FALSE(widget.applyAnimatableProperty("opacity", FUIAnimValue::fromVec2({0.1f, 0.2f})));
    EXPECT_FLOAT_EQ(widget.getRenderOpacity(), 0.4f);
    EXPECT_FALSE(widget.applyAnimatableProperty("doesNotExist", FUIAnimValue::fromFloat(0.5f)));
}

TEST(GuiAnimationTest, TweenAdvancesThenStopsDirtying)
{
    WidgetTree tree({.width = 400, .height = 200});
    auto       card  = std::make_shared<UIBorder>("Card");
    card->setColor({1.0f, 1.0f, 1.0f, 1.0f});
    auto       tween = std::make_shared<UITweenBehavior>();
    tween->addFloatTrack("opacity", 0.0f, 1.0f, EUIAnimEase::Linear);
    tween->setDuration(1.0f);

    FCanvasSlotArgs slot;
    slot.offset = {10.0f, 10.0f};
    slot.fixedSize = {100.0f, 50.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), card, slot);
    card->addBehavior(tween);

    EXPECT_FALSE(card->wantsTick());
    tween->play();
    // play() applies t=0 immediately through the changed-only setter.
    EXPECT_TRUE(card->wantsTick());
    EXPECT_FLOAT_EQ(card->getRenderOpacity(), 0.0f);

    tree.tick(0.25f);
    EXPECT_FLOAT_EQ(card->getRenderOpacity(), 0.25f);
    EXPECT_TRUE(card->isPaintDirty());

    UIFrameSnapshot mid = tree.buildSnapshot({});
    const auto item = std::find_if(mid.items.begin(), mid.items.end(), [](const UIFrameDrawItem& it) {
        return it.kind == UIFrameDrawItem::EKind::Sprite && it.size == glm::vec2(100.0f, 50.0f);
    });
    ASSERT_NE(item, mid.items.end());
    EXPECT_FLOAT_EQ(item->color.a, 0.25f);

    tree.tick(0.75f);
    EXPECT_FLOAT_EQ(card->getRenderOpacity(), 1.0f);
    EXPECT_FALSE(card->wantsTick());
    EXPECT_FALSE(tween->isPlaying());

    tree.buildSnapshot({});

    // A finished tween must not keep the tree rebuilding: a later tick does
    // not dirty the widget and the next snapshot reuses its cached segment.
    tree.tick(0.5f);
    EXPECT_FALSE(card->isPaintDirty());
    tree.buildSnapshot({});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);
}

TEST(GuiAnimationTest, TweenDrivesDownstreamPropertyWithoutFrameworkChange)
{
    WidgetTree tree({.width = 400, .height = 200});
    auto probe  = std::make_shared<UIAnimProbeWidget>("Probe");
    FCanvasSlotArgs slot;
    slot.fixedSize = {80.0f, 40.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), probe, slot);
    auto tween  = std::make_shared<UITweenBehavior>();
    // from != the property default, so "play applies t=0 immediately" is
    // actually observable instead of coinciding with the initial value.
    tween->addFloatTrack("gauge", 0.2f, 1.0f, EUIAnimEase::Linear);
    tween->setDuration(1.0f);
    probe->addBehavior(tween);

    tween->play();
    EXPECT_FLOAT_EQ(probe->getGauge(), 0.2f);
    EXPECT_TRUE(probe->wantsTick());

    tween->tick(*probe, 0.5f);
    EXPECT_FLOAT_EQ(probe->getGauge(), 0.6f);
    EXPECT_TRUE(probe->isPaintDirty());

    tween->tick(*probe, 0.5f);
    EXPECT_FLOAT_EQ(probe->getGauge(), 1.0f); // end of the track
    EXPECT_FALSE(probe->wantsTick());

    // Type mismatch is rejected at the seam, so a track can never corrupt a
    // property domain through the generic driver.
    EXPECT_FALSE(probe->applyAnimatableProperty("gauge", FUIAnimValue::fromVec4(glm::vec4(1.0f))));
}

TEST(GuiAnimationTest, RenderOverlayAppliesToOwnItems)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       card = std::make_shared<UIBorder>("Card");
    card->setColor({1.0f, 1.0f, 1.0f, 1.0f});
    FCanvasSlotArgs slot;
    slot.offset    = {20.0f, 20.0f};
    slot.fixedSize = {200.0f, 100.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), card, slot);

    const auto findFill = [](const UIFrameSnapshot& snapshot, glm::vec2 size) {
        return std::find_if(snapshot.items.begin(), snapshot.items.end(), [size](const UIFrameDrawItem& it) {
            return it.kind == UIFrameDrawItem::EKind::Sprite && it.size == size;
        });
    };

    // Baseline: the widget's own fill at identity overlay.
    UIFrameSnapshot snapshot = tree.buildSnapshot({});
    auto item = findFill(snapshot, glm::vec2(200.0f, 100.0f));
    ASSERT_NE(item, snapshot.items.end());
    EXPECT_EQ(item->pos, glm::vec2(20.0f, 20.0f));
    EXPECT_EQ(item->color, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

    // Opacity maps the own draw item.
    card->setRenderOpacity(0.5f);
    snapshot = tree.buildSnapshot({});
    item     = findFill(snapshot, glm::vec2(200.0f, 100.0f));
    ASSERT_NE(item, snapshot.items.end());
    EXPECT_FLOAT_EQ(item->color.a, 0.5f);

    // Translation moves the own draw item without touching layout.
    card->setRenderOpacity(1.0f);
    card->setRenderTranslation({40.0f, 20.0f});
    snapshot = tree.buildSnapshot({});
    item     = findFill(snapshot, glm::vec2(200.0f, 100.0f));
    ASSERT_NE(item, snapshot.items.end());
    EXPECT_EQ(item->pos, glm::vec2(60.0f, 40.0f));

    // Scale around the center pivot (120,70 of the rect 20,20 + 200,100).
    card->setRenderTranslation({0.0f, 0.0f});
    card->setRenderScale({0.5f, 0.5f});
    snapshot = tree.buildSnapshot({});
    item     = findFill(snapshot, glm::vec2(100.0f, 50.0f));
    ASSERT_NE(item, snapshot.items.end());
    EXPECT_EQ(item->pos, glm::vec2(70.0f, 45.0f));
}

TEST(GuiAnimationTest, RenderOverlayReachesSubtreeItems)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIContainer>("Panel");
    panel->setClipChildren(true);
    FCanvasSlotArgs panelSlot;
    panelSlot.offset    = {20.0f, 20.0f};
    panelSlot.fixedSize = {200.0f, 100.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, panelSlot);

    auto child = std::make_shared<UIBorder>("Child");
    child->setColor({1.0f, 1.0f, 1.0f, 1.0f});
    panel->addDetachedChild(child, [](UIElement&, UISlot& slot) {
        if (auto* box = dynamic_cast<UIBoxSlot*>(&slot)) {
            box->setPreferredSize({300.0f, 100.0f});
        }
    });

    const auto findFill = [](const UIFrameSnapshot& snapshot, glm::vec2 size) {
        return std::find_if(snapshot.items.begin(), snapshot.items.end(), [size](const UIFrameDrawItem& it) {
            return it.kind == UIFrameDrawItem::EKind::Sprite && it.size == size;
        });
    };

    // --- opacity: the child's item inherits the parent overlay ---
    panel->setRenderOpacity(0.5f);
    UIFrameSnapshot snapshot = tree.buildSnapshot({});
    auto childItem = findFill(snapshot, glm::vec2(300.0f, 100.0f));
    ASSERT_NE(childItem, snapshot.items.end());
    EXPECT_FLOAT_EQ(childItem->color.a, 0.5f);

    // --- translation: paint-only offset moves the child with its parent ---
    panel->setRenderOpacity(1.0f);
    panel->setRenderTranslation({40.0f, 20.0f});
    snapshot  = tree.buildSnapshot({});
    childItem = findFill(snapshot, glm::vec2(300.0f, 100.0f));
    ASSERT_NE(childItem, snapshot.items.end());
    // The child sits at the container origin (20,20), so the overlay offset
    // moves it to 20+40, 20+20.
    EXPECT_EQ(childItem->pos, glm::vec2(60.0f, 40.0f));

    // --- scale around the container's center pivot (100,50) ---
    panel->setRenderTranslation({0.0f, 0.0f});
    panel->setRenderScale({0.5f, 0.5f});
    snapshot  = tree.buildSnapshot({});
    childItem = findFill(snapshot, glm::vec2(150.0f, 50.0f)); // 300,100 * 0.5
    ASSERT_NE(childItem, snapshot.items.end());
    // Pivot = container center (120,70): 120,70 + (child pos - 120,70) * 0.5.
    EXPECT_EQ(childItem->pos, glm::vec2(70.0f, 45.0f));

    // --- tint: RGBA multiplier reaches the child ---
    panel->setRenderScale({1.0f, 1.0f});
    panel->setRenderTint({1.0f, 0.0f, 0.0f, 1.0f});
    snapshot  = tree.buildSnapshot({});
    childItem = findFill(snapshot, glm::vec2(300.0f, 100.0f));
    ASSERT_NE(childItem, snapshot.items.end());
    EXPECT_EQ(childItem->color, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
}

// Deterministic cost model for overlay-driven animation (counters, not wall
// time): a leaf overlay animation repaints exactly the animating widget, while
// a subtree-wide overlay repaints the whole subtree. That ratio is the reason
// HUD motion should animate leaves (button / knob / card) and that page-scale
// transitions are the expensive case - see .agent/plan/gui-animation/plan.md.
TEST(GuiAnimationTest, OverlayAnimationCostModelIsLeafVsSubtree)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       root = std::make_shared<UIContainer>("Root");
    FCanvasSlotArgs rootSlot;
    rootSlot.offset    = {0.0f, 0.0f};
    rootSlot.fixedSize = {400.0f, 300.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), root, rootSlot);

    std::vector<std::shared_ptr<UIBorder>> leaves;
    for (int i = 0; i < 6; ++i) {
        auto leaf = std::make_shared<UIBorder>("Leaf");
        leaf->setColor({1.0f, 1.0f, 1.0f, 1.0f});
        root->addDetachedChild(leaf, [](UIElement&, UISlot& slot) {
            if (auto* box = dynamic_cast<UIBoxSlot*>(&slot)) {
                box->setPreferredSize({40.0f, 20.0f});
            }
        });
        leaves.push_back(leaf);
    }
    (void)tree.buildSnapshot({});

    // Leaf overlay: exactly one rebuilt widget.
    leaves[2]->setRenderOpacity(0.5f);
    (void)tree.buildSnapshot({});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 1u);

    // Subtree overlay: the whole subtree repaints (root + 6 leaves).
    (void)tree.buildSnapshot({});
    root->setRenderOpacity(0.5f);
    (void)tree.buildSnapshot({});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 7u);
}

// A track the owner cannot expose is skipped (warned once) without disabling
// the tracks that do resolve - the descriptor cache must not collapse the
// whole tween when one id is unknown.
TEST(GuiAnimationTest, UnknownTrackIsSkippedWithoutBreakingResolvedTracks)
{
    WidgetTree tree({.width = 400, .height = 200});
    auto       card  = std::make_shared<UIBorder>("Card");
    card->setColor({1.0f, 1.0f, 1.0f, 1.0f});
    FCanvasSlotArgs slot;
    slot.fixedSize = {80.0f, 40.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), card, slot);

    auto tween = std::make_shared<UITweenBehavior>();
    tween->addFloatTrack("notAnimatable", 0.0f, 1.0f, EUIAnimEase::Linear);
    tween->addFloatTrack("opacity", 0.0f, 1.0f, EUIAnimEase::Linear);
    tween->setDuration(1.0f);
    card->addBehavior(tween);

    tween->play();
    EXPECT_FLOAT_EQ(card->getRenderOpacity(), 0.0f);
    tween->tick(*card, 0.5f);
    EXPECT_FLOAT_EQ(card->getRenderOpacity(), 0.5f);
}

// Retargetable playback: a two-state consumer (switch knob, hover feedback)
// must be able to reverse mid-flight without snapping back to an endpoint.
TEST(GuiAnimationTest, PlayTowardRetargetsWithoutSnapping)
{
    WidgetTree tree({.width = 400, .height = 200});
    auto       card = std::make_shared<UIBorder>("Card");
    card->setColor({1.0f, 1.0f, 1.0f, 1.0f});
    FCanvasSlotArgs slot;
    slot.fixedSize = {80.0f, 40.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), card, slot);

    auto tween = animate(*card, 1.0f);
    tween->fade(0.0f, 1.0f, EUIAnimEase::Linear).play();
    tween->tick(*card, 0.5f);
    EXPECT_FLOAT_EQ(card->getRenderOpacity(), 0.5f);

    // Reverse from the current position: the value keeps falling from 0.5.
    tween->playToward(EUIAnimDirection::Backward);
    EXPECT_TRUE(card->wantsTick());
    tween->tick(*card, 0.2f);
    EXPECT_FLOAT_EQ(card->getRenderOpacity(), 0.3f);

    // Forward again from 0.3 (no jump to 0).
    tween->playToward(EUIAnimDirection::Forward);
    tween->tick(*card, 0.2f);
    EXPECT_FLOAT_EQ(card->getRenderOpacity(), 0.5f);

    // Reaching the endpoint settles instead of spinning.
    tween->playToward(EUIAnimDirection::Forward);
    tween->tick(*card, 5.0f);
    EXPECT_FLOAT_EQ(card->getRenderOpacity(), 1.0f);
    EXPECT_FALSE(card->wantsTick());

    // Already at the target: no-op, not a restart.
    tween->playToward(EUIAnimDirection::Forward);
    EXPECT_FALSE(tween->isPlaying());
    EXPECT_FLOAT_EQ(card->getRenderOpacity(), 1.0f);
}

TEST(GuiAnimationTest, SetLerpNowAppliesInitialStateWithoutAnimating)
{
    WidgetTree tree({.width = 400, .height = 200});
    auto       card = std::make_shared<UIBorder>("Card");
    card->setColor({1.0f, 1.0f, 1.0f, 1.0f});
    FCanvasSlotArgs slot;
    slot.fixedSize = {80.0f, 40.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), card, slot);

    auto tween = animate(*card, 0.5f);
    tween->fade(0.0f, 1.0f, EUIAnimEase::Linear).setLerpNow(1.0f);
    EXPECT_FLOAT_EQ(card->getRenderOpacity(), 1.0f); // built already "on"
    EXPECT_FALSE(tween->isPlaying());
    EXPECT_FALSE(card->wantsTick());
}

// The typed handle + sugar methods are the authoring surface: same runtime
// path as the string API, but the value domain is checked at compile time.
TEST(GuiAnimationTest, TypedHandlesAndSugarDriveTheSameTracks)
{
    WidgetTree tree({.width = 400, .height = 200});
    auto       card = std::make_shared<UIBorder>("Card");
    card->setColor({1.0f, 1.0f, 1.0f, 1.0f});
    FCanvasSlotArgs slot;
    slot.fixedSize = {80.0f, 40.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), card, slot);

    auto tween = animate(*card, 1.0f);
    tween->track(ya::ui::anim::opacity, 0.25f, 0.75f, EUIAnimEase::Linear)
        .track(ya::ui::anim::scale, glm::vec2(1.0f, 1.0f), glm::vec2(2.0f, 2.0f), EUIAnimEase::Linear)
        .track(ya::ui::anim::translation, glm::vec2(0.0f, 0.0f), glm::vec2(10.0f, 20.0f), EUIAnimEase::Linear)
        .track(ya::ui::anim::tint, glm::vec4(1.0f), glm::vec4(0.5f), EUIAnimEase::Linear)
        .setLerpNow(0.5f);

    EXPECT_FLOAT_EQ(card->getRenderOpacity(), 0.5f);
    EXPECT_EQ(card->getRenderScale(), glm::vec2(1.5f, 1.5f));
    EXPECT_EQ(card->getRenderTranslation(), glm::vec2(5.0f, 10.0f));
    EXPECT_EQ(card->getRenderTint(), glm::vec4(0.75f));
    EXPECT_EQ(tween->getTrackCount(), 4u);

    // The sugar methods are the same three track kinds.
    auto sugar = animate(*card, 1.0f);
    sugar->fade(1.0f, 0.0f).scale({1.0f, 1.0f}, {0.5f, 0.5f}).slide({0.0f, 0.0f}, {4.0f, 0.0f}).tint(glm::vec4(0.0f), glm::vec4(1.0f));
    EXPECT_EQ(sugar->getTrackCount(), 4u);
}

TEST(GuiAnimationTest, AnimateHelperAttachesTheBehaviour)
{
    WidgetTree tree({.width = 400, .height = 200});
    auto       card = std::make_shared<UIBorder>("Card");
    FCanvasSlotArgs slot;
    slot.fixedSize = {80.0f, 40.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), card, slot);

    // Fire and forget: the widget owns the behaviour, so dropping the handle
    // does not cancel the animation.
    animate(*card, 0.3f)->fade(0.0f, 1.0f, EUIAnimEase::OutCubic).play();
    EXPECT_TRUE(card->wantsTick());
    tree.tick(0.15f);
    EXPECT_GT(card->getRenderOpacity(), 0.0f);
    tree.tick(0.15f);
    EXPECT_FLOAT_EQ(card->getRenderOpacity(), 1.0f);
    EXPECT_FALSE(card->wantsTick());
}

TEST(GuiAnimationTest, ClockResolvesEndpointsAndLooping)
{
    UIAnimClock clock;
    clock.setDuration(1.0f);
    clock.play();
    EXPECT_TRUE(clock.isPlaying());
    EXPECT_TRUE(clock.tick(0.4f));
    EXPECT_FLOAT_EQ(clock.getLerp(), 0.4f);
    // The tick that reaches the end stops the clock (it finished IN that
    // tick), so the completing tick itself returns false.
    EXPECT_FALSE(clock.tick(0.7f)); // overshoot clamps to the end
    EXPECT_FALSE(clock.isPlaying());
    EXPECT_TRUE(clock.hasFinished());
    EXPECT_FLOAT_EQ(clock.getLerp(), 1.0f);

    clock.playReverse();
    EXPECT_TRUE(clock.isPlaying());
    EXPECT_TRUE(clock.tick(0.25f));
    EXPECT_FLOAT_EQ(clock.getLerp(), 0.75f);
    EXPECT_FALSE(clock.tick(1.0f));
    EXPECT_FALSE(clock.isPlaying());
    EXPECT_TRUE(clock.hasFinished());
    EXPECT_FLOAT_EQ(clock.getLerp(), 0.0f);

    clock.setLoop(true);
    clock.play();
    EXPECT_TRUE(clock.tick(1.1f)); // wraps instead of finishing
    EXPECT_TRUE(clock.isPlaying());
    EXPECT_FALSE(clock.hasFinished());
    EXPECT_NEAR(clock.getLerp(), 0.1f, 1e-4f);
    clock.stop();
    EXPECT_FALSE(clock.isPlaying());
    EXPECT_FLOAT_EQ(clock.getLerp(), 0.0f);
}

TEST(GuiAnimationTest, EasingAndLerpStayInDomain)
{
    EXPECT_FLOAT_EQ(evaluateEase(EUIAnimEase::Linear, 0.5f), 0.5f);
    EXPECT_FLOAT_EQ(evaluateEase(EUIAnimEase::OutCubic, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(evaluateEase(EUIAnimEase::OutCubic, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(evaluateEase(EUIAnimEase::InQuad, 0.5f), 0.25f);
    EXPECT_FLOAT_EQ(evaluateEase(EUIAnimEase::OutCubic, 0.5f), 0.875f);
    EXPECT_FLOAT_EQ(evaluateEase(EUIAnimEase::OutCubic, 2.0f), 1.0f); // clamped

    const FUIAnimValue mixed = lerpAnimValue(FUIAnimValue::fromFloat(0.0f),
                                             FUIAnimValue::fromFloat(2.0f),
                                             0.25f);
    EXPECT_FLOAT_EQ(mixed.asFloat(), 0.5f);
    // Mismatched domains resolve to the source value instead of changing type.
    const FUIAnimValue guard = lerpAnimValue(FUIAnimValue::fromVec2({1.0f, 2.0f}),
                                             FUIAnimValue::fromFloat(9.0f),
                                             0.5f);
    EXPECT_EQ(guard.type, EUIAnimValueType::Vec2);
    EXPECT_EQ(guard.asVec2(), glm::vec2(1.0f, 2.0f));
}

} // namespace ya
