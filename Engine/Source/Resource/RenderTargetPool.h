

#pragma once

#include "Core/Base.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/RenderDefines.h"
#include "Resource/ResourceRegistry.h"
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>


namespace ya
{

// Forward declarations
struct IImage;
struct IImageView;
struct IRender;



/**
 * @brief Specification for allocating a render target from the pool
 */
struct RenderTargetSpec
{
    std::string label = "RenderTarget";

    // Image properties
    EFormat::T      format        = EFormat::R8G8B8A8_UNORM;
    Extent2D        extent        = {.width = 0, .height = 0}; // 0 means use default/window size
    uint32_t        mipLevels     = 1;
    ESampleCount::T samples       = ESampleCount::Sample_1;
    EImageUsage::T  usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled;
    EImageLayout::T initialLayout = EImageLayout::Undefined;
    uint32_t        layerCount    = 1;

    // Pool behavior
    bool     bTransient   = false; // true = auto release at end of frame
    bool     bResizable   = true;  // true = auto resize when window resizes
    uint32_t frameLatency = 3;     // Number of frames to keep before recycling (for multi-buffering)

    // Comparison for map key
    bool operator==(const RenderTargetSpec &other) const
    {
        return format == other.format &&
               extent.width == other.extent.width &&
               extent.height == other.extent.height &&
               mipLevels == other.mipLevels &&
               samples == other.samples &&
               usage == other.usage &&
               layerCount == other.layerCount;
    }
};

/**
 * @brief Pooled render target entry
 */
struct PooledRenderTarget
{
    RID                            rtID;
    RenderTargetSpec               spec;
    std::shared_ptr<IRenderTarget> renderTarget;
    uint64_t                       lastUsedFrame = 0;
    bool                           bInUse        = false;
    FName                          debugName;
};

/**
 * @brief RenderTargetPool - Managed pool for render targets
 *
 * Design goals:
 * 1. Avoid repeated creation/destruction of render targets
 * 2. Automatic size management (resize when window changes)
 * 3. Support both persistent and transient render targets
 * 4. Multi-frame buffering support
 *
 * Usage example:
 * @code
 * // Acquire a transient render target for bloom pass
 * auto spec = RenderTargetSpec{
 *     .format = EFormat::R16G16B16A16_SFLOAT,
 *     .extent = viewportSize,
 *     .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
 *     .bTransient = true  // Will be auto-released at frame end
 * };
 * RTID bloomRT = RenderTargetPool::acquire(spec);
 *
 * // Use it...
 * auto rt = RenderTargetPool::get(bloomRT);
 * cmdBuf->beginRendering({.renderTarget = rt, ...});
 *
 * // Release (optional for transient, automatic at frame end)
 * RenderTargetPool::release(bloomRT);
 * @endcode
 */
struct RenderTargetPool : public IResourceCache
{

    // Members
    IRender   *_render = nullptr;
    std::mutex _mutex;

    // Active pool entries
    std::unordered_map<RID, std::unique_ptr<PooledRenderTarget>> _entries;

    // Available pool (spec hash -> list of available entries)
    std::unordered_map<std::string, std::vector<PooledRenderTarget *>> _availablePool;

    // Transient RTs to auto-release at frame end
    std::vector<RID> _transientRTs;

    // Resizable RTs (auto-update on window resize)
    std::vector<RID> _resizableRTs;

    // Frame tracking
    uint64_t _currentFrameIndex = 0;
    uint64_t _nextRTid          = 1;

    // Window size for auto-resize
    uint32_t _windowWidth  = 0;
    uint32_t _windowHeight = 0;

    bool _bInitialized = false;

  public:
    static RenderTargetPool &get();

    FName getCacheName() const override { return "RenderTargetPool"; }

    void clearCache() override;
    void invalidate(const std::string &assetName) override {}

    // Core API

    /**
     * @brief Initialize the pool
     * @param render The renderer instance for creating images
     */
    void init(IRender *render);

    /**
     * @brief Shutdown and cleanup all pooled render targets
     */
    void shutdown();

    RID acquire(const RenderTargetSpec &spec);

    IRenderTarget *get(RID rtID);

    /**
     * @brief Get the current frame index (for internal use)
     */
    uint32_t getCurrentFrameIndex() const { return _currentFrameIndex; }

    void release(RID rtID);

    /**
     * @brief Called at the start of each frame
     * Handles transient RT cleanup and frame index advancement
     */
    void beginFrame();

    /**
     * @brief Called when window/screen size changes
     * Re-creates resizable render targets
     */
    void onWindowResized(uint32_t newWidth, uint32_t newHeight);

    /**
     * @brief Get statistics for debugging
     */
    void getStats(uint32_t &outTotal, uint32_t &outInUse, uint32_t &outAvailable);

    /**
     * @brief Force garbage collection - removes unused RTs exceeding maxAgeFrames
     * @param maxAgeFrames Remove RTs unused for this many frames
     */
    void garbageCollect(uint32_t maxAgeFrames = 60);

    // RenderGUI for debugging
    void onRenderGUI();

  private:
    RenderTargetPool()           = default;
    ~RenderTargetPool() override = default;

    RenderTargetPool(const RenderTargetPool &)            = delete;
    RenderTargetPool &operator=(const RenderTargetPool &) = delete;

    // Internal helpers
    std::shared_ptr<IRenderTarget> createRenderTarget(const RenderTargetSpec &spec);
    std::string                    generateCacheKey(const RenderTargetSpec &spec);
    void                           findOrCreateMatch(const RenderTargetSpec &spec, PooledRenderTarget *&outEntry, bool &bCreated);
};


} // namespace ya