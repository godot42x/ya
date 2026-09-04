// Phase 9D editor input/locale contract: DPI mapping, CJK/IME code-point
// editing, and WidgetTree clipboard used by focused text fields.
//
// The target links ONLY the GUI closure (no SDL clipboard).

#include "Core/Event.h"
#include "Core/KeyCode.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Render/Resources/FontManager.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace ya
{

namespace
{

WidgetEventContext pointAt(float x, float y)
{
    WidgetEventContext ctx;
    ctx.logicalPoint = {x, y};
    return ctx;
}

KeyPressedEvent makeKeyPress(EKey::T key, uint32_t mod = 0, bool bRepeat = false)
{
    KeyPressedEvent ev;
    ev._keyCode = key;
    ev._mod     = mod;
    ev.bRepeat  = bRepeat;
    return ev;
}

uint32_t primaryMod()
{
#if defined(__APPLE__)
    return EKeyMod::LMeta;
#else
    return EKeyMod::LCtrl;
#endif
}

std::shared_ptr<Font> registerCjkFont(float fontSize = 16.0f, float latinAdvance = 8.0f, float cjkAdvance = 16.0f)
{
    auto font        = std::make_shared<Font>();
    font->fontSize   = fontSize;
    font->lineHeight = fontSize * 1.25f;
    font->ascent     = fontSize;
    font->descent    = fontSize * 0.25f;
    font->renderMode = EFontRenderMode::Bitmap;
    for (uint32_t cp = 32; cp < 127; ++cp) {
        Character ch;
        ch.size       = {static_cast<int>(latinAdvance), static_cast<int>(fontSize)};
        ch.bearing    = {0, 0};
        ch.advance    = {latinAdvance, 0.0f};
        ch.designSize = static_cast<uint32_t>(fontSize);
        ch.bInAtlas   = true;
        font->characters[cp] = ch;
    }
    const uint32_t cjkPoints[] = {0x4F60u, 0x597Du}; // 你 好
    for (uint32_t cp : cjkPoints) {
        Character ch;
        ch.size       = {static_cast<int>(cjkAdvance), static_cast<int>(fontSize)};
        ch.bearing    = {0, 0};
        ch.advance    = {cjkAdvance, 0.0f};
        ch.designSize = static_cast<uint32_t>(fontSize);
        ch.bInAtlas   = true;
        font->characters[cp] = ch;
    }
    FontManager::get()->registerFont(DEFAULT_RUNTIME_FONT_NAME, static_cast<uint32_t>(fontSize), font);
    return font;
}

} // namespace

TEST(EditorInputContractTest, DpiScaleFoldsIntoSnapshotLikeUiScale)
{
    WidgetTree tree({.width = 320, .height = 200});
    auto       panel = std::make_shared<UIPanel>("DpiPanel");
    FCanvasSlotArgs slot;
    slot.offset    = {10.0f, 10.0f};
    slot.fixedSize = {100.0f, 50.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, slot);

    const UIFrameSnapshot unscaled = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_EQ(unscaled.items.size(), 1u);
    EXPECT_EQ(unscaled.items[0].pos, glm::vec2(10.0f, 10.0f));
    EXPECT_EQ(unscaled.items[0].size, glm::vec2(100.0f, 50.0f));

    tree.setDpiScale(2.0f);
    const UIFrameSnapshot dpi = tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getLastInvalidationReason(), EUIInvalidationReason::BuildContextChanged);
    ASSERT_EQ(dpi.items.size(), 1u);
    EXPECT_EQ(dpi.items[0].pos, glm::vec2(20.0f, 20.0f));
    EXPECT_EQ(dpi.items[0].size, glm::vec2(200.0f, 100.0f));

    const UIFrameSnapshot both = tree.buildSnapshot(UIFrameBuildContext{.uiScale = {2.0f, 2.0f}});
    ASSERT_EQ(both.items.size(), 1u);
    EXPECT_EQ(both.items[0].pos, glm::vec2(40.0f, 40.0f));
    EXPECT_EQ(both.items[0].size, glm::vec2(400.0f, 200.0f));
}

TEST(EditorInputContractTest, TextFieldImeCommitEditsByCodePoint)
{
    registerCjkFont();

    WidgetTree tree({.width = 400, .height = 80});
    auto       field = std::make_shared<UITextField>("Ime");
    FCanvasSlotArgs slot;
    slot.fixedSize = {240.0f, 28.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), field, slot);
    tree.layout();
    tree.setFocus(field.get());

    const auto at = pointAt(0.0f, 0.0f);
    EXPECT_EQ(tree.dispatchEvent(KeyTypedEvent("你好"), at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(field->_text, "你好");

    auto label = std::make_shared<UIText>("Measure");
    label->setText("你好");
    label->_fontSize = 16;
    EXPECT_FLOAT_EQ(label->computeDesiredSize().x, 32.0f);

    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Backspace), at),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(field->_text, "你");

    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Home), at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Delete), at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(field->_text, "");
}

TEST(EditorInputContractTest, TextFieldClipboardCopyCutPasteStripsNewlines)
{
    WidgetTree tree({.width = 400, .height = 80});
    auto       field = std::make_shared<UITextField>("Clip");
    FCanvasSlotArgs slot;
    slot.fixedSize = {240.0f, 28.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), field, slot);
    tree.layout();
    tree.setFocus(field.get());

    const auto at = pointAt(0.0f, 0.0f);
    EXPECT_EQ(tree.dispatchEvent(KeyTypedEvent("hello"), at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::K_C, primaryMod()), at),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.getClipboardText(), "hello");
    EXPECT_EQ(field->_text, "hello");

    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::K_X, primaryMod()), at),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(field->_text, "");
    EXPECT_EQ(tree.getClipboardText(), "hello");

    tree.setClipboardText("a\nb\tc");
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::K_V, primaryMod()), at),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(field->_text, "abc");

    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::K_V, primaryMod(), /*bRepeat=*/true), at),
              EWidgetRouteResult::NotHandled);
}

TEST(EditorInputContractTest, ClipboardHooksReplaceInMemoryBuffer)
{
    WidgetTree tree({.width = 200, .height = 80});
    std::string osClipboard = "from-os";
    tree.setClipboardHooks([&]() { return osClipboard; },
                           [&](const std::string& text) { osClipboard = text; });

    auto field = std::make_shared<UITextField>("Hook");
    FCanvasSlotArgs slot;
    slot.fixedSize = {160.0f, 28.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), field, slot);
    tree.layout();
    tree.setFocus(field.get());

    const auto at = pointAt(0.0f, 0.0f);
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::K_V, primaryMod()), at),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(field->_text, "from-os");

    EXPECT_EQ(tree.dispatchEvent(KeyTypedEvent("X"), at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::K_C, primaryMod()), at),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(osClipboard, "from-osX");
}

} // namespace ya
