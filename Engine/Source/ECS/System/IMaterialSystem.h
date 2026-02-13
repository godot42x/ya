#pragma once

#include "Core/Base.h"
#include "Core/Debug/Instrumentor.h"
#include "Core/System/System.h"

#include "IRenderSystem.h"
#include "Render/Core/CommandBuffer.h"
#include "glm/mat4x4.hpp"
#include "imgui.h"


namespace ya
{

struct IRender;
struct IRenderPass;
struct VulkanRender;
struct App;
struct IRenderTarget;
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

  protected:
    // void onRenderGUI() override { IRenderSystem::onRenderGUI();}
};
} // namespace ya