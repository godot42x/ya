#pragma once

#include <algorithm>

#include "Core/Base.h"
#include "Core/Event.h"
#include "UIBase.h"

namespace ya
{



struct UIElement
{
    // todo: multi window
    std::vector<std::shared_ptr<UIElement>> _children;
    UIElement                              *_parent = nullptr;


  public:
    UIElement()                             = default;
    UIElement(const UIElement &)            = default;
    UIElement(UIElement &&)                 = delete;
    UIElement &operator=(const UIElement &) = default;
    UIElement &operator=(UIElement &&)      = delete;



    virtual ~UIElement() = default;

    virtual void render(UIRenderContext &ctx, layer_idx_t layerId)
    {
        for (auto &child : _children)
        {
            child->render(ctx, layerId);
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
        child->setParent(this);
    }
    void removeChild(std::shared_ptr<UIElement> child)
    {
        _children.erase(std::remove(_children.begin(), _children.end(), child), _children.end());
        child->setParent(nullptr);
    }

    UIElement *getParent() const { return _parent; }
    void       setParent(UIElement *parent) { _parent = parent; }
};

} // namespace ya