// Minimal GUI host: creates a window, initializes the Vulkan backend, loads
// the builtin texture library and a runtime font, draws a sprite and a text
// line with the GUI Draw2D renderer for a bounded number of frames, then
// shuts everything down cleanly. Exercises the GUI product closure only.

#include "Core/FName.h"
#include "Core/Common/DeferredDeletionQueue.h"
#include "Core/System/VirtualFileSystem.h"

#include "RHI/Render.h"
#include "RHI/RenderDefines.h"
#include "RHI/Shader.h"
#include "RHI/WindowProvider.h"
#include "RHI/Backend/TextureLibrary.h"
#include "RHI/Backend/Vulkan/VulkanSwapChain.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderImage.h"

#include "GUI/Resources/FontManager.h"
#include "GUI/Draw2D/Render2D.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace ya;

namespace
{

constexpr uint32_t WINDOW_WIDTH  = 1024;
constexpr uint32_t WINDOW_HEIGHT = 768;

uint64_t parseExitAfterFrame(int argc, char** argv)
{
    uint64_t exitAfterFrame = 120;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--exit-after-frame" && i + 1 < argc) {
            exitAfterFrame = static_cast<uint64_t>(std::strtoull(argv[++i], nullptr, 10));
        }
    }
    return exitAfterFrame;
}

} // namespace

