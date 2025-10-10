#pragma once

#include <algorithm>

#include "Core/Base.h"
#include "Core/Event.h"
#include "UIBase.h"

namespace ya
{



struct UIElement
{
    std::vector<std::shared_ptr<UIElement>> _children;


  public:
    UIElement()                             = default;
    UIElement(const UIElement &)            = default;
    UIElement(UIElement &&)                 = delete;
    UIElement &operator=(const UIElement &) = default;
    UIElement &operator=(UIElement &&)      = delete;



    virtual ~UIElement() = default;

    virtual void render(UIRenderContext &ctx)
    {
        for (auto &child : _children)
        {
            child->render(ctx);
        }
    }
    virtual void update(float dt)
    {
        for (auto &child : _children)
        {
            child->update(dt);
        }
    }
    virtual int handleEvent(const Event &event, UIAppCtx &ctx)
    {
        for (auto &child : _children)
        {
            if (child->handleEvent(event, ctx)) {
                return 1;
            }
        }
        return 0;
    }

    void addChild(std::shared_ptr<UIElement> child)
    {
        _children.push_back(child);
    }
    void removeChild(std::shared_ptr<UIElement> child)
    {
        _children.erase(std::remove(_children.begin(), _children.end(), child), _children.end());
    }
};

} // namespace ya