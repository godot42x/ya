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
    _entries.clear();
    _factory         = nullptr;
    _nextGeneration = 1;
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

        Entry& entry = _entries[request.viewId];
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
