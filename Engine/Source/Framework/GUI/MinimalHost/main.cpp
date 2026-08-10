// Minimal GUI host: creates a window, initializes the Vulkan backend, loads
// the builtin texture library and runtime fonts, then runs the complete GUI
// framework loop for a bounded number of frames:
//
//   SDL input  -> WidgetTree event routing (hover / press / click)
//   WidgetTree -> layout + immutable UIFrameSnapshot
//   snapshot   -> Render2D compose pass onto the swapchain presentation target
//
// The window is resizable: swapchain recreation is detected every frame and
// the presentation targets / logical extent are rebuilt accordingly. The
// binary exercises the GUI product closure only (Core/RHI/Vulkan backend +
// the four GUI modules); no ECS/Physics/Resource/RenderGraph/Render3D/Host/
// Editor.

#include "Core/Event.h"
#include "Core/FName.h"
#include "Core/KeyCode.h"
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

#include "GUI/Compose/Render2DComposePass.h"
#include "GUI/Resources/FontManager.h"
#include "GUI/Draw2D/Render2D.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/WidgetTree.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <format>
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
        if (arg == "--exit-after-frame") {
            if (i + 1 < argc) {
                exitAfterFrame = static_cast<uint64_t>(std::strtoull(argv[++i], nullptr, 10));
            }
        }
        else if (arg.starts_with("--exit-after-frame=")) {
            exitAfterFrame = static_cast<uint64_t>(
                std::strtoull(arg.c_str() + std::string("--exit-after-frame=").size(), nullptr, 10));
        }
    }
    return exitAfterFrame;
}

/// Interactive demo content: a panel with a title, a click counter label and
/// a button. The button label is a Pass-filtered child text so hover/press
/// still reach the button underneath.
struct FMinimalUIDemo
{
    std::shared_ptr<UIPanel>  panel;
    std::shared_ptr<UIText>   title;
    std::shared_ptr<UIText>   counter;
    std::shared_ptr<UIButton> button;
    std::shared_ptr<UIText>   buttonLabel;
};

void buildDemoContent(WidgetTree& tree, FMinimalUIDemo& demo)
{
    demo.panel = std::make_shared<UIPanel>("DemoPanel");
    demo.panel->_position = {64.0f, 64.0f};
    demo.panel->_size     = {340.0f, 200.0f};
    demo.panel->_color    = {0.13f, 0.14f, 0.17f, 0.96f};

    demo.title = std::make_shared<UIText>("Title");
    demo.title->_position = {16.0f, 14.0f};
    demo.title->_size     = {308.0f, 30.0f};
    demo.title->_fontSize = 20;
    demo.title->_text     = "YA Minimal GUI Host";
    demo.title->_color    = {1.0f, 1.0f, 1.0f, 1.0f};

    demo.counter = std::make_shared<UIText>("Counter");
    demo.counter->_position = {16.0f, 58.0f};
    demo.counter->_size     = {308.0f, 26.0f};
    demo.counter->_fontSize = 16;
    demo.counter->_text     = "Clicked: 0";
    demo.counter->_color    = {0.85f, 0.87f, 0.90f, 1.0f};

    demo.button = std::make_shared<UIButton>("ClickButton");
    demo.button->_position = {16.0f, 100.0f};
    demo.button->_size     = {150.0f, 44.0f};
    demo.button->_normalColor  = {0.22f, 0.48f, 0.86f, 1.0f};
    demo.button->_hoveredColor = {0.32f, 0.58f, 0.96f, 1.0f};
    demo.button->_pressedColor = {0.14f, 0.34f, 0.66f, 1.0f};

    demo.buttonLabel = std::make_shared<UIText>("ButtonLabel");
    demo.buttonLabel->_size     = {150.0f, 44.0f};
    demo.buttonLabel->_fontSize = 16;
    demo.buttonLabel->_text     = "Click me";
    demo.buttonLabel->_color    = {1.0f, 1.0f, 1.0f, 1.0f};
    demo.buttonLabel->_hAlign   = EWidgetAlignH::Center;
    demo.buttonLabel->_vAlign   = EWidgetAlignV::Center;

    // Shared state captured by value: the lambda stays valid even if the demo
    // struct goes out of scope (the tree owns the widgets either way).
    auto clickCount = std::make_shared<uint32_t>(0);
    demo.button->_onClick = [counter = demo.counter, clickCount]() {
        ++(*clickCount);
        counter->_text = std::format("Clicked: {}", *clickCount);
        YA_CORE_INFO("Minimal host button clicked (count {})", *clickCount);
    };

    tree.attachToLayer(WidgetTree::ELayer::Content, demo.panel);
    tree.attach(*demo.panel, demo.title);
    tree.attach(*demo.panel, demo.counter);
    tree.attach(*demo.panel, demo.button);
    tree.attach(*demo.button, demo.buttonLabel);
}

