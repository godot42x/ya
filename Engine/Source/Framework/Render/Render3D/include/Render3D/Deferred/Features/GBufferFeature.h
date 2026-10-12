#pragma once

#include "Render3D/Composition/FeatureProtocol.h"
#include "Render3D/Common/ViewTargetStore.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/FrameUploadArena.h"
#include "Graph/RenderGraph.h"

#include <cstdint>

namespace ya
{

struct RenderFrameData;
struct GBufferStage;
class DeferredFrameResourceSet;

/// GBuffer generation feature: renders opaque geometry to deferred buffers.
/// 
/// Outputs: albedo, normal, ORM (occlusion/roughness/metallic), shading model, depth
/// These become inputs for SSAO, lighting, and other deferred passes.
class GBufferFeature
{
public:
    // ────────────────────────────────────────────────────────────────────────
    // Settings
    // ────────────────────────────────────────────────────────────────────────
    
    struct Settings
    {
        bool bEnabled = true;
    };
    
    // ────────────────────────────────────────────────────────────────────────
    // Facts (immutable per-view data)
    // ────────────────────────────────────────────────────────────────────────
    
    struct Facts
    {
        uint64_t viewId = 0;
        Extent2D viewExtent;
        const RenderFrameData* frameData = nullptr;
        uint32_t flightIndex = 0;
    };
    
    // ────────────────────────────────────────────────────────────────────────
    // Inputs (data flow from other features)
    // ────────────────────────────────────────────────────────────────────────
    
    struct Inputs
    {
        // GBuffer has no feature inputs (it's the first geometry pass)
    };
    
    // ────────────────────────────────────────────────────────────────────────
    // Resolved (resource requirements)
    // ────────────────────────────────────────────────────────────────────────
    
    struct Resolved
    {
        struct Outputs
        {
            EViewAttachment albedoRole  = EViewAttachment::GBuffer0;
            EViewAttachment normalRole  = EViewAttachment::GBuffer1;
            EViewAttachment ormRole     = EViewAttachment::GBuffer2;
            EViewAttachment shadingRole = EViewAttachment::GBuffer3;
            EViewAttachment depthRole   = EViewAttachment::SceneDepth;
        } outputs;
        
        bool bEnabled = false;
    };
    
    // ────────────────────────────────────────────────────────────────────────
    // Prepared (submission-allocated resources)
    // ────────────────────────────────────────────────────────────────────────
    
    struct Prepared
    {
        DescriptorSetHandle frameAndLightDS;
        DescriptorSetHandle skinningDS;
        FrameUploadArena::Allocation frameUBO;
        FrameUploadArena::Allocation lightUBO;
    };
    
    // ────────────────────────────────────────────────────────────────────────
    // Graph outputs (RDG handles)
    // ────────────────────────────────────────────────────────────────────────
    
    struct GraphOutputs
    {
        RGTextureHandle albedo;
        RGTextureHandle normal;
        RGTextureHandle orm;
        RGTextureHandle shading;
        RGTextureHandle depth;
    };
    
    // ────────────────────────────────────────────────────────────────────────
    // Feature protocol
    // ────────────────────────────────────────────────────────────────────────
    
    Resolved resolve(const Settings& settings, const Facts& facts, const Inputs& inputs);
    
    Prepared prepareBindings(
        DeferredFrameResourceSet* resourceSet,
        const Facts& facts,
        const Resolved& resolved);
    
    GraphOutputs append(
        RenderGraph& graph,
        GBufferStage* stage,
        const FeatureGraphContext& ctx,
        const Facts& facts,
        const Resolved& resolved,
        const Prepared& prepared,
        const Inputs& inputs);
};

} // namespace ya
