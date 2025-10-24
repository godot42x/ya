#include "WindowProvider.h"

#if USE_VULKAN
    #include "SDL3/SDL_vulkan.h"
#endif
namespace ya
{


#if USE_VULKAN
bool SDLWindowProvider::onCreateVkSurface(VkInstance instance, VkSurfaceKHR *surface)
{
    if (!SDL_Vulkan_CreateSurface(static_cast<SDL_Window *>(nativeWindowHandle),
                                  instance,
                                  nullptr, // if needed
                                  surface))
    {
        YA_CORE_ERROR("Failed to create Vulkan surface: {}", SDL_GetError());
        return false;
    }
    YA_CORE_INFO("Vulkan surface created successfully.");
    return true;
}

void SDLWindowProvider::onDestroyVkSurface(VkInstance instance, VkSurfaceKHR *surface)
{
    SDL_Vulkan_DestroySurface(instance, *surface, nullptr); // if needed
    YA_CORE_INFO("Vulkan surface destroyed successfully.");
}

std::vector<const char *> SDLWindowProvider::onGetVkInstanceExtensions()
{
    Uint32 count = 0;
    // VK_KHR_win32_surface
    const char *const *extensions = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!extensions) {
        YA_CORE_ERROR("Failed to get Vulkan instance extensions: {}", SDL_GetError());
        return {};
    }
    return std::vector<const char *>(extensions, extensions + count);
}
#endif
} // namespace ya