void dispatchToTree(WidgetTree& tree, const Event& event, float mouseX, float mouseY)
{
    WidgetEventContext ctx;
    ctx.logicalPoint = {mouseX, mouseY};
    tree.dispatchEvent(event, ctx);
}

/// Import the current swapchain images as render targets. Called at startup
/// and again after every swapchain recreation (resize / restore).
void buildPresentationTargets(IRender&                      render,
                              VulkanSwapChain&              swapchain,
                              std::vector<std::shared_ptr<RenderImage>>& outImages)
{
    outImages.clear();
    outImages.reserve(swapchain.getImageCount());
    for (uint32_t i = 0; i < swapchain.getImageCount(); ++i) {
        auto importedImage = render.getResourceFactory()->importImage(ImportedImageDesc{
            .label         = std::format("MinimalGUI_Presentation_{}", i),
            .nativeHandle  = static_cast<void*>(swapchain.getVkImages().at(i)),
            .format        = swapchain.getFormat(),
            .usage         = static_cast<EImageUsage::T>(EImageUsage::ColorAttachment),
            .extent        = {.width = swapchain.getExtent().width, .height = swapchain.getExtent().height, .depth = 1},
            .initialLayout = EImageLayout::Undefined,
            .finalLayout   = EImageLayout::PresentSrcKHR,
        });
        auto imageView = render.getResourceFactory()->createImageView(
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
        auto renderImage        = std::make_shared<RenderImage>();
        renderImage->label      = std::format("MinimalGUI_Presentation_{}", i);
        renderImage->image      = std::move(importedImage);
        renderImage->defaultView = std::move(imageView);
        outImages.push_back(std::move(renderImage));
    }
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

    // 4. Builtin textures/samplers and the runtime fonts (one atlas entry per
    //    size used by the demo; UIText resolves fonts by exact name+size).
    TextureLibrary::get().init(render);
    const char* fontPath = "Engine/Content/Fonts/JetBrainsMono-Medium.ttf";
    if (!FontManager::get()->loadFont(*render, fontPath, DEFAULT_RUNTIME_FONT_NAME, 20) ||
        !FontManager::get()->loadFont(*render, fontPath, DEFAULT_RUNTIME_FONT_NAME, 16)) {
        YA_CORE_WARN("Failed to load runtime font; text drawing disabled");
    }

    // 5. GUI Draw2D renderer (screen-space sprites, depth-less pipeline),
    //    matching the swapchain's real surface format.
    auto* swapchain = render->getSwapchain()->as<VulkanSwapChain>();
    YA_CORE_ASSERT(swapchain != nullptr, "Minimal GUI host requires VulkanSwapChain");
    Render2D::init(render, swapchain->getFormat(), EFormat::Undefined);

    // 5b. Game UI WidgetTree closure: layout + immutable snapshot without any
    //     Scene / ECS / Host / Render3D dependency. The widgets module is the
    //     standalone Game UI fact source; SDL input is routed into the same
    //     tree that produces the snapshot.
    WidgetTree     widgetTree(Extent2D{WINDOW_WIDTH, WINDOW_HEIGHT});
    FMinimalUIDemo demo;
    buildDemoContent(widgetTree, demo);

    std::vector<std::shared_ptr<ICommandBuffer>> commandBuffers;
    render->allocateCommandBuffers(render->getSwapchainImageCount(), commandBuffers);

    // Presentation render targets: one imported swapchain image per frame.
    std::vector<std::shared_ptr<RenderImage>> presentationImages;
    buildPresentationTargets(*render, *swapchain, presentationImages);

    // 6. Frame loop: SDL events -> WidgetTree -> snapshot -> compose -> present.
    Extent2D cachedSwapchainExtent = swapchain->getExtent();
    bool     bRunning              = true;
    bool     bLoggedFirstSnapshot  = false;
    for (uint64_t frame = 0; frame < exitAfterFrame && bRunning; ++frame) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                bRunning = false;
                break;
            case SDL_EVENT_MOUSE_MOTION: {
                MouseMoveEvent ev(event.motion.x, event.motion.y);
                dispatchToTree(widgetTree, ev, event.motion.x, event.motion.y);
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                MouseButtonPressedEvent ev(event.button.button);
                dispatchToTree(widgetTree, ev, event.button.x, event.button.y);
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                MouseButtonReleasedEvent ev(event.button.button);
                dispatchToTree(widgetTree, ev, event.button.x, event.button.y);
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL: {
                MouseScrolledEvent ev(event.wheel.x, event.wheel.y);
                dispatchToTree(widgetTree, ev, -1.0f, -1.0f);
                break;
            }
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE) {
                    bRunning = false;
                    break;
                }
                {
                    KeyPressedEvent ev;
                    ev._keyCode = EKey::fromSDLKeycode(event.key.key);
                    ev._mod     = event.key.mod;
                    ev.bRepeat  = event.key.repeat;
                    dispatchToTree(widgetTree, ev, -1.0f, -1.0f);
                }
                break;
            case SDL_EVENT_KEY_UP: {
                KeyReleasedEvent ev;
                ev._keyCode = EKey::fromSDLKeycode(event.key.key);
                ev._mod     = event.key.mod;
                dispatchToTree(widgetTree, ev, -1.0f, -1.0f);
                break;
            }
            default:
                break;
            }
        }
        if (!bRunning) {
            break;
        }

        int32_t imageIndex = -1;
        if (!render->begin(&imageIndex)) {
            continue;
        }
        if (imageIndex < 0) {
            render->waitIdle();
            continue;
        }

        // Swapchain recreated (resize / restore): rebuild the presentation
        // targets and re-map the tree to the new logical extent before
        // recording. `render->begin` applies the recreation, so compare
        // against the swapchain's current image count and extent.
        swapchain = render->getSwapchain()->as<VulkanSwapChain>();
        const Extent2D swapchainExtent = swapchain->getExtent();
        if (render->getSwapchainImageCount() != presentationImages.size() ||
            swapchainExtent.width != cachedSwapchainExtent.width ||
            swapchainExtent.height != cachedSwapchainExtent.height) {
            render->waitIdle();
            commandBuffers.clear();
            render->allocateCommandBuffers(render->getSwapchainImageCount(), commandBuffers);
            buildPresentationTargets(*render, *swapchain, presentationImages);
            widgetTree.setLogicalExtent(swapchainExtent);
            cachedSwapchainExtent = swapchainExtent;
            continue; // retry the next frame with the new swapchain
        }

        // All pipeline changes and the immutable UI snapshot must be ready
        // before command recording starts.
        prepareRender2DComposePassPipeline(
            FRender2DComposePassDesc{
                .kind = ERender2DComposePassKind::RuntimeUIComposite,
            },
            swapchain->getFormat());
        const UIFrameSnapshot snapshot = widgetTree.buildSnapshot(UIFrameBuildContext{});
        if (!bLoggedFirstSnapshot) {
            bLoggedFirstSnapshot = true;
            YA_CORE_INFO("Minimal host first snapshot: {} draw items, {}x{} logical",
                         snapshot.items.size(),
                         snapshot.logicalExtent.width,
                         snapshot.logicalExtent.height);
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

        // Clear the swapchain image first (the RuntimeUIComposite pass loads
        // the target because the game path composites over the world image).
        cmdBuf->retainResource(presentation->getImageShared());
        cmdBuf->retainResource(presentation->getImageViewShared());
        cmdBuf->transitionImageLayoutAuto(presentation->getImage(), EImageLayout::ColorAttachmentOptimal);
        cmdBuf->beginRendering(RenderingInfo{
            .label                         = "MinimalGUI_Clear",
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
        cmdBuf->endRendering();

        // Compose the immutable UI snapshot over the cleared target.
        recordRender2DComposePass(
            cmdBuf.get(),
            *presentation,
            /*depthTarget=*/nullptr,
            &snapshot,
            FRender2DComposePassDesc{
                .kind                   = ERender2DComposePassKind::RuntimeUIComposite,
                .logicalViewportExtent  = widgetTree.getLogicalExtent(),
                .finalLayout            = EImageLayout::PresentSrcKHR,
            });

        cmdBuf->end();
        render->end(imageIndex, {cmdBuf->getHandle()});
    }

    // 7. Clean shutdown, reverse order.
    render->waitIdle();
    Render2D::destroy();
    commandBuffers.clear(); // releases command-buffer resource retention
    presentationImages.clear();
    FontManager::get()->clearCache();
    TextureLibrary::get().shutdown();
    DeferredDeletionQueue::get().flushAll();
    render->destroy();
    delete render;
    window.destroy();

    YA_CORE_INFO("Minimal GUI host finished after {} frames", exitAfterFrame);
    return 0;
}
