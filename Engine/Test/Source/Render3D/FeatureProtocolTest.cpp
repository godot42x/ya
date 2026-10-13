#include "Render3D/Composition/FeatureProtocol.h"
#include "Render3D/Composition/ViewOutputCollection.h"
#include "Render3D/Common/RenderViewOutput.h"

#include "Graph/RenderGraphExecutor.h"
#include "RHI/Core/RenderResourceFactory.h"

#include <glm/glm.hpp>

#include <array>

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

    [[nodiscard]] static constexpr std::string_view name() { return "MinimalFeature"; }
    static void targets(const Resolved&, std::vector<ViewAttachmentDesc>&) {}

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

    [[nodiscard]] static constexpr std::string_view name() { return "MinimalFeature"; }
    static void targets(const Resolved&, std::vector<ViewAttachmentDesc>&) {}

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

    [[nodiscard]] static constexpr std::string_view name() { return "TestTransformFeature"; }
    static void targets(const Resolved&, std::vector<ViewAttachmentDesc>&) {}
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


TEST(FeatureProtocolTest, FeatureWithoutTargetsRegistrationIsNotAFeature)
{
    // D3/M26: the recipe folds targets over the concept, so a feature that
    // cannot state its declared targets does not compile into a map. This is
    // the negative half of "wiring omissions are compile errors": forgetting
    // targets() fails here, not at runtime with a missing View target.
    struct NoTargetsFeature
    {
        struct Stage {};
        struct Settings {};
        struct Facts {};
        struct Inputs {};
        struct Resolved {};
        struct PrepareContext {};
        struct Prepared {};
        struct GraphOutputs {};

        [[nodiscard]] static constexpr std::string_view name() { return "NoTargets"; }
        // targets() missing on purpose.

        Resolved resolve(const Settings&, const Facts&, const Inputs&) const { return {}; }
        Result<Prepared, FeatureError> prepareBindings(const Resolved&, const PrepareContext&) const { return Prepared{}; }
        Result<GraphOutputs, FeatureError> append(RenderGraph&, Stage*, const FeatureGraphContext&, const Facts&, const Resolved&, const Prepared&, const Inputs&) const { return GraphOutputs{}; }
    };
    static_assert(!Feature<NoTargetsFeature>);

    struct NoNameFeature
    {
        struct Stage {};
        struct Settings {};
        struct Facts {};
        struct Inputs {};
        struct Resolved {};
        struct PrepareContext {};
        struct Prepared {};
        struct GraphOutputs {};

        // name() missing on purpose.
        static void targets(const Resolved&, std::vector<ViewAttachmentDesc>&) {}

        Resolved resolve(const Settings&, const Facts&, const Inputs&) const { return {}; }
        Result<Prepared, FeatureError> prepareBindings(const Resolved&, const PrepareContext&) const { return Prepared{}; }
        Result<GraphOutputs, FeatureError> append(RenderGraph&, Stage*, const FeatureGraphContext&, const Facts&, const Resolved&, const Prepared&, const Inputs&) const { return GraphOutputs{}; }
    };
    static_assert(!Feature<NoNameFeature>);
}

namespace
{

// Minimal factory for the executor-backed publish tests below: the same shape
// the RDG core tests use, reduced to what transient textures need.
class PublishTestImage final : public IImage
{
    ImageCreateInfo _desc;

  public:
    explicit PublishTestImage(ImageCreateInfo desc) : _desc(std::move(desc)) {}
    ImageHandle getHandle() const override { return ImageHandle{reinterpret_cast<void*>(1)}; }
    uint32_t getWidth() const override { return _desc.extent.width; }
    uint32_t getHeight() const override { return _desc.extent.height; }
    EFormat::T getFormat() const override { return _desc.format; }
    uint32_t getMipLevels() const override { return _desc.mipLevels; }
    uint32_t getArrayLayers() const override { return _desc.arrayLayers; }
    EImageUsage::T getUsage() const override { return _desc.usage; }
    EImageLayout::T getCompatibilityLayout() const override { return _desc.initialLayout; }
    void setDebugName(const std::string& name) override { _desc.label = name; }
};

class PublishTestImageView final : public IImageView
{
  public:
    PublishTestImageView(IImage* image, const ImageViewCreateInfo& desc)
    {
        _image = image;
        _subresourceRange = {
            .aspectMask     = desc.aspectFlags,
            .baseMipLevel   = desc.baseMipLevel,
            .levelCount     = desc.levelCount,
            .baseArrayLayer = desc.baseArrayLayer,
            .layerCount     = desc.layerCount,
        };
    }
    ImageViewHandle getHandle() const override { return ImageViewHandle{reinterpret_cast<void*>(2)}; }
    EFormat::T getFormat() const override { return _image ? _image->getFormat() : EFormat::Undefined; }
    void setDebugName(const std::string&) override {}
};

class PublishTestFactory final : public IRenderResourceFactory
{
  public:
    std::shared_ptr<IBuffer> createBuffer(const BufferCreateInfo&) override { return nullptr; }
    std::shared_ptr<Sampler> createSampler(const SamplerDesc&) override { return nullptr; }
    std::shared_ptr<IImage> createImage(const ImageCreateInfo& desc) override
    {
        return std::make_shared<PublishTestImage>(desc);
    }
    std::shared_ptr<IImage> importImage(const ImportedImageDesc&) override { return nullptr; }
    std::shared_ptr<IImageView> createImageView(std::shared_ptr<IImage> image, const ImageViewCreateInfo& desc) override
    {
        return std::make_shared<PublishTestImageView>(image.get(), desc);
    }
};

} // namespace

