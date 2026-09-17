#pragma once

#include "Core/Api.h"
#include "Core/TypeIndex.h"

#include <entt/entt.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace ya
{

struct Entity;
struct Scene;
struct CompanionManager;

/// What a generated companion entity is for.
enum class ECompanionKind : uint8_t
{
    /// Real scene content that happens to be generated: drawn in every view,
    /// rebuilt from the host on load, editable through the host.
    Content = 0,
    /// Editor visualization of a host component (camera body, light icon):
    /// drawn only in views that ask for gizmos, read-only in the editor.
    EditorGizmo,
};

/// Packaging seam. Runtime packs keep every companion; a future editor-only
/// pack filter drops the declared ones together with their assets. The filter
/// itself is not implemented -- the declaration is the single place it reads.
enum class EAssetPackClass : uint8_t
{
    Runtime = 0,
    EditorOnly,
};

/// Boundary declaration for the generated entities of one host component type.
///
/// This is the only place a companion's provenance, editability and packing
/// class are stated. Consumers ask the registry (through CompanionManager)
/// instead of testing component fields: never asset path, never entity name,
/// never "does this look generated" heuristics.
struct CompanionSpec
{
    ECompanionKind  kind            = ECompanionKind::Content;
    /// Companion geometry may be edited in the editor. Model mesh children are
    /// addressable and transformable today, so the default keeps them
    /// editable; gizmos declare false.
    bool            bAuthorEditable = true;
    EAssetPackClass packClass       = EAssetPackClass::Runtime;
    /// Companion values follow the host's transform (light icon direction).
    /// Position/orientation follow for free through the node hierarchy; this
    /// flag only asks for a refresh when the host moves.
    bool            bFollowsHostTransform = false;
    /// Build the companion entity for `host` (a child node, so the host
    /// transform applies) plus the components it carries. Required. The
    /// manager stamps the generated-entity marker itself.
    std::function<Entity*(Scene& scene, Entity& host)> onCreate;
    /// Release companion-owned resources before the entity is destroyed.
    std::function<void(Scene& scene, Entity& companion)> onDestroy;
    /// Re-read host-driven values (light colour/intensity, orientation).
    std::function<void(Scene& scene, Entity& host, Entity& companion)> onUpdateHost;
};

// Type-erased per-type hooks. The registry stores these instead of entt
// templates so it stays free of the linkage layer (CompanionManager lives
// above scene-3d in the dependency order).
using FCompanionPresentFn = bool (*)(const entt::registry& registry, entt::entity entity);
using FCompanionWireFn    = void (*)(entt::registry& registry, CompanionManager& manager, bool bConnect);
using FCompanionSweepFn   = void (*)(entt::registry& registry, CompanionManager& manager);

/// Declared companion boundaries, keyed by host component type.
class YA_SCENE_3D_API CompanionRegistry final
{
  public:
    static CompanionRegistry& get();

    /// Declare (or replace) the companion boundary of `hostType` together with
    /// the hooks that keep it in sync. Declaration order is preserved: an
    /// entity carrying several declared host types keeps a single companion,
    /// refreshed by each present declaration in that order.
    void declare(type_index_t                       hostType,
                 CompanionSpec                      spec,
                 FCompanionPresentFn                present,
                 FCompanionWireFn                   wire,
                 FCompanionSweepFn                  sweep);
    void undefine(type_index_t hostType);

    [[nodiscard]] const CompanionSpec* find(type_index_t hostType) const;
    [[nodiscard]] size_t               size() const { return _entries.size(); }
    [[nodiscard]] bool                 empty() const { return _entries.empty(); }

    /// Drop every declaration. Signals already connected are not touched, so
    /// this exists for tests and process teardown only.
    void clear();

  private:
    friend struct CompanionManager;

    struct FEntry
    {
        type_index_t        hostType = 0;
        CompanionSpec       spec;
        FCompanionPresentFn present = nullptr;
        FCompanionWireFn    wire    = nullptr;
        FCompanionSweepFn   sweep   = nullptr;
    };

    std::vector<FEntry> _entries;
};

} // namespace ya
