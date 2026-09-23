#pragma once

#include "Core/Base.h"
#include "RHI/Core/ImageResource.h"
#include "RHI/Core/RenderTexture.h"
#include "Render3D/EnvironmentLighting/EnvironmentLightingProcessor.h"
#include "Render3D/Common/RenderViewportSnapshot.h"
#include "Render3D/Deferred/DeferredPipelineDebugViews.h"
#include "Render3D/Common/Shadow/ShadowTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace ya
{

struct Scene;

/// Debug/colour outputs one pipeline published this frame, for the inspector.
/// A panel-facing view of a pipeline's targets -- not renderer state and not an
/// execution input.
struct RenderPipelineDebugOutputCatalog
{
    bool                          bShadowMappingEnabled         = false;
    std::shared_ptr<ImageResource> shadowDirectionalDepthResource = nullptr;
    std::shared_ptr<RenderTexture> viewOutputImageOwner      = nullptr;
    std::shared_ptr<RenderTexture> viewDepthImageOwner       = nullptr;
    std::shared_ptr<RenderTexture> postprocessOutputImageOwner   = nullptr;
    std::shared_ptr<RenderTexture> bloomExtractOwner             = nullptr;
    std::shared_ptr<RenderTexture> bloomBlurOwner                = nullptr;
    std::shared_ptr<RenderTexture> bloomCompositeOwner           = nullptr;
    bool                          bPostprocessingEnabled          = false;
};

/// Everything the debug catalog needs, already resolved to handles.
///
/// The catalog is a presentation of resources, so building it must not query
/// the renderer: the owner resolves the handles it is willing to expose and
/// hands them over. That keeps "which images does the inspector show" a pure
/// function of this value instead of a second traversal of renderer internals.
struct ViewportDebugCatalogInput
{
    bool bForwardPipeline  = false;
    bool bDeferredPipeline = false;

    RenderPipelineDebugOutputCatalog debugOutputs{};
    DeferredPipelineDebugViews       deferredViews{};
    std::shared_ptr<RenderTexture>   brdfLut = nullptr;

    /// Point-light shadow cubemap faces, flattened. Resolved per frame by the
    /// owner so the builder needs no lookup and no callback.
    std::array<std::shared_ptr<ImageResource>, ShadowConstants::POINT_SHADOW_FACE_COUNT> pointShadowFaces{};

    /// The inspect Scene's environment-lighting resolve state. Scene-bound on
    /// purpose: the entity-scoped lookups below are unambiguous only because
    /// this is one Scene's state, and the builder must not ask which Scene is
    /// current.
    const EnvironmentLightingProcessor::SceneWork* environmentLighting = nullptr;
    /// Scene to describe environment resources for, or null for none.
    Scene* inspectScene = nullptr;
};

/// Build the inspector's catalog: categories, slots and cube-face groups.
[[nodiscard]] YA_RENDER_3D_API RenderViewportDebugCatalog buildViewportDebugCatalog(
    const ViewportDebugCatalogInput& input);

/// Append only the image slots, reusing `catalog`'s slot metadata when it is
/// non-null (the cached-catalog path) and skipping it when it is not.
YA_RENDER_3D_API void appendViewportDebugImages(std::vector<RenderViewportDebugImageSlot>& images,
                                               RenderViewportDebugCatalog*                catalog,
                                               const ViewportDebugCatalogInput&           input);

/// Cheap digest of everything the catalog names. The owner compares it to the
/// digest it last built from, so an unchanged frame reuses the catalog instead
/// of rebuilding it.
[[nodiscard]] YA_RENDER_3D_API size_t viewportDebugCatalogSignature(
    const ViewportDebugCatalogInput& input);

/// Holds the catalog last built for one owner, keyed by its digest, so an
/// unchanged frame reuses it. Owned by whoever builds the snapshot the panel
/// reads -- the cache is part of this presentation concern, not of the renderer
/// that happens to hold the resources.
class ViewportDebugCatalogCache
{
  public:
    [[nodiscard]] std::shared_ptr<const RenderViewportDebugCatalog> get(
        const ViewportDebugCatalogInput& input);

  private:
    size_t                                            _signature = 0;
    std::shared_ptr<const RenderViewportDebugCatalog> _catalog;
};

} // namespace ya
