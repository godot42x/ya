#include "../WorkbenchDemoPages.h"
#include "DemoPageCommon.h"

#include "GUI/Declarative/Build.h"

#include <format>

namespace guiworkbench
{

void buildTextDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                   const std::function<void(const std::string&)>& log)
{
    auto header = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kHeaderColor);
    };

    auto form = ya::ui::column("TextForm").setPadding({16.0f, 12.0f}).setSpacing(12.0f);
    form.child(header("TextTitle", "Text — wrap, alignment, theme style keys"));
    form.child(ya::ui::text("WrappedText")
                   .setText("This paragraph demonstrates automatic text wrapping. Long content breaks onto "
                            "multiple lines instead of overflowing its box, matching the editor's TextWrapped "
                            "behavior. CJK text also wraps: 中文换行测试中文换行测试。")
                   .setFontSize(13)
                   .setWrap(true)
                   .setMaxWrapWidth(360.0f),
               ya::FBoxSlotArgs{.crossAlignment = ya::EUIBoxSlotCrossAlignment::Start});
    form.child(header("AlignTitle", "Alignment"));
    form.child(ya::ui::row("TextAlignRow")
                   .setSpacing(8.0f)
                   .child(ya::ui::panel("AlignLeft")
                           .setColor({0.16f, 0.18f, 0.22f, 1.0f})
                           .child(ya::ui::text("AlignLeft_Body")
                                      .setText("left")
                                      .setFontSize(13)
                                      .setHAlign(ya::EWidgetAlignH::Left)
                                      .setVAlign(ya::EWidgetAlignV::Center),
                                  ya::ui::canvasSlot().fill()),
                          ya::ui::boxSlot().preferredSize({140.0f, 32.0f}))
                   .child(ya::ui::panel("AlignCenter")
                           .setColor({0.16f, 0.18f, 0.22f, 1.0f})
                           .child(ya::ui::text("AlignCenter_Body")
                                      .setText("center")
                                      .setFontSize(13)
                                      .setHAlign(ya::EWidgetAlignH::Center)
                                      .setVAlign(ya::EWidgetAlignV::Center),
                                  ya::ui::canvasSlot().fill()),
                          ya::ui::boxSlot().preferredSize({140.0f, 32.0f}))
                   .child(ya::ui::panel("AlignRight")
                           .setColor({0.16f, 0.18f, 0.22f, 1.0f})
                           .child(ya::ui::text("AlignRight_Body")
                                      .setText("right")
                                      .setFontSize(13)
                                      .setHAlign(ya::EWidgetAlignH::Right)
                                      .setVAlign(ya::EWidgetAlignV::Center),
                                  ya::ui::canvasSlot().fill()),
                          ya::ui::boxSlot().preferredSize({140.0f, 32.0f})));
    form.child(header("StyleKeyTitle", "Theme keys (no authored color)"));
    form.child(ya::ui::text("TextHeaderKey").setText("text.header").setStyleKey("text.header").setFontSize(13));
    form.child(ya::ui::text("TextMutedKey").setText("text.muted").setStyleKey("text.muted").setFontSize(13));
    form.child(ya::ui::text("TextDefaultKey").setText("text").setStyleKey("text").setFontSize(13));

    auto page = ya::ui::panel("TextDemo").setColor(kPanelColor).child(std::move(form), ya::ui::canvasSlot().fill());
    ya::ui::build(tree, parent, std::move(page), ya::ui::canvasSlot().fill());
    (void)state;
    (void)log;
}

void buildFontsDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                    const std::function<void(const std::string&)>& log)
{
    auto header = [](std::string key, const std::string& text, uint32_t fontSize = 13)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(fontSize).setColor(kHeaderColor);
    };
    auto body = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kTextColor);
    };

    auto form = ya::ui::column("FontsForm").setPadding({12.0f, 12.0f}).setSpacing(8.0f);
    form.child(header("UnicodeTitle",
                      "Fonts — CJK + emoji through the font stack (SDF fallback + color atlas)"));
    form.child(body("UnicodeZh", "简体中文：你好，世界！这是一个字体栈测试。"));
    form.child(body("UnicodeJa", "日本語：こんにちは、世界。"));
    form.child(body("UnicodeKo", "한국어：안녕하세요, 세계."));
    form.child(body("UnicodeEmoji", "Emoji: 😀 🎉 🚀 ❤️ 🍕 ✅"));
    form.child(body("UnicodeMixed", "Mixed: 中文 + Latin + 123 + emoji 🎈"));
    form.child(header("UnicodeLargeTitle", "Large CJK title", 20));
    form.child(body("UnicodeLargeBody", "大字号中文标题：字体渲染验收"));

    form.child(header("ChineseTitle", "中文测试 — 单一 CJK fallback 字重一致性验收"));
    form.child(body("ChineseHint",
                    "本页只渲染中文，用于核对相邻字亮度/粗细是否一致、边缘是否发虚。"
                    "若字体栈把中文分散到多个 fallback 字体，相邻字会忽明忽暗、边缘发虚。"));
    struct Row
    {
        uint32_t    px;
        const char* tag;
    };
    static constexpr Row kRows[] = {
        {.px = 9, .tag = "小字"},
        {.px = 11, .tag = "小字"},
        {.px = 13, .tag = "正文"},
        {.px = 16, .tag = "中字"},
        {.px = 20, .tag = "大字"},
        {.px = 24, .tag = "大字"},
        {.px = 32, .tag = "特大"},
        {.px = 40, .tag = "特大"},
    };
    for (const auto& r : kRows) {
        form.child(header(std::format("CjkRow{}", r.px),
                          std::format("{} {}px：字体渲染验收测试中文连续文本", r.tag, r.px),
                          r.px));
    }
    form.child(header("ChineseParaTitle", "纯中文段落（连续文本）"));
    form.child(body("ChinesePara",
                    "渲染引擎字体子系统负责把缺失字形从中文备用字体解析出来，保证同一段中文来自"
                    "同一个字体面孔，从而相邻字符的笔画粗细与亮度保持一致，避免出现一个字亮一个字"
                    "暗、边缘发虚的问题。这是中文渲染质量的验收段落，请观察每个字的黑白对比是否均匀。"));
    form.child(header("ChinesePunctTitle", "标点与字混合（逗号句号叹号括号问号）"));
    form.child(header("ChinesePunct", "中文，中文。中文！（中文）中文？中文；中文：中文、中文——"));
    form.child(header("ChineseGridTitle", "逐字黑白对比验收（一字一格，便于发现忽明忽暗）"));
    form.child(header("ChineseGrid",
                      "日 本 语 言 学 中 文 字 体 测 试 标 题 验 收 简 体 繁 体 汉 字 笔 画 粗 细 亮 度 边 缘"));

    auto page = ya::ui::panel("FontsDemo")
                    .setColor(kPanelColor)
                    .child(ya::ui::scroll("FontsScroll").child(std::move(form)), ya::ui::canvasSlot().fill());
    ya::ui::build(tree, parent, std::move(page), ya::ui::canvasSlot().fill());
    state.statusText = "Fonts page built (CJK fallback + emoji)";
    (void)log;
}

} // namespace guiworkbench
