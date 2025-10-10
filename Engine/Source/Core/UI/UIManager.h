#pragma once

#include "Core/Base.h"
#include "Render/UIRender.h"
#include "UElement.h"
#include "UIBase.h"


namespace ya
{

struct UIManager
{

    UIElement _rootElement;

    static void init() {}
    static void shutdown() {}

    static UIManager *get();

    void render()
    {
        UIRenderContext ctx{
            .layerId = 0,
        };
        _rootElement.render(ctx);
    }

    void onEvent(Event &event, UIAppCtx &ctx)
    {
        _rootElement.handleEvent(event, ctx);
    }
};

struct UIElementRegistry
{
    std::vector<UIElement *> _allElements;


    static UIElementRegistry *get();

    void registerElement(UIElement *element)
    {
        _allElements.push_back(element);
    }

    void gc()
    {
        auto it = std::remove_if(_allElements.begin(),
                                 _allElements.end(),
                                 [](UIElement *el) {
                                     return FUIHelper::isUIPendingKill(el);
                                 });
        // destruct, release resources...
        for (; it != _allElements.end(); ++it)
        {
            // delete *it;
        }
        _allElements.erase(it, _allElements.end());
    }
};


} // namespace ya