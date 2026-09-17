#pragma once

#include "ECS/Linkage/LinkageRule.h"

#include <unordered_set>

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
struct YA_RENDER_ECS_ADAPTERS_API MaterialRenderLinkageRule : public ILinkageRule
{
    explicit MaterialRenderLinkageRule(LinkageFramework* framework);
    ~MaterialRenderLinkageRule() override;

    void onSceneInit(Scene* scene) override;
    void onComponentRemoved(entt::registry& reg, entt::entity entity, ya::type_index_t type) override;
    void onSceneUnload(Scene* scene) override;

  private:
    void onMaterialEvent(entt::registry& reg, entt::entity entity);
    void disconnectScene(entt::registry& registry);

    LinkageFramework* _framework = nullptr;
    /// Registries this rule connected entt signals to; used to disconnect on
    /// scene unload and when the rule is destroyed (framework shutdown).
    std::unordered_set<entt::registry*> _connectedRegistries;
};

} // namespace ya
