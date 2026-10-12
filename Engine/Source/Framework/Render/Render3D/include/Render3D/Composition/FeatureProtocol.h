#pragma once

#include "Core/Base.h"
#include "Graph/RenderGraph.h"

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace ya
{

// ────────────────────────────────────────────────────────────────────────────
// Typed resource references
// ────────────────────────────────────────────────────────────────────────────

/// Type-safe texture reference in composition. The tag T is compile-time only;
/// lowering may alias multiple TextureRef<T> to the same RG handle.
template <typename T>
struct TextureRef
{
    static_assert(std::is_empty_v<T>, "TextureRef tag must be empty type");

    uint32_t _index = ~0u;

    constexpr TextureRef() = default;
    explicit constexpr TextureRef(uint32_t idx) : _index(idx) {}

    [[nodiscard]] constexpr bool isValid() const { return _index != ~0u; }
    [[nodiscard]] constexpr uint32_t index() const { return _index; }

    constexpr bool operator==(const TextureRef& other) const = default;
};

/// Type-safe buffer reference in composition.
template <typename T>
struct BufferRef
{
    static_assert(std::is_empty_v<T>, "BufferRef tag must be empty type");

    uint32_t _index = ~0u;

    constexpr BufferRef() = default;
    explicit constexpr BufferRef(uint32_t idx) : _index(idx) {}

    [[nodiscard]] constexpr bool isValid() const { return _index != ~0u; }
    [[nodiscard]] constexpr uint32_t index() const { return _index; }

    constexpr bool operator==(const BufferRef& other) const = default;
};

/// Reference to another feature's prepared bindings. T is the prepared data type.
/// PreparedRef<T> must be resolved in prepare phase, not resolve phase.
template <typename T>
struct PreparedRef
{
    uint32_t _index = ~0u;

    constexpr PreparedRef() = default;
    explicit constexpr PreparedRef(uint32_t idx) : _index(idx) {}

    [[nodiscard]] constexpr bool isValid() const { return _index != ~0u; }
    [[nodiscard]] constexpr uint32_t index() const { return _index; }

    constexpr bool operator==(const PreparedRef& other) const = default;
};

// ────────────────────────────────────────────────────────────────────────────
// Composition tags (empty types for typed refs)
// ────────────────────────────────────────────────────────────────────────────

namespace composition_tag
{

struct GBufferAlbedo {};
struct GBufferNormal {};
struct GBufferMaterial {};
struct GBufferEmissive {};
struct Depth {};
struct Color {};
struct DisplayColor {};
struct AmbientOcclusion {};
struct Shadow {};

} // namespace composition_tag

// ────────────────────────────────────────────────────────────────────────────
// Feature concept (C++20)
// ────────────────────────────────────────────────────────────────────────────

struct FeatureResolveContext;
struct FeaturePrepareContext;
struct FeatureGraphContext;

template <typename F>
concept Feature = requires(F feature) {
    typename F::Inputs;
    typename F::Outputs;
    typename F::Resolved;
    typename F::Prepared;

    // resolve(context, inputs, settings) -> Resolved
    { feature.resolve(std::declval<FeatureResolveContext&>(),
                     std::declval<const typename F::Inputs&>(),
                     std::declval<const void*>()) } -> std::same_as<typename F::Resolved>;

    // prepareBindings(context, resolved, preparedInputs) -> Prepared
    { feature.prepareBindings(std::declval<FeaturePrepareContext&>(),
                             std::declval<const typename F::Resolved&>(),
                             std::declval<const void*>()) } -> std::same_as<typename F::Prepared>;

    // append(context, resolved, prepared, inputs) -> Outputs
    { feature.append(std::declval<FeatureGraphContext&>(),
                    std::declval<const typename F::Resolved&>(),
                    std::declval<const typename F::Prepared&>(),
                    std::declval<const void*>()) } -> std::same_as<typename F::Outputs>;
};

// ────────────────────────────────────────────────────────────────────────────
// Contexts (forward declarations, defined in separate headers)
// ────────────────────────────────────────────────────────────────────────────

struct IRender;
struct IRenderResourceFactory;
class RenderSubmission;
class ViewTargetStore;

/// Context for feature resolve phase: resource requirements and configuration.
struct FeatureResolveContext
{
    IRender*                 render         = nullptr;
    IRenderResourceFactory*  factory        = nullptr;
    ViewTargetStore*         targetStore    = nullptr;
    uint64_t                 hostTick       = 0;
    uint32_t                 flightIndex    = 0;
};

/// Context for feature prepare phase: allocate bindings using submission resources.
struct FeaturePrepareContext
{
    IRender*                 render         = nullptr;
    RenderSubmission*        submission     = nullptr;
    ViewTargetStore*         targetStore    = nullptr;
    uint64_t                 hostTick       = 0;
    uint32_t                 flightIndex    = 0;
};

/// Context for feature graph append phase: add RDG passes.
struct FeatureGraphContext
{
    RenderGraph*             graph          = nullptr;
    IRender*                 render         = nullptr;
    uint64_t                 viewId         = 0;
};

} // namespace ya
