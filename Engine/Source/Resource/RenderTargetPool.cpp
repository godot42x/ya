#include "RenderTargetPool.h"

#include "Core/App/App.h"
#include "Core/Log.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Core/IRenderTarget.h"
#include "imgui.h"

#include <algorithm>
#include <sstream>

namespace ya
{

RenderTargetPool &RenderTargetPool::get()
{
    static RenderTargetPool instance;
    return instance;
}

void RenderTargetPool::init(IRender *render)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (_bInitialized) {
        YA_CORE_WARN("RenderTargetPool already initialized");
        return;
    }

    _render = render;

    // Get initial window size
    int winW = 0, winH = 0;
    _render->getWindowSize(winW, winH);
    _windowWidth  = static_cast<uint32_t>(winW);
    _windowHeight = static_cast<uint32_t>(winH);

    _bInitialized = true;
    YA_CORE_INFO("RenderTargetPool initialized with window size {}x{}", _windowWidth, _windowHeight);
}

void RenderTargetPool::shutdown()
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (!_bInitialized) {
        return;
    }

    // Clean up all entries
    for (auto &[rtID, entry] : _entries) {
        if (entry->renderTarget) {
            entry->renderTarget->destroy();
            entry->renderTarget.reset();
        }
    }

    _entries.clear();
    _availablePool.clear();
    _transientRTs.clear();
    _resizableRTs.clear();

    _render       = nullptr;
    _bInitialized = false;

    YA_CORE_INFO("RenderTargetPool shutdown complete");
}

void RenderTargetPool::clearCache()
{
    garbageCollect(0); // Collect all
}

std::string RenderTargetPool::generateCacheKey(const RenderTargetSpec &spec)
{
    // Create a unique key based on format and usage
    std::stringstream ss;
    ss << spec.format << "_"
       << spec.extent.width << "x" << spec.extent.height << "_"
       << spec.mipLevels << "_"
       << spec.samples << "_"
       << spec.usage << "_"
       << spec.layerCount;
    return ss.str();
}

std::shared_ptr<IRenderTarget> RenderTargetPool::createRenderTarget(const RenderTargetSpec &spec)
{
    if (!_render) {
        YA_CORE_ERROR("RenderTargetPool not initialized!");
        return nullptr;
    }

    // Resolve extent (0 means use window size)
    Extent2D resolvedExtent = spec.extent;
    if (resolvedExtent.width == 0) {
        resolvedExtent.width = _windowWidth;
    }
    if (resolvedExtent.height == 0) {
        resolvedExtent.height = _windowHeight;
    }

    // Determine if depth is needed (check usage flags)
    bool bHasDepth = (spec.usage & EImageUsage::DepthStencilAttachment) != 0;

    // Build attachment descriptions
    std::vector<AttachmentDescription> colorAttachments;
    AttachmentDescription              depthAttachment;

    // Color attachment
    colorAttachments.push_back({
        .index          = 0,
        .format         = spec.format,
        .samples        = spec.samples,
        .loadOp         = EAttachmentLoadOp::Clear,
        .storeOp        = EAttachmentStoreOp::Store,
        .stencilLoadOp  = EAttachmentLoadOp::DontCare,
        .stencilStoreOp = EAttachmentStoreOp::DontCare,
        .initialLayout  = spec.initialLayout,
        .finalLayout    = EImageLayout::ShaderReadOnlyOptimal, // Default
        .usage          = spec.usage,
    });

    // Depth attachment if needed
    if (bHasDepth) {
        depthAttachment = {
            .index          = 1,
            .format         = EFormat::D32_SFLOAT_S8_UINT, // TODO: make configurable
            .samples        = spec.samples,
            .loadOp         = EAttachmentLoadOp::Clear,
            .storeOp        = EAttachmentStoreOp::Store,
            .stencilLoadOp  = EAttachmentLoadOp::DontCare,
            .stencilStoreOp = EAttachmentStoreOp::DontCare,
            .initialLayout  = EImageLayout::Undefined,
            .finalLayout    = EImageLayout::DepthStencilAttachmentOptimal,
            .usage          = EImageUsage::DepthStencilAttachment,
        };
    }

    // Create the render target
    auto rt = ya::createRenderTarget({
        .label            = spec.label,
        .renderingMode    = ERenderingMode::DynamicRendering, // Pool always uses dynamic rendering
        .bSwapChainTarget = false,
        .extent           = resolvedExtent,
        .frameBufferCount = spec.frameLatency, // For multi-buffering
        .attachments      = {
                 .colorAttach = colorAttachments,
                 .depthAttach = depthAttachment,
        },
    });

    return rt;
}

