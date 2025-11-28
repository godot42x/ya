
#pragma once

#include "Core/Base.h"
#include "Core/UI/UIElement.h"
#include "Render/2D/Render2D.h"

namespace ya
{

struct UITextBlock : public UIElement
{
    std::string text;
    float       fontSize = 16.0f;
    glm::vec4   color{1.0f, 1.0f, 1.0f, 1.0f};
    bool        bAutoWrap = true;

    void render(UIRenderContext &ctx, layer_idx_t layerId) override
    {
        // Render2D::drawText(
        //     text,
        //     _position,
        //     fontSize,
        //     color,
        //     bAutoWrap,
        //     _size.x);
    }
};
} // namespace ya