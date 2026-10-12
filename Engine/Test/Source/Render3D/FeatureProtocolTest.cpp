#include "Render3D/Composition/FeatureProtocol.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(FeatureProtocolTest, TextureRefTypeSafety)
{
    using namespace composition_tag;

    CompositionTextureRef<Color> color{0};
    CompositionTextureRef<Depth> depth{0};

    // Same index but different types - not interchangeable
    EXPECT_TRUE(color.isValid());
    EXPECT_TRUE(depth.isValid());
    EXPECT_EQ(color.index(), 0u);
    EXPECT_EQ(depth.index(), 0u);

    // Type-safe: cannot assign color to depth
    // This would fail to compile:
    // depth = color;

    // Can compare same types
    CompositionTextureRef<Color> color2{0};
    EXPECT_EQ(color, color2);

    // Invalid refs
    CompositionTextureRef<Color> invalid;
    EXPECT_FALSE(invalid.isValid());
    EXPECT_NE(color, invalid);
}

TEST(FeatureProtocolTest, BufferRefTypeSafety)
{
    struct LightBuffer {};
    struct ShadowBuffer {};

    CompositionBufferRef<LightBuffer> lights{1};
    CompositionBufferRef<ShadowBuffer> shadows{1};

    EXPECT_TRUE(lights.isValid());
    EXPECT_TRUE(shadows.isValid());
    EXPECT_EQ(lights.index(), 1u);
    EXPECT_EQ(shadows.index(), 1u);

    // Cannot cross-assign different buffer types
    // This would fail to compile:
    // shadows = lights;
}

TEST(FeatureProtocolTest, PreparedRefTypeSafety)
{
    struct SSAOPrepared {};
    struct LightingPrepared {};

    CompositionPreparedRef<SSAOPrepared> ssao{2};
    CompositionPreparedRef<LightingPrepared> lighting{3};

    EXPECT_TRUE(ssao.isValid());
    EXPECT_TRUE(lighting.isValid());
    EXPECT_EQ(ssao.index(), 2u);
    EXPECT_EQ(lighting.index(), 3u);

    // Cannot mix prepared ref types
    // This would fail to compile:
    // lighting = ssao;

    CompositionPreparedRef<SSAOPrepared> invalid;
    EXPECT_FALSE(invalid.isValid());
}

TEST(FeatureProtocolTest, CompositionTagsAreEmpty)
{
    using namespace composition_tag;

    // All tags must be empty types for zero-cost abstraction
    static_assert(std::is_empty_v<GBufferAlbedo>);
    static_assert(std::is_empty_v<GBufferNormal>);
    static_assert(std::is_empty_v<GBufferMaterial>);
    static_assert(std::is_empty_v<GBufferEmissive>);
    static_assert(std::is_empty_v<Depth>);
    static_assert(std::is_empty_v<Color>);
    static_assert(std::is_empty_v<DisplayColor>);
    static_assert(std::is_empty_v<AmbientOcclusion>);
    static_assert(std::is_empty_v<Shadow>);

    // CompositionTextureRef<T> should be just uint32_t in memory
    static_assert(sizeof(CompositionTextureRef<Color>) == sizeof(uint32_t));
    static_assert(sizeof(CompositionBufferRef<Color>) == sizeof(uint32_t));
    static_assert(sizeof(CompositionPreparedRef<Color>) == sizeof(uint32_t));
}

// Minimal feature implementation for concept validation: the enforced shape,
// not a stand-in for it. `Stage` rides on the concept through the alias.
struct MinimalFeature
{
    struct Stage {};

    struct Settings {};
    struct Facts {};
    struct Inputs {};
    struct Resolved {};
    struct PrepareContext {};
    struct Prepared {};
    struct GraphOutputs {};

    Resolved resolve(const Settings&, const Facts&, const Inputs&) const { return {}; }
    Result<Prepared, FeatureError> prepareBindings(const Resolved&, const PrepareContext&) const { return Prepared{}; }
    Result<GraphOutputs, FeatureError> append(RenderGraph&,
                                              Stage*,
                                              const FeatureGraphContext&,
                                              const Facts&,
                                              const Resolved&,
                                              const Prepared&,
                                              const Inputs&) const
    {
        return GraphOutputs{};
    }
};

TEST(FeatureProtocolTest, FeatureConceptSatisfied)
{
    // The concept must accept a real-shaped feature...
    static_assert(Feature<MinimalFeature>);

    MinimalFeature feature;
    const MinimalFeature::Inputs inputs{};
    const auto resolved = feature.resolve(MinimalFeature::Settings{}, MinimalFeature::Facts{}, inputs);
    const auto prepared = feature.prepareBindings(resolved, MinimalFeature::PrepareContext{});
    EXPECT_TRUE(prepared.isOk());
}

// A feature whose append forgets its Result wrapper is not a Feature: the
// concept's second conjunct is what makes the uniform protocol enforceable.
struct UnwrappedAppendFeature
{
    struct Stage {};
    struct Settings {};
    struct Facts {};
    struct Inputs {};
    struct Resolved {};
    struct PrepareContext {};
    struct Prepared {};
    struct GraphOutputs {};

    Resolved resolve(const Settings&, const Facts&, const Inputs&) const { return {}; }
    Result<Prepared, FeatureError> prepareBindings(const Resolved&, const PrepareContext&) const { return Prepared{}; }
    GraphOutputs append(RenderGraph&,
                        Stage*,
                        const FeatureGraphContext&,
                        const Facts&,
                        const Resolved&,
                        const Prepared&,
                        const Inputs&) const
    {
        return {};
    }
};

TEST(FeatureProtocolTest, IncompleteFeatureDoesNotSatisfyConcept)
{
    // Missing the nested types is simply "not a Feature", not a hard error.
    struct IncompleteFeature
    {
        struct Inputs {};
        struct Outputs {};
        struct Resolved {};
        // Missing Prepared / Stage / the rest.
    };
    static_assert(!Feature<IncompleteFeature>);

    // A drifted signature is also rejected, so the protocol cannot silently
    // stop applying to a feature that used to satisfy it.
    static_assert(!Feature<UnwrappedAppendFeature>);
}

} // namespace ya
