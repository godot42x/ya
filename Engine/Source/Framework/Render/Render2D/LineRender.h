#pragma once

#include "glm/glm.hpp"

#include "Core/Base.h"
#include "Core/Common/Types.h"

#include "RHI/Core/Buffer.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Pipeline.h"
#include "RHI/RenderDefines.h"

#include "QuadRender.h"

#include <array>
#include <memory>
#include <vector>

namespace ya
{

struct IRender;

/**
 * @brief FLineRender - World-space debug line rendering used by Render2D.
 *
 * A minimal line-list pipeline sharing the camera view-projection convention
 * of the world-space sprite pipeline. Used for debug overlays such as
 * physics collision boxes.
 */
struct YA_RENDER_2D_API FLineRender
{
    struct Vertex
    {
        glm::vec3 pos;
        glm::vec4 color;
    };

    static constexpr size_t MaxVertexCount = 8192; // 4096 line segments

    // Same shared-buffer rule as FQuadRender: multiple flushes per frame must
    // write to distinct regions because the GPU reads the buffer only after
    // recording finishes.
    static constexpr uint32_t kFrameFlushSlots = 4;

    using FrameUBO = FQuadRender::FrameUBO;

    IRender* _render = nullptr;

    PipelineLayoutDesc _pipelineDesc = PipelineLayoutDesc{
        .pushConstants        = {},
        .descriptorSetLayouts = {
            DescriptorSetLayoutDesc{
                .label    = "Frame_UBO",
                .set      = 0,
                .bindings = {
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Vertex,
                    },
                },
            },
        },
    };

    std::shared_ptr<IDescriptorPool>      _descriptorPool;
    std::shared_ptr<IDescriptorSetLayout> _frameUboDSL;
    std::shared_ptr<IPipelineLayout>      _pipelineLayout;
    std::shared_ptr<IGraphicsPipeline>    _pipeline;

    struct FlightResources
    {
        DescriptorSetHandle      frameUboDS{};
        std::shared_ptr<IBuffer> frameUBOBuffer{};
        std::shared_ptr<IBuffer> vertexBuffer{};
        Vertex*                  vertexPtrHead = nullptr;
    };
    struct PassResources
    {
        std::array<FlightResources, MAX_FLIGHTS_IN_FLIGHT> flights{};
    };
    std::array<PassResources, FQuadRender::kMaxPassSlots> _passResources{};
    Render2DPassSlot _activePassSlot = 0;
    uint32_t         _activeFlightIndex = 0;
    Vertex*          vertexPtr          = nullptr;
    Vertex*          vertexPtrHead      = nullptr;
    uint32_t         vertexCount        = 0;
    uint32_t         batchStartVertex   = 0; // start of the pending batch in the shared buffer

    void init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat);
    void destroy();
    /// Lazily allocate one pass slot's buffers + descriptor sets (all flights).
    void ensureSlotResources(Render2DPassSlot passSlot);
    void begin(Render2DPassSlot passSlot);
    void flush(ICommandBuffer* cmdBuf, const glm::mat4& viewProj);

    void addLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color);
    void addWireBox(const glm::mat4& model, const glm::vec3& halfExtent, const glm::vec4& color);
    void addWireSphere(const glm::vec3& center, float radius, const glm::vec4& color);
};

} // namespace ya
