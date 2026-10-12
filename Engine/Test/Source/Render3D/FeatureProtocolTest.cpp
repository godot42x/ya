#include "Render3D/Composition/FeatureProtocol.h"

#include <type_traits>

#include <gtest/gtest.h>

namespace ya
{

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
