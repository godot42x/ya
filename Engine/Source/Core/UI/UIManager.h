#pragma once

#include "Core/Base.h"
#include "Render/UIRender.h"
#include "UIBase.h"
#include "UIElement.h"



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
        UIRenderContext ctx{};
        _rootElement.render(ctx, 0);
    }

    void onEvent(const Event &event, UIAppCtx &ctx)
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

struct UIFactory
{
    template <typename T, typename... Args>
    static std::shared_ptr<T> create(Args &&...args)
    {
        auto element = makeShared<T>(std::forward<Args>(args)...);
        UIElementRegistry::get()->registerElement(element.get());
        return element;
    }
};


} // namespace ya