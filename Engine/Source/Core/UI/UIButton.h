#pragma once

#include "Render/2D/Render2D.h"
#include "UIElement.h"



namespace ya
{

struct UIButton : public UIElement
{
    UI_TYPE(UIButton, UIElement);

    FUIColor _normalColor  = FUIColor(0.8f, 0.8f, 0.8f, 1.0f);
    FUIColor _hoveredColor = FUIColor(0.6f, 0.6f, 0.6f, 1.0f);
    FUIColor _pressedColor = FUIColor(0.4f, 0.4f, 0.4f, 1.0f);

    glm::vec2 _position = {0.0f, 0.0f};
    glm::vec2 _size     = {100.0f, 50.0f};

    enum EState
    {
        Normal,
        Hovered,
        Pressed
    } _state;


    void render(UIRenderContext &ctx, layer_idx_t layerId) override
    {


        FUIColor *color = nullptr;
        switch (_state)
        {
        case Normal:
            color = &_normalColor;
            break;
        case Hovered:
            color = &_hoveredColor;
            break;
        case Pressed:
            color = &_pressedColor;
            break;
        default:
            color = &_normalColor;
            break;
        }
        Render2D::makeSprite(glm::vec3(_position, (float)layerId / 100.f),
                             _size,
                             nullptr,
                             color->asVec4());
        ctx.pos += _position;
        layerId += 1;
        Super::render(ctx, layerId);
    }

    int handleEvent(const Event &event, UIAppCtx &ctx) override
    {
        glm::vec2 &pos = _position;

        switch (event.getEventType()) {
        case ya::EEvent::MouseButtonPressed:
        {
            if (FUIHelper::isPointInRect(ctx.lastMousePos, pos, _size))
            {
                _state = Pressed;
                return 1;
            }
        } break;
        case EEvent::MouseButtonReleased:
        {
            if (_state == Pressed)
            {
                if (FUIHelper::isPointInRect(ctx.lastMousePos, pos, _size)) {
                    _state = Hovered;
                }
                else {
                    _state = Normal;
                }
                return 1;
            }
        } break;
        case EEvent::MouseMoved:
        {
            if (FUIHelper::isPointInRect(ctx.lastMousePos, pos, _size))
            {
                _state = Hovered;
            }
            else
            {
                _state = Normal;
            }
        } break;
        default:
            break;
        }

        return 0;
    };
};

} // namespace ya