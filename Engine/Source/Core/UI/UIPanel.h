#pragma once

#include "Render/Render2D.h"
#include "UElement.h"


namespace ya
{



struct UIPanel : public UIElement
{
    using Super = UIElement;

    FUIColor  _color    = {0.8f, 0.8f, 0.8f, 1.0f};
    glm::vec2 _position = {0.0f, 0.0f};
    glm::vec2 _size     = {100.0f, 100.0f};


  public:

    void render(UIRenderContext &ctx) override
    {
        Render2D::makeSprite(glm::vec3(_position, (float)ctx.layerId / 100.f), _size, nullptr, _color.asVec4());
        ctx.layerId += 1;
        Super::render(ctx);
    }

    void update(float dt) override
    {
        Super::update(dt);
    }

    int handleEvent(const Event &event, UIAppCtx &ctx) override
    {
        Super::handleEvent(event, ctx);

        return 0;
    }
};

} // namespace ya