#pragma once

#include <vector>

namespace ya
{

struct IRenderTarget;

struct RenderTargetEditorCatalog
{
    struct Entry
    {
        const char*    label = "";
        IRenderTarget* rt    = nullptr;
        enum class EOwner
        {
            Presentation,
            ForwardViewport,
            ForwardShadow,
            DeferredGBuffer,
            DeferredViewport,
            DeferredShadow,
        } owner = EOwner::Presentation;
        bool bEditable = true;
    };

    std::vector<Entry> entries;
};

} // namespace ya