void RenderTargetPool::findOrCreateMatch(const RenderTargetSpec &spec, PooledRenderTarget *&outEntry, bool &bCreated)
{
    bCreated             = false;
    std::string cacheKey = generateCacheKey(spec);

    // Try to find an available match
    auto &availableList = _availablePool[cacheKey];
    for (auto it = availableList.begin(); it != availableList.end(); ++it) {
        PooledRenderTarget *entry = *it;
        if (!entry->bInUse) {
            // Found a match
            entry->bInUse        = true;
            entry->lastUsedFrame = _currentFrameIndex;
            availableList.erase(it);
            outEntry = entry;
            return;
        }
    }

    // No match found, create new
    auto entry           = std::make_unique<PooledRenderTarget>();
    entry->rtID          = {.id = _nextRTid++};
    entry->spec          = spec;
    entry->renderTarget  = createRenderTarget(spec);
    entry->lastUsedFrame = _currentFrameIndex;
    entry->bInUse        = true;
    entry->debugName     = FName(spec.label);

    outEntry              = entry.get();
    _entries[entry->rtID] = std::move(entry);
    bCreated              = true;
}

RID RenderTargetPool::acquire(const RenderTargetSpec &spec)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (!_bInitialized) {
        YA_CORE_ERROR("RenderTargetPool not initialized! Call init() first.");
        return RID{.id = 0};
    }

    PooledRenderTarget *entry    = nullptr;
    bool                bCreated = false;

    findOrCreateMatch(spec, entry, bCreated);

    if (!entry || !entry->renderTarget) {
        YA_CORE_ERROR("Failed to acquire render target!");
        return RID{0};
    }

    // Track transient RTs
    if (spec.bTransient) {
        _transientRTs.push_back(entry->rtID);
    }

    // Track resizable RTs
    if (spec.bResizable) {
        // Only add if not already tracking
        bool bAlreadyTracking = false;
        for (RID tracked : _resizableRTs) {
            if (tracked == entry->rtID) {
                bAlreadyTracking = true;
                break;
            }
        }
        if (!bAlreadyTracking) {
            _resizableRTs.push_back(entry->rtID);
        }
    }

    if (bCreated) {
        YA_CORE_DEBUG("RenderTargetPool: Created new RT {} ({})", entry->rtID.id, spec.label);
    }
    else {
        YA_CORE_TRACE("RenderTargetPool: Reused RT {} ({})", entry->rtID.id, spec.label);
    }

    return entry->rtID;
}

IRenderTarget *RenderTargetPool::get(RID rtID)
{

    auto it = _entries.find(rtID);
    if (it == _entries.end()) {
        return nullptr;
    }

    return it->second->renderTarget.get();
}

void RenderTargetPool::release(RID rtID)
{

    auto it = _entries.find(rtID);
    if (it == _entries.end()) {
        return;
    }

    auto &entry = it->second;
    if (entry->bInUse) {
        entry->bInUse        = false;
        entry->lastUsedFrame = _currentFrameIndex;

        // Add back to available pool
        std::string cacheKey = generateCacheKey(entry->spec);
        _availablePool[cacheKey].push_back(entry.get());

        YA_CORE_TRACE("RenderTargetPool: Released RT {} to pool", rtID.id);
    }
}

void RenderTargetPool::beginFrame()
{

    _currentFrameIndex++;

    // Auto-release transient RTs
    if (!_transientRTs.empty()) {
        for (RID rtID : _transientRTs) {
            auto it = _entries.find(rtID);
            if (it != _entries.end()) {
                auto &entry = it->second;
                if (entry->bInUse) {
                    entry->bInUse        = false;
                    entry->lastUsedFrame = _currentFrameIndex;

                    std::string cacheKey = generateCacheKey(entry->spec);
                    _availablePool[cacheKey].push_back(entry.get());
                }
            }
        }

        YA_CORE_TRACE("RenderTargetPool: Auto-released {} transient RTs", _transientRTs.size());
        _transientRTs.clear();
    }
}

