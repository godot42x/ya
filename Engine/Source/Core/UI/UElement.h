#pragma once

#include "Core/Base.h"
#include "Core/Event.h"


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

    virtual void              render()                        = 0; // Abstract method for rendering the UI element
    virtual void              update(float dt)                = 0; // Abstract method for updating the UI element state
    virtual EventProcessState handleEvent(const Event &event) = 0; // Abstract method for handling events

    void addChild(std::shared_ptr<UIElement> child)
    {
        _children.push_back(child);
    }
};