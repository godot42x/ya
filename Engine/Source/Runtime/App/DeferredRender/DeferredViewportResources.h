#pragma once

namespace ya
{

struct Texture;

struct DeferredViewportResources
{
    Texture* color = nullptr;
    Texture* depth = nullptr;

    [[nodiscard]] bool isComplete() const
    {
        return color != nullptr && depth != nullptr;
    }
};

} // namespace ya