int main(int argc, char** argv)
{
    const uint64_t exitAfterFrame = parseExitAfterFrame(argc, argv);
    // Host composition: the virtual file system backs shader/font asset
    // reads; it is initialized here exactly like the engine host does.
    VirtualFileSystem::init();

    // 1. Window provider (SDL3 + Vulkan surface).
    SDLWindowProvider window;
    if (!window.init()) {
        return 1;
    }
    if (!window.recreate(WindowCreateInfo{
            .index      = 0,
            .renderAPI  = ERenderAPI::Vulkan,
            .title      = "YA Minimal GUI Host",
            .width      = WINDOW_WIDTH,
            .height     = WINDOW_HEIGHT,
            .scale      = 1.0f,
            .bResizable = true,
        })) {
        return 1;
    }

    // 2. Shader compile/cache service (Slang processor serves the GUI
    //    Sprite2D shaders; injected into the backend before pipeline build).
    auto shaderProcessor = ShaderProcessorFactory()
                               .withProcessorType(ShaderProcessorFactory::EProcessorType::Slang)
                               .withShaderStoragePath("Engine/Shader/Slang")
                               .withCachedStoragePath("Engine/Intermediate/Shader/Slang")
                               .FactoryNew<SlangProcessor>();
    auto shaderStorage = std::make_shared<ShaderStorage>(shaderProcessor);
    shaderStorage->setSlangProcessor(shaderProcessor);

    // 3. Vulkan backend (the fixed host backend choice).
    RenderCreateInfo renderCI{
        .renderAPI = ERenderAPI::Vulkan,
        .swapchainCI = SwapchainCreateInfo{
            .imageFormat   = EFormat::R8G8B8A8_UNORM,
            .bVsync        = false,
            .minImageCount = 3,
            .width         = WINDOW_WIDTH,
            .height        = WINDOW_HEIGHT,
        },
        .windowProvider = &window,
    };
    IRender* render = IRender::create(renderCI);
    if (!render) {
        YA_CORE_ERROR("Failed to create IRender instance");
        window.destroy();
        return 1;
    }
    render->setShaderStorage(shaderStorage);
    if (!render->init(renderCI)) {
        YA_CORE_ERROR("Failed to initialize render backend");
        render->destroy();
        delete render;
        window.destroy();
        return 1;
    }

    // 4. Builtin textures/samplers and the runtime font.
    TextureLibrary::get().init(render);
    auto font = FontManager::get()->loadFont(
        *render,
        "Engine/Content/Fonts/JetBrainsMono-Medium.ttf",
        DEFAULT_RUNTIME_FONT_NAME,
        DEFAULT_RUNTIME_FONT_SIZE);
    if (!font) {
        YA_CORE_WARN("Failed to load runtime font; text drawing disabled");
    }

    // 5. GUI Draw2D renderer (screen-space sprites, depth-less pipeline),
    //    matching the swapchain's real surface format.
    auto* swapchain = render->getSwapchain()->as<VulkanSwapChain>();
    YA_CORE_ASSERT(swapchain != nullptr, "Minimal GUI host requires VulkanSwapChain");
    Render2D::init(render, swapchain->getFormat(), EFormat::Undefined);

    std::vector<std::shared_ptr<ICommandBuffer>> commandBuffers;
    render->allocateCommandBuffers(render->getSwapchainImageCount(), commandBuffers);

    // Presentation render targets: one imported swapchain image per frame.
    std::vector<std::shared_ptr<RenderImage>> presentationImages(swapchain->getImageCount());
    for (uint32_t i = 0; i < presentationImages.size(); ++i) {
        auto importedImage = render->getResourceFactory()->importImage(ImportedImageDesc{
            .label         = std::format("MinimalGUI_Presentation_{}", i),
            .nativeHandle  = static_cast<void*>(swapchain->getVkImages().at(i)),
            .format        = swapchain->getFormat(),
            .usage         = static_cast<EImageUsage::T>(EImageUsage::ColorAttachment),
            .extent        = {.width = swapchain->getExtent().width, .height = swapchain->getExtent().height, .depth = 1},
            .initialLayout = EImageLayout::Undefined,
            .finalLayout   = EImageLayout::PresentSrcKHR,
        });
        auto imageView = render->getResourceFactory()->createImageView(
            importedImage,
            ImageViewCreateInfo{
                .label          = std::format("MinimalGUI_Presentation_{}_View", i),
                .viewType       = EImageViewType::View2D,
                .aspectFlags    = EImageAspect::Color,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            });
        auto renderImage      = std::make_shared<RenderImage>();
        renderImage->label    = std::format("MinimalGUI_Presentation_{}", i);
        renderImage->image    = std::move(importedImage);
        renderImage->defaultView = std::move(imageView);
        presentationImages[i] = std::move(renderImage);
    }

    // 6. Frame loop: clear event queue, record one Render2D pass, present.
    for (uint64_t frame = 0; frame < exitAfterFrame; ++frame) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                frame = exitAfterFrame;
            }
        }

        int32_t imageIndex = -1;
        if (!render->begin(&imageIndex)) {
            continue;
        }
        if (imageIndex < 0) {
            render->waitIdle();
            continue;
        }

        auto cmdBuf = commandBuffers[static_cast<size_t>(imageIndex)];
        cmdBuf->reset();
        cmdBuf->begin();

        int windowWidth  = 0;
        int windowHeight = 0;
        render->getWindowSize(windowWidth, windowHeight);

        const auto& presentation = presentationImages[static_cast<size_t>(imageIndex)];
        const Extent2D presentExtent{
            .width  = static_cast<uint32_t>(windowWidth),
            .height = static_cast<uint32_t>(windowHeight),
        };

        cmdBuf->retainResource(presentation->getImageShared());
        cmdBuf->retainResource(presentation->getImageViewShared());
        cmdBuf->transitionImageLayoutAuto(presentation->getImage(), EImageLayout::ColorAttachmentOptimal);
        cmdBuf->beginRendering(RenderingInfo{
            .label                         = "MinimalGUI_Present",
            .bExternalTransitionManagement = true,
            .attachments                   = RenderAttachmentSet{
                .renderArea = Rect2D{
                    .pos    = {0.0f, 0.0f},
                    .extent = {static_cast<float>(presentExtent.width), static_cast<float>(presentExtent.height)},
                },
                .layerCount = 1,
                .colors     = {
                    RenderAttachment{
                        .image         = presentation->getImage(),
                        .imageView     = presentation->getImageView(),
                        .loadOp        = EAttachmentLoadOp::Clear,
                        .storeOp       = EAttachmentStoreOp::Store,
                        .clearValue    = ClearValue(0.05f, 0.06f, 0.07f, 1.0f),
                        .initialLayout = EImageLayout::ColorAttachmentOptimal,
                        .finalLayout   = EImageLayout::ColorAttachmentOptimal,
                    },
                },
                .depth = std::nullopt,
            },
        });

        FRender2dContext ctx{
            .cmdBuf       = cmdBuf.get(),
            .windowWidth  = presentExtent.width,
            .windowHeight = presentExtent.height,
            .passDomain   = ERender2DPassDomain::GameUICompositor,
            .cam          = {
                .position       = glm::vec3(0.0f),
                .view           = glm::mat4(1.0f),
                .projection     = glm::mat4(1.0f),
                .viewProjection = glm::mat4(1.0f),
            },
        };
        Render2D::begin(ctx);
        Render2D::makeSprite(glm::vec3(32.0f, 32.0f, 0.0f),
                             glm::vec2(128.0f, 128.0f),
                             TextureLibrary::get().getWhiteTexture(),
                             glm::vec4(0.9f, 0.2f, 0.2f, 1.0f));
        if (font) {
            Render2D::makeText("Hello from YA Minimal GUI Host",
                               glm::vec3(32.0f, 190.0f, 0.0f),
                               glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
                               font.get());
        }
        Render2D::onRender();
        Render2D::end();

        cmdBuf->endRendering();
        cmdBuf->transitionImageLayoutAuto(presentation->getImage(), EImageLayout::PresentSrcKHR);

        cmdBuf->end();
        render->end(imageIndex, {cmdBuf->getHandle()});
    }

    // 7. Clean shutdown, reverse order.
    render->waitIdle();
    Render2D::destroy();
    commandBuffers.clear(); // releases command-buffer resource retention
    presentationImages.clear();
    font.reset(); // releases the font atlas texture reference
    FontManager::get()->clearCache();
    TextureLibrary::get().shutdown();
    DeferredDeletionQueue::get().flushAll();
    render->destroy();
    delete render;
    window.destroy();

    YA_CORE_INFO("Minimal GUI host finished after {} frames", exitAfterFrame);
    return 0;
}
