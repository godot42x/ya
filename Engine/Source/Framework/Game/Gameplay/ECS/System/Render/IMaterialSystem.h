#pragma once

#include "Foundation/Core/Base.h"
#include "Foundation/Core/Profiling/Instrumentor.h"
#include "Foundation/Core/System/System.h"

#include "Framework/Game/Gameplay/ECS/System/Render/IRenderSystem.h"
#include "Foundation/RHI/Core/CommandBuffer.h"
#include "glm/mat4x4.hpp"


namespace ya
{

struct IRender;
struct IRenderPass;
struct VulkanRender;
struct App;
struct Scene;
struct FrameContext;

struct IMaterialSystem : public IRenderSystem
{
    IMaterialSystem(const std::string label) : IRenderSystem(label) {}
    virtual ~IMaterialSystem() = default;



    template <typename T>
    T* as()
    {
        static_assert(std::is_base_of_v<IMaterialSystem, T>, "T must be derived from IMaterialSystem");
        return static_cast<T*>(this);
    }

};
} // namespace ya
