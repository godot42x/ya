
#pragma once

#include "Core/Base.h"
#include "Core/UI/UIElement.h"
#include "Render/2D/Render2D.h"

namespace ya
{

struct UITextBlock : public UIElement
{
    UI_TYPE(UITextBlock, UIElement);

    // std::wstring _text = L"你好";
    std::string          _text     = "Hello, World!";
    glm::vec2            _position = {0.0f, 0.0f};
    glm::vec2            _size     = {0.0f, 0.0f}; // Container size for alignment (0 = no container)
    glm::vec4            _color    = {1.0f, 1.0f, 1.0f, 1.0f};
    Font                *_font     = nullptr;
    EHorizontalAlignment _hAlign   = HAlign_Left;
    EVerticalAlignment   _vAlign   = VAlign_Top;

    UITextBlock()                    = default;
    UITextBlock(const UITextBlock &) = default;

    void render(UIRenderContext &ctx, layer_idx_t layerId) override
    {
        if (!_font) {
            Super::render(ctx, layerId);
            return;
        }

        glm::vec2 textPos = ctx.pos + _position;

        // Calculate text alignment offset
        if (_size.x > 0 || _size.y > 0) {
            float textWidth  = _font->measureText(_text);
            float textHeight = _font->ascent - _font->descent;

            // Horizontal alignment
            if (_size.x > 0) {
                switch (_hAlign) {
                case HAlign_Center:
                    textPos.x += (_size.x - textWidth) * 0.5f;
                    break;
                case HAlign_Right:
                    textPos.x += _size.x - textWidth;
                    break;
                case HAlign_Left:
                default:
                    break;
                }
            }

            // Vertical alignment
            if (_size.y > 0) {
                switch (_vAlign) {
                case VAlign_Center:
                    textPos.y += (_size.y - textHeight) * 0.5f;
                    break;
                case VAlign_Bottom:
                    textPos.y += _size.y - textHeight;
                    break;
                case VAlign_Top:
                default:
                    break;
                }
            }
        }

        glm::vec3 pivot = glm::vec3(textPos, (float)layerId / 100.f);
        Render2D::makeText(_text, pivot, _color, _font);
        layerId += 1;
        Super::render(ctx, layerId);
    }

  public:

    const std::string &getText() const { return _text; }
    void               setText(const std::string &text) { _text = text; }
};
} // namespace ya