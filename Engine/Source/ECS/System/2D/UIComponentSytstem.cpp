#include "UIComponentSytstem.h"
#include "Core/App/App.h"
#include "ECS/Component/2D/UIComponent.h"

#include "Core/Math/ScreenUtil.h"
#include "Render/2D/Render2D.h"



namespace ya

{

void UIComponentSystem::onRender()
{
    auto app = App::get();
    auto rt  = app->_viewportRT;
    // auto rp    = rt->getRenderPass();
    auto scene = app->getSceneManager()->getActiveScene();


    auto &ctx      = rt->getFrameContext();
    auto  viewport = app->viewportRect;

    // 这里想要的是一个三维世界之中的2D平面，而不是屏幕二维的平面
    // 即可以旋转相机视角，来看到一张纸...
    // Render2D 内部的接口只能画在屏幕屏幕?
    // 需要一个 quad mesh + transform + texture + 光照？
    // 放入 PhongMaterialSystem 之中实现

    scene->getRegistry()
        .view<UIComponent, TransformComponent>()
        .each(
            [&](auto /*entity*/, UIComponent &uc, TransformComponent &tc) {
                auto mat = tc.getWorldMatrix();
                mat[1] *= -1.0f; // Flip Y for screen space
                Render2D::makeSprite(
                    mat,
                    uc.view.textureRef.getShared());
            });
}

} // namespace ya