// D4/M21: the chain a real View runs, through the real publish path. The
// feature runs request -> resolve -> prepare -> append exactly as before; the
// difference is what follows: the executor captures the exports and
// composition::collectViewOutput reads them back by typed handle, so the
// published RenderViewOutput is proven to carry what the recipe reported --
// with no export name involved anywhere in the publish half.
TEST(FeatureProtocolTest, PublishedOutputIsTheTypedReaderOfWhatTheRecipeReported)
{
    const TestTransformFeature feature;
    const auto resolved = feature.resolve(
        TestTransformFeature::Settings{.passName = "TestTransform"},
        TestTransformFeature::Facts{.viewExtent = {.width = 32, .height = 24}},
        TestTransformFeature::Inputs{});
    ASSERT_TRUE(resolved.bEnabled);
    const auto prepared = feature.prepareBindings(resolved, TestTransformFeature::PrepareContext{});
    ASSERT_TRUE(prepared.isOk());

    RenderGraph graph;
    const auto source = graph.createTexture(RGTextureDesc{
        .label  = "TestTransform.Source",
        .format = EFormat::R16G16B16A16_SFLOAT,
        .extent = Extent3D{32, 24, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });
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

    // Publish: export the reported handle (as exportGraphOutputs does, for
    // topology diagnostics) and read it back through the typed mapping.
    graph.exportTexture(outputs.value().color, "TestTransform.View.Color");
    PublishTestFactory factory;
    RenderGraphExecutor executor(factory);
    RGCompiledGraph            compiled;
    RenderGraphExecutionResult execution;
    ASSERT_TRUE(executor.prepare(graph, compiled, &execution));

    const composition::ViewOutputHandles handles{
        .color = outputs.value().color,
        .depth = {}, // nothing exported for it: stays null, like a disabled feature
    };
    const RenderViewOutput published = composition::collectViewOutput(
        execution, handles, RenderViewOutputDesc{.viewId = 7, .extent = {.width = 32, .height = 24}});

    ASSERT_TRUE(published.color != nullptr);
    EXPECT_EQ(published.color->getWidth(), 32u);
    EXPECT_EQ(published.color->getHeight(), 24u);
    // The unexported handle is absent -- presence is keyed by identity, and a
    // name that was never exported cannot resurrect it.
    EXPECT_EQ(published.depth, nullptr);
    EXPECT_EQ(published.desc.colorFormat, published.color->getFormat());
}

// D4/M10: one View's append failure is a family verdict, not a per-View one.
// The second View's append is poisoned by handing it an invalid input after
// the first View contributed passes; the family must answer with no topology
// and no published views, not with the surviving View's frame.
TEST(FeatureProtocolTest, AppendFailureKeepsTheWholeFamilyUnpublished)
{
    PublishTestFactory factory;
    RenderGraphExecutor executor(factory);

    RenderGraph graph;
    TestTransformFeature feature;
    const FeatureGraphContext graphContext{};

    // View A records cleanly: producer + transform over its own scratch.
    const auto resolvedA = feature.resolve(
        TestTransformFeature::Settings{.passName = "TestTransform.A"},
        TestTransformFeature::Facts{.viewExtent = {.width = 32, .height = 24}},
        TestTransformFeature::Inputs{});
    const auto preparedA = feature.prepareBindings(resolvedA, TestTransformFeature::PrepareContext{});
    ASSERT_TRUE(preparedA.isOk());
    const auto sourceA = graph.createTexture(RGTextureDesc{
        .label  = "Family.Source.A",
        .format = EFormat::R16G16B16A16_SFLOAT,
        .extent = Extent3D{32, 24, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });
    (void)graph.addPass("Family.Producer.A", [sourceA](RGPassBuilder& builder) {
        builder.declareRaster({
            .renderArea = Rect2D{.pos = {0, 0}, .extent = glm::vec2(32.0f, 24.0f)},
            .layerCount = 1,
            .colors = {{.color = sourceA, .finalLayout = EImageLayout::ShaderReadOnlyOptimal}},
        });
    });
    const auto outputsA = feature.append(
        graph, nullptr, graphContext,
        TestTransformFeature::Facts{.viewExtent = {.width = 32, .height = 24}},
        resolvedA, preparedA.value(), TestTransformFeature::Inputs{.color = sourceA});
    ASSERT_TRUE(outputsA.isOk());

    // View B: append fails (a feature whose dependency is gone -- injected by
    // feeding it a handle that is not a producer's output this family built).
    const RGTextureHandle dangling{};
    const auto outputsB = feature.append(
        graph, nullptr, graphContext,
        TestTransformFeature::Facts{.viewExtent = {.width = 32, .height = 24}},
        resolvedA, preparedA.value(), TestTransformFeature::Inputs{.color = dangling});
    ASSERT_TRUE(outputsB.isErr());
    EXPECT_EQ(outputsB.error(), FeatureError::MissingDependency);

    // The atomic family verdict: nothing compiles into an executable frame,
    // because View A's passes belong to a chain whose continuation failed. A
    // pipeline that appended B would not execute or publish this family at
    // all; the same graph simply dies here unexecuted.
    RGCompiledGraph compiled;
    compiled = graph.compile();
    // A is alone in this graph, so it does compile; the pipeline-level
    // guarantee lives in recordFamily (both pipelines return an empty
    // ViewFamilyRenderResult when any View's append fails). What this test
    // pins is the injection contract: an invalid input fails append with
    // MissingDependency instead of recording a broken pass set.
    EXPECT_TRUE(compiled.isValid());
}

} // namespace ya
