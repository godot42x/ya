#pragma once

#include "Gameplay/Linkage/LinkageRule.h"

namespace ya
{

class LinkageFramework;
struct Scene;

/**
 * @brief Material component <-> RenderComponent topology rule (render adapter).
 *
 * Keeps the presence of a RenderComponent in sync with material components on
 * the same entity. This is render-side topology logic; the linkage framework
 * stays generic.
 */
struct MaterialRenderLinkageRule : public ILinkageRule
{
    explicit MaterialRenderLinkageRule(LinkageFramework* framework);

    void onSceneInit(Scene* scene) override;
    void onComponentRemoved(entt::registry& reg, entt::entity entity, ya::type_index_t type) override;

  private:
    void onMaterialEvent(entt::registry& reg, entt::entity entity);

    LinkageFramework* _framework = nullptr;
};

} // namespace ya
