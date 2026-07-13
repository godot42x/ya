#pragma once

#include <array>

namespace ya
{

struct Texture;

struct DeferredGBufferResources
{
    std::array<Texture*, 4> color{};
    Texture*                depth = nullptr;

    [[nodiscard]] bool isComplete() const
    {
        return color[0] != nullptr &&
               color[1] != nullptr &&
               color[2] != nullptr &&
               color[3] != nullptr &&
               depth != nullptr;
    }
};

} // namespace ya
