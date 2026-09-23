#include "Render3D/Common/ViewTargetStore.h"

#include "Core/Log.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "RHI/Core/TextureCreateInfo.h"

#include <format>

namespace ya
{

namespace
{

const char* attachmentName(EViewAttachment role)
{
    switch (role) {
    case EViewAttachment::SceneColor: return "SceneColor";
    case EViewAttachment::SceneDepth: return "SceneDepth";
    case EViewAttachment::DisplayColor: return "DisplayColor";
    case EViewAttachment::EntityId: return "EntityId";
    case EViewAttachment::GBuffer0: return "GBuffer0";
    case EViewAttachment::GBuffer1: return "GBuffer1";
    case EViewAttachment::GBuffer2: return "GBuffer2";
    case EViewAttachment::GBuffer3: return "GBuffer3";
    case EViewAttachment::SSAO: return "SSAO";
    case EViewAttachment::BloomExtract: return "BloomExtract";
    case EViewAttachment::BloomBlur: return "BloomBlur";
    case EViewAttachment::BloomComposite: return "BloomComposite";
    case EViewAttachment::Count: break;
    }
    return "Unknown";
}

} // namespace

std::shared_ptr<RenderTexture> ViewTargetAllocation::find(EViewAttachment role) const
{
    const size_t index = static_cast<size_t>(role);
    return index < attachments.size() ? attachments[index] : nullptr;
}

void ViewTargetStore::init(IRenderResourceFactory& factory)
{
    clear();
    _factory = &factory;
}

void ViewTargetStore::clear()
{
    _publications.clear();
    _entries.clear();
    _factory         = nullptr;
    _nextGeneration = 1;
}

void ViewTargetStore::registerView(SceneViewKey key)
{
    if (!key.valid()) {
        YA_CORE_ERROR("Cannot register an invalid scene view key");
        return;
    }

    // Re-registering a live View keeps its allocation: registration is the
    // lifecycle boundary, not a per-frame reset.
    _entries[key.viewId()].key = key;
}

void ViewTargetStore::unregisterView(SceneViewKey key)
{
    if (!key.valid()) {
        return;
    }

    const auto it = _entries.find(key.viewId());
    if (it == _entries.end() || it->second.key != key) {
        return;
    }
    _publications.dropView(key.viewId());
    _entries.erase(it);
}

bool ViewTargetStore::prepare(std::span<const ViewTargetRequest> requests)
{
    if (!_factory) {
        return false;
    }

    for (const ViewTargetRequest& request : requests) {
        if (request.viewId == 0 || request.extent.width == 0 || request.extent.height == 0) {
            continue;
        }

        const auto registered = _entries.find(request.viewId);
        if (registered == _entries.end()) {
            // A request without registration is a producer/plan bug: the View
            // lifecycle is explicit, so absence is not a resize or a create.
            // The recording path already reports the missing lease for it.
            YA_CORE_ERROR("View target request for unregistered view {}", request.viewId);
            continue;
        }

        Entry& entry = registered->second;
        if (entry.allocation && entry.allocation->desc == request) {
            continue;
        }
        auto allocation = createAllocation(request);
        if (!allocation) {
            return false;
        }
        entry.allocation = std::move(allocation);
    }
    return true;
}

ViewTargetLease ViewTargetStore::lease(SceneViewId viewId) const
{
    const auto it = _entries.find(viewId);
    return it != _entries.end() ? ViewTargetLease{.allocation = it->second.allocation} : ViewTargetLease{};
}

std::shared_ptr<ViewTargetAllocation> ViewTargetStore::createAllocation(const ViewTargetRequest& request)
{
    auto allocation        = std::make_shared<ViewTargetAllocation>();
    allocation->desc       = request;
    allocation->generation = _nextGeneration++;

    for (const ViewAttachmentDesc& attachment : request.attachments) {
        const size_t index = static_cast<size_t>(attachment.role);
        if (index >= allocation->attachments.size() || allocation->attachments[index]) {
            YA_CORE_ERROR("Invalid duplicate View attachment role {} for view {}", index, request.viewId);
            return nullptr;
        }
        auto texture = RenderTexture::create(*_factory, RenderTextureCreateInfo{
            .label   = std::format("View{}.{}", request.viewId, attachmentName(attachment.role)),
            .width   = request.extent.width,
            .height  = request.extent.height,
            .format  = attachment.format,
            .usage   = attachment.usage,
            .samples = attachment.samples,
            .isDepth = attachment.isDepth,
        });
        if (!texture) {
            YA_CORE_ERROR("Failed to allocate View attachment {} for view {}", attachmentName(attachment.role), request.viewId);
            return nullptr;
        }
        allocation->attachments[index] = std::move(texture);
    }
    return allocation;
}

} // namespace ya
