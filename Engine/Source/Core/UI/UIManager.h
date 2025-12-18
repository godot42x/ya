#pragma once

#include "Core/Base.h"
#include "Render/UIRender.h"
#include "UIBase.h"
#include "UICanvas.h"
#include "UIElement.h"


namespace ya
{

struct UIManager
{

    stdptr<UICanvas> _rootCanvas;


    UIManager()
    {
        _rootCanvas = makeShared<UICanvas>();
    }
    UIManager(UIManager const &)            = delete;
    UIManager &operator=(UIManager const &) = delete;

    static UIManager *get();


    void render();

    void onEvent(const Event &event, UIAppCtx &ctx);


    struct PopupWrapper
    {
        stdptr<UIElement> popup;
        bool              bOpen  = true;
        bool              bModal = true;


        void show()
        {
            if (UIMeta::get()->isBaseOf(UIElement::getStaticType(), popup->getStaticType())) {
                // popup->update(0.0f);
                // Render2D::beginUIOverlay();
                // popup->render(UIRenderContext{glm::vec2(0.0f), glm::vec2(0.0f)}, 1000);
                // Render2D::endUIOverlay();
            }
        }
    };

    void addPopup(std::shared_ptr<UIElement> popup);
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
                                     return false;
                                     //  return FUIHelper::isUIPendingKill(el);
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