void RenderTargetPool::onWindowResized(uint32_t newWidth, uint32_t newHeight)
{

    if (newWidth == _windowWidth && newHeight == _windowHeight) {
        return;
    }

    YA_CORE_INFO("RenderTargetPool: Window resized from {}x{} to {}x{}",
                 _windowWidth,
                 _windowHeight,
                 newWidth,
                 newHeight);

    _windowWidth  = newWidth;
    _windowHeight = newHeight;

    // Recreate all resizable RTs
    for (RID rtID : _resizableRTs) {
        auto it = _entries.find(rtID);
        if (it == _entries.end()) {
            continue;
        }

        auto &entry = it->second;

        // Only recreate if extent was 0 (auto-size) or if we want all resizable
        if (entry->spec.extent.width == 0 || entry->spec.extent.height == 0 || entry->spec.bResizable) {
            // Mark for recreation
            if (entry->bInUse) {
                YA_CORE_WARN("RenderTargetPool: Resized RT {} is still in use!", rtID.id);
            }

            // Save old ID and create new one
            RID              oldId   = entry->rtID;
            RenderTargetSpec oldSpec = entry->spec;

            // Destroy old
            if (entry->renderTarget) {
                entry->renderTarget->destroy();
            }

            // Create new with updated size
            entry->renderTarget  = createRenderTarget(oldSpec);
            entry->lastUsedFrame = _currentFrameIndex;

            YA_CORE_DEBUG("RenderTargetPool: Recreated RT {} ({}x{})",
                          oldId.id,
                          _windowWidth,
                          _windowHeight);
        }
    }

    // Clear available pool since sizes changed
    _availablePool.clear();
}

void RenderTargetPool::getStats(uint32_t &outTotal, uint32_t &outInUse, uint32_t &outAvailable)
{
    outTotal     = static_cast<uint32_t>(_entries.size());
    outInUse     = 0;
    outAvailable = 0;

    for (const auto &[rtID, entry] : _entries) {
        if (entry->bInUse) {
            outInUse++;
        }
        else {
            outAvailable++;
        }
    }
}

void RenderTargetPool::garbageCollect(uint32_t maxAgeFrames)
{
    std::lock_guard<std::mutex> lock(_mutex);

    uint32_t removedCount = 0;

    for (auto it = _entries.begin(); it != _entries.end();) {
        auto &entry = it->second;

        // Don't remove in-use entries
        if (entry->bInUse) {
            ++it;
            continue;
        }

        // Check age
        uint64_t age = _currentFrameIndex - entry->lastUsedFrame;
        if (age > maxAgeFrames) {
            // Remove from available pool first
            std::string cacheKey      = generateCacheKey(entry->spec);
            auto       &availableList = _availablePool[cacheKey];
            availableList.erase(
                std::ranges::remove(availableList, entry.get()).begin(),
                availableList.end());

            // Destroy and remove
            if (entry->renderTarget) {
                entry->renderTarget->destroy();
            }

            it = _entries.erase(it);
            removedCount++;
        }
        else {
            ++it;
        }
    }

    if (removedCount > 0) {
        YA_CORE_INFO("RenderTargetPool: Garbage collected {} old render targets", removedCount);
    }
}

void RenderTargetPool::onRenderGUI()
{
    ImGui::PushID("RenderTargetPool");


    uint32_t total, inUse, available;
    getStats(total, inUse, available);

    ImGui::Text("Frame: %llu", _currentFrameIndex);
    ImGui::Text("Total RTs: %u (In Use: %u, Available: %u)", total, inUse, available);
    ImGui::Text("Transient RTs this frame: %zu", _transientRTs.size());
    ImGui::Text("Resizable RTs: %zu", _resizableRTs.size());
    ImGui::Text("Window Size: %ux%u", _windowWidth, _windowHeight);

    if (ImGui::Button("Garbage Collect (60 frames)")) {
        garbageCollect(60);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear All")) {
        clearCache();
    }

    if (ImGui::TreeNode("Active Render Targets")) {
        for (const auto &[rtID, entry] : _entries) {
            ImGui::PushID(static_cast<int>(rtID.id));

            const char *status = entry->bInUse ? "[IN USE]" : "[available]";
            ImGui::Text("RT %llu %s", rtID.id, status);

            ImGui::SameLine();
            if (ImGui::SmallButton("Release")) {
                release(rtID);
            }

            ImGui::Text("  Label: %s", entry->debugName.c_str());
            ImGui::Text("  Format: %d, Extent: %ux%u",
                        entry->spec.format,
                        entry->spec.extent.width ? entry->spec.extent.width : _windowWidth,
                        entry->spec.extent.height ? entry->spec.extent.height : _windowHeight);
            ImGui::Text("  Last used: %llu frames ago", _currentFrameIndex - entry->lastUsedFrame);

            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    ImGui::Unindent();
}

} // namespace ya
