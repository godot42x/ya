#include "UIManager.h"
#include "Core/Debug/Instrumentor.h"

namespace ya

{


UIManager *UIManager::get()
{
    static UIManager instance;
    return &instance;
}

void UIManager::render()
{
    YA_PROFILE_FUNCTION()
    UIRenderContext ctx{};
    _rootCanvas->render(ctx, 0);
}

void UIManager::onEvent(const Event &event, UIAppCtx &ctx)
{
    _rootCanvas->handleEvent(event, ctx);
}

void UIManager::addPopup(std::shared_ptr<UIElement> popup)
{
    _rootCanvas->addChild(popup);
}

UIElementRegistry *UIElementRegistry::get()
{
    static UIElementRegistry instance;
    return &instance;
}

} // namespace ya
