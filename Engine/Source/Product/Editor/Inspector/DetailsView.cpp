#include "Product/Editor/Inspector/DetailsViewInternal.h"
#include "Framework/Game/Gameplay/ECS/Component/TransformComponent.h"

namespace ya
{

DetailsView::DetailsView(EditorLayer* owner)
    : _owner(owner)
{
}

void DetailsView::testNewRenderInterface(Entity& entity)
{
    if (auto* transform = entity.getComponent<TransformComponent>()) {
        ya::RenderContext ctx;
        ctx.beginInstance(transform);
        ya::renderReflectedType("Transform", ya::type_index_v<TransformComponent>, transform, ctx, 0);

        if (ctx.isModified("_position")) {
            YA_CORE_INFO("Position was modified!");
        }
        if (ctx.isModifiedPrefix("_rotation")) {
            YA_CORE_INFO("Some rotation property was modified!");
        }

        if (ctx.hasModifications()) {
            for (const auto& mod : ctx.modifications) {
                YA_CORE_INFO("Property {} was modified (path: {})", mod.propPath, mod.propId.id);
            }
        }
    }
}

} // namespace ya
