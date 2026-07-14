#pragma once

#include "Render/RenderDefines.h"

#include <optional>
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
        std::vector<EFormat::T>          colorFormats{};
        std::optional<EFormat::T>        depthFormat{};
        bool bEditable = true;
    };

    std::vector<Entry> entries;
};

} // namespace ya
