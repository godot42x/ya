#include "Render3D/Composition/FeatureProtocol.h"

#include <glm/glm.hpp>

#include <string>
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


// M21: a test-only feature must be able to join the composition with nothing but
// its own definition. This one is a multi-pass transform with a private scratch
// target: producer (declares a View target) -> transform (adds two passes over
// its own scratch) -> consumer (reads the transform's output). If any of this
// needed a pipeline, a shared context field or a bindings bag, the test would
// not compile — which is the point of writing it.
struct TestTransformFeature
{
    using Stage = RenderGraph; // the stage is the graph itself: no device object

    struct Settings
    {
        std::string_view passName{};
    };

    struct Facts
    {
        Extent2D viewExtent{};
    };

    struct Inputs
    {
        RGTextureHandle color{};
    };

    struct Resolved
    {
        bool             bEnabled = false;
        std::string_view passName{};
        /// The feature's private scratch: a graph transient it owns, not a View
        /// attachment and not a pipeline slot.
        RGTextureDesc scratch{};
    };

    struct PrepareContext
    {
    };

    struct Prepared
    {
        bool bHasScratch = false;
    };

    struct GraphOutputs
    {
        RGTextureHandle color{};
    };

    [[nodiscard]] Resolved resolve(const Settings& settings, const Facts& facts, const Inputs& inputs) const
    {
        (void)inputs;
        Resolved resolved;
        resolved.bEnabled = true;
        resolved.passName = settings.passName;
        resolved.scratch = RGTextureDesc{
            .label  = "TestTransform.Scratch",
            .format = EFormat::R16G16B16A16_SFLOAT,
            .extent = Extent3D{facts.viewExtent.width, facts.viewExtent.height, 1},
            .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        };
        return resolved;
    }

    [[nodiscard]] Result<Prepared, FeatureError> prepareBindings(const Resolved&       resolved,
                                                                 const PrepareContext& ctx) const
    {
        (void)ctx;
        Prepared prepared;
        prepared.bHasScratch = resolved.bEnabled;
        return prepared;
    }

    [[nodiscard]] Result<GraphOutputs, FeatureError> append(RenderGraph&               graph,
                                                            Stage*                     stage,
                                                            const FeatureGraphContext& ctx,
                                                            const Facts&               facts,
                                                            const Resolved&            resolved,
                                                            const Prepared&            prepared,
                                                            const Inputs&              inputs) const
    {
        (void)stage;
        (void)ctx;
        (void)facts;
        GraphOutputs outputs;
        outputs.color = inputs.color;
        if (!resolved.bEnabled || !prepared.bHasScratch || !inputs.color.isValid()) {
            return FeatureError::MissingDependency;
        }

        const RGTextureHandle scratch = graph.createTexture(resolved.scratch);
        const RGTextureHandle result  = graph.createTexture(RGTextureDesc{
            .label  = "TestTransform.Result",
            .format = EFormat::R16G16B16A16_SFLOAT,
            .extent = Extent3D{facts.viewExtent.width, facts.viewExtent.height, 1},
            .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        });
        const Rect2D area{.pos = {0, 0}, .extent = facts.viewExtent.toVec2()};
        (void)graph.addPass(std::string(resolved.passName) + ".First",
                            [inputs, scratch, area](RGPassBuilder& builder) {
                                builder.read(inputs.color);
                                builder.declareRaster({
                                    .renderArea = area,
                                    .layerCount = 1,
                                    .colors = {{.color = scratch, .finalLayout = EImageLayout::ShaderReadOnlyOptimal}},
                                });
                            });
        (void)graph.addPass(std::string(resolved.passName) + ".Second",
                            [scratch, result, area](RGPassBuilder& builder) {
                                builder.read(scratch);
                                builder.declareRaster({
                                    .renderArea = area,
                                    .layerCount = 1,
                                    .colors = {{.color = result, .finalLayout = EImageLayout::ShaderReadOnlyOptimal}},
                                });
                            });
        // The consumer downstream gets the transform's output handle; the scratch
        // never leaves this feature.
        outputs.color = result;
        return outputs;
    }
};

static_assert(Feature<TestTransformFeature>);

TEST(FeatureProtocolTest, TestOnlyFeatureRunsTheFullChainWithoutCorePlumbing)
{
    // request: the feature declares a View target from its own resolve
    const TestTransformFeature feature;
    const auto resolved = feature.resolve(
        TestTransformFeature::Settings{.passName = "TestTransform"},
        TestTransformFeature::Facts{.viewExtent = {.width = 32, .height = 24}},
        TestTransformFeature::Inputs{});
    ASSERT_TRUE(resolved.bEnabled);

    // prepare: no device object, no shared bag — it needs nothing from outside
    const auto prepared = feature.prepareBindings(resolved, TestTransformFeature::PrepareContext{});
    ASSERT_TRUE(prepared.isOk());
    EXPECT_TRUE(prepared.value().bHasScratch);

    // append: producer -> multi-pass transform over private scratch -> consumer
    RenderGraph graph;
    const auto source = graph.createTexture(RGTextureDesc{
        .label  = "TestTransform.Source",
        .format = EFormat::R16G16B16A16_SFLOAT,
        .extent = Extent3D{32, 24, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });
    // The producer: something has to write the source before the transform reads
    // it, exactly as a real upstream pass would.
    (void)graph.addPass("TestTransform.Producer", [source](RGPassBuilder& builder) {
        builder.declareRaster({
            .renderArea = Rect2D{.pos = {0, 0}, .extent = glm::vec2(32.0f, 24.0f)},
            .layerCount = 1,
            .colors = {{.color = source, .finalLayout = EImageLayout::ShaderReadOnlyOptimal}},
        });
    });

    const auto outputs = feature.append(
        graph,
        nullptr,
        FeatureGraphContext{},
        TestTransformFeature::Facts{.viewExtent = {.width = 32, .height = 24}},
        resolved,
        prepared.value(),
        TestTransformFeature::Inputs{.color = source});
    ASSERT_TRUE(outputs.isOk());

    // publish: the View's output is what the transform returned, and its scratch
    // stayed private — the graph knows both passes and the consumer path.
    EXPECT_TRUE(outputs.value().color.isValid());
    EXPECT_NE(outputs.value().color, source);
    const auto compiled = graph.compile();
    if (!compiled.issues.empty()) {
        FAIL() << "compile issue [" << static_cast<int>(compiled.issues[0].kind) << "]: "
               << compiled.issues[0].message << "\n" << graph.debugDump(compiled);
    }
    size_t transformPasses = 0;
    for (const auto& pass : graph.getPasses()) {
        if (pass.name.find("TestTransform.") != std::string::npos) {
            ++transformPasses;
        }
    }
    // Producer + the transform's two passes: the chain the plan asks a test
    // feature to run end to end.
    EXPECT_EQ(transformPasses, 3u);
}

} // namespace ya
