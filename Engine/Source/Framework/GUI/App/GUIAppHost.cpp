#include "GUI/App/GUIAppHost.h"
#include "GUI/App/GUIPresentationTarget.h"

#include "Core/FName.h"
#include "Core/KeyCode.h"
#include "Core/Log.h"
#include "Core/Common/DeferredDeletionQueue.h"
#include "Core/System/VirtualFileSystem.h"

#include "RHI/Render.h"
#include "RHI/RenderDefines.h"
#include "RHI/Shader.h"
#include "RHI/WindowProvider.h"
#include "RHI/Backend/TextureLibrary.h"
#include "RHI/Backend/Vulkan/VulkanSwapChain.h"
#include "RHI/Core/CommandBuffer.h"

#include "GUI/Compose/Render2DComposePass.h"
#include "GUI/Resources/FontManager.h"
#include "GUI/Draw2D/Render2D.h"

#include <SDL3/SDL.h>

#include <format>
#include <vector>

namespace ya
{

namespace
{

constexpr uint32_t DEFAULT_WINDOW_WIDTH  = 1024;
constexpr uint32_t DEFAULT_WINDOW_HEIGHT = 768;

} // namespace

struct GUIAppHost::FImpl
{
    const FGUIAppHostConfig* config = nullptr;
    IGUIAppDelegate*         delegate = nullptr;

    SDLWindowProvider        window;
    IRender*                 render  = nullptr;
    std::shared_ptr<ShaderStorage> shaderStorage;
    std::unique_ptr<WidgetTree> tree;

    std::vector<std::shared_ptr<ICommandBuffer>>       commandBuffers;
    std::vector<std::shared_ptr<GUIPresentationTarget>> presentationTargets;
    Extent2D cachedSwapchainExtent{};
    float    lastMouseX = -1.0f;
    float    lastMouseY = -1.0f;
    bool     bInitialized = false;
};

GUIAppHost::GUIAppHost(const FGUIAppHostConfig& config, IGUIAppDelegate& delegate)
    : _impl(std::make_unique<FImpl>())
{
    _impl->config   = &config;
    _impl->delegate = &delegate;
}

GUIAppHost::~GUIAppHost()
{
    shutdown();
}

bool GUIAppHost::init()
{
    if (_impl->bInitialized) {
        return true;
    }

    const FGUIAppHostConfig& config = *_impl->config;

    // Host composition: the virtual file system backs shader/font asset
    // reads; initialized here exactly like the engine host does.
    VirtualFileSystem::init();

    // 1. Window provider (SDL3 + Vulkan surface).
    SDLWindowProvider& window = _impl->window;
    if (!window.init()) {
        return false;
    }
    if (!window.recreate(WindowCreateInfo{
            .index      = 0,
            .renderAPI  = ERenderAPI::Vulkan,
            .title      = config.title,
            .width      = config.width != 0 ? config.width : DEFAULT_WINDOW_WIDTH,
            .height     = config.height != 0 ? config.height : DEFAULT_WINDOW_HEIGHT,
            .scale      = config.scale,
            .bResizable = config.bResizable,
        })) {
        window.destroy();
        return false;
    }
    // Enable Unicode text input (SDL_EVENT_TEXT_INPUT -> KeyTypedEvent) so
    // focused text fields can edit; the events are routed like every other
    // keyboard event.
    SDL_StartTextInput(static_cast<SDL_Window*>(window.getNativeWindowHandle()));

    // 2. Shader compile/cache service (Slang processor serves the GUI
    //    Sprite2D shaders; injected into the backend before pipeline build).
    auto shaderProcessor = ShaderProcessorFactory()
                               .withProcessorType(ShaderProcessorFactory::EProcessorType::Slang)
                               .withShaderStoragePath("Engine/Shader/Slang")
                               .withCachedStoragePath("Engine/Intermediate/Shader/Slang")
                               .FactoryNew<SlangProcessor>();
    _impl->shaderStorage = std::make_shared<ShaderStorage>(shaderProcessor);
    _impl->shaderStorage->setSlangProcessor(shaderProcessor);

    // 3. Vulkan backend (the fixed host backend choice).
    RenderCreateInfo renderCI{
        .renderAPI = ERenderAPI::Vulkan,
        .swapchainCI = SwapchainCreateInfo{
            .imageFormat   = EFormat::R8G8B8A8_UNORM,
            .bVsync        = false,
            .minImageCount = 3,
            .width         = config.width != 0 ? config.width : DEFAULT_WINDOW_WIDTH,
            .height        = config.height != 0 ? config.height : DEFAULT_WINDOW_HEIGHT,
        },
        .windowProvider = &window,
    };
    IRender* render = IRender::create(renderCI);
    if (!render) {
        YA_CORE_ERROR("GUIAppHost: failed to create IRender instance");
        window.destroy();
        return false;
    }
    _impl->render = render;
    render->setShaderStorage(_impl->shaderStorage);
    if (!render->init(renderCI)) {
        YA_CORE_ERROR("GUIAppHost: failed to initialize render backend");
        render->destroy();
        delete render;
        _impl->render = nullptr;
        window.destroy();
        return false;
    }

    // 4. Builtin textures/samplers and the runtime fonts (one atlas entry per
    //    configured size; UIText resolves fonts by exact name+size).
    TextureLibrary::get().init(render);
    for (const uint32_t fontSize : config.fontSizes) {
        if (!FontManager::get()->loadFont(*render, config.fontPath, DEFAULT_RUNTIME_FONT_NAME, fontSize)) {
            YA_CORE_WARN("GUIAppHost: failed to load runtime font '{}' at size {}; text drawing disabled",
                         config.fontPath, fontSize);
        }
    }

    // 5. GUI Draw2D renderer (screen-space sprites, depth-less pipeline),
    //    matching the swapchain's real surface format.
    auto* swapchain = render->getSwapchain()->as<VulkanSwapChain>();
    YA_CORE_ASSERT(swapchain != nullptr, "GUIAppHost requires a VulkanSwapChain");
    Render2D::init(render, swapchain->getFormat(), EFormat::Undefined);

    // 5b. Game UI WidgetTree closure: layout + immutable snapshot without any
    //     Scene / ECS / Host / Render3D dependency. SDL input is routed into
    //     the same tree that produces the snapshot.
    _impl->tree = std::make_unique<WidgetTree>(Extent2D{
        .width  = swapchain->getExtent().width,
        .height = swapchain->getExtent().height,
    });
    _impl->delegate->buildUI(*_impl->tree);

    render->allocateCommandBuffers(render->getSwapchainImageCount(), _impl->commandBuffers);

    // Presentation render targets: one imported swapchain image per frame.
    GUIPresentationTarget::buildAll(*render, *swapchain, "GUIApp", _impl->presentationTargets);
    _impl->cachedSwapchainExtent = swapchain->getExtent();

    _impl->bInitialized = true;
    return true;
}

void GUIAppHost::pumpEvents(bool& bRunning)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            bRunning = false;
            break;
        case SDL_EVENT_MOUSE_MOTION: {
            MouseMoveEvent ev(event.motion.x, event.motion.y);
            _impl->lastMouseX = event.motion.x;
            _impl->lastMouseY = event.motion.y;
            dispatchToTree(ev, event.motion.x, event.motion.y);
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            MouseButtonPressedEvent ev(event.button.button);
            dispatchToTree(ev, event.button.x, event.button.y);
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            MouseButtonReleasedEvent ev(event.button.button);
            dispatchToTree(ev, event.button.x, event.button.y);
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            MouseScrolledEvent ev(event.wheel.x, event.wheel.y);
            // Wheel events carry no position in SDL; route them at the last
            // known pointer position so the tree's hit walk reaches the
            // innermost scrollable viewport.
            dispatchToTree(ev, _impl->lastMouseX, _impl->lastMouseY);
            break;
        }
        case SDL_EVENT_KEY_DOWN:
            if (_impl->config->bEscapeQuits && event.key.key == SDLK_ESCAPE) {
                bRunning = false;
                break;
            }
            {
                KeyPressedEvent ev;
                ev._keyCode = EKey::fromSDLKeycode(event.key.key);
                ev._mod     = event.key.mod;
                ev.bRepeat  = event.key.repeat;
                dispatchToTree(ev, -1.0f, -1.0f);
            }
            break;
        case SDL_EVENT_KEY_UP: {
            KeyReleasedEvent ev;
            ev._keyCode = EKey::fromSDLKeycode(event.key.key);
            ev._mod     = event.key.mod;
            dispatchToTree(ev, -1.0f, -1.0f);
            break;
        }
        case SDL_EVENT_TEXT_INPUT: {
            KeyTypedEvent ev(event.text.text);
            dispatchToTree(ev, -1.0f, -1.0f);
            break;
        }
        default:
            break;
        }
    }
}

void GUIAppHost::dispatchToTree(const Event& event, float mouseX, float mouseY)
{
    WidgetEventContext ctx;
    ctx.logicalPoint = {mouseX, mouseY};
    const EWidgetRouteResult result = _impl->tree->dispatchEvent(event, ctx);
    _impl->delegate->onRoutedEvent(event, result);
}

void GUIAppHost::rebuildPresentationResources()
{
    // Frame boundary only: wait for in-flight work, then release command
    // buffers (and their retained resources) and the imported images/views
    // before rebuilding from the current swapchain.
    _impl->render->waitIdle();
    _impl->commandBuffers.clear();
    _impl->presentationTargets.clear();

    auto* swapchain = _impl->render->getSwapchain()->as<VulkanSwapChain>();
    _impl->render->allocateCommandBuffers(_impl->render->getSwapchainImageCount(), _impl->commandBuffers);
    GUIPresentationTarget::buildAll(*_impl->render, *swapchain, "GUIApp", _impl->presentationTargets);
    _impl->tree->setLogicalExtent(swapchain->getExtent());
    _impl->cachedSwapchainExtent = swapchain->getExtent();
}

int GUIAppHost::run(uint64_t exitAfterFrame)
{
    if (!_impl->bInitialized) {
        YA_CORE_ERROR("GUIAppHost::run called before a successful init()");
        return 1;
    }

    bool bRunning             = true;
    bool bLoggedFirstSnapshot = false;
    for (uint64_t frame = 0; frame < exitAfterFrame && bRunning; ++frame) {
        pumpEvents(bRunning);
        if (!bRunning) {
            break;
        }

        int32_t imageIndex = -1;
        if (!_impl->render->begin(&imageIndex)) {
            continue;
        }
        if (imageIndex < 0) {
            _impl->render->waitIdle();
            continue;
        }

        // Swapchain recreated (resize / restore): rebuild the presentation
        // targets and re-map the tree to the new logical extent before
        // recording. `render->begin` applies the recreation, so compare
        // against the swapchain's current image count and extent.
        auto* swapchain = _impl->render->getSwapchain()->as<VulkanSwapChain>();
        const Extent2D swapchainExtent = swapchain->getExtent();
        if (_impl->render->getSwapchainImageCount() != _impl->presentationTargets.size() ||
            swapchainExtent.width != _impl->cachedSwapchainExtent.width ||
            swapchainExtent.height != _impl->cachedSwapchainExtent.height) {
            rebuildPresentationResources();
            continue; // retry the next frame with the new swapchain
        }

        // All pipeline changes and the immutable UI snapshot must be ready
        // before command recording starts.
        prepareRender2DComposePassPipeline(
            FRender2DComposePassDesc{
                .kind = ERender2DComposePassKind::RuntimeUIComposite,
            },
            swapchain->getFormat());
        _impl->delegate->updateUI();
        const UIFrameSnapshot snapshot = _impl->tree->buildSnapshot(UIFrameBuildContext{});
        if (!bLoggedFirstSnapshot) {
            bLoggedFirstSnapshot = true;
            YA_CORE_INFO("GUIAppHost first snapshot: {} draw items, {}x{} logical",
                         snapshot.items.size(),
                         snapshot.logicalExtent.width,
                         snapshot.logicalExtent.height);
        }

        auto cmdBuf = _impl->commandBuffers[static_cast<size_t>(imageIndex)];
        cmdBuf->reset();
        cmdBuf->begin();

        int windowWidth  = 0;
        int windowHeight = 0;
        _impl->render->getWindowSize(windowWidth, windowHeight);

        const auto& presentation = _impl->presentationTargets[static_cast<size_t>(imageIndex)];
        const Extent2D presentExtent{
            .width  = static_cast<uint32_t>(windowWidth),
            .height = static_cast<uint32_t>(windowHeight),
        };

        // Clear the swapchain image first (the RuntimeUIComposite pass loads
        // the target because the game path composites over the world image).
        cmdBuf->retainResource(presentation->renderImage->getImageShared());
        cmdBuf->retainResource(presentation->renderImage->getImageViewShared());
        cmdBuf->transitionImageLayoutAuto(presentation->renderImage->getImage(), EImageLayout::ColorAttachmentOptimal);
        cmdBuf->beginRendering(RenderingInfo{
            .label                         = "GUIApp_Clear",
            .bExternalTransitionManagement = true,
            .attachments                   = RenderAttachmentSet{
                .renderArea = Rect2D{
                    .pos    = {0.0f, 0.0f},
                    .extent = {static_cast<float>(presentExtent.width), static_cast<float>(presentExtent.height)},
                },
                .layerCount = 1,
                .colors     = {
                    RenderAttachment{
                        .image         = presentation->renderImage->getImage(),
                        .imageView     = presentation->renderImage->getImageView(),
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
            *presentation->renderImage,
            /*depthTarget=*/nullptr,
            &snapshot,
            FRender2DComposePassDesc{
                .kind                  = ERender2DComposePassKind::RuntimeUIComposite,
                .logicalViewportExtent = _impl->tree->getLogicalExtent(),
                .finalLayout           = EImageLayout::PresentSrcKHR,
            });

        cmdBuf->end();
        _impl->render->end(imageIndex, {cmdBuf->getHandle()});
    }

    return 0;
}

WidgetTree& GUIAppHost::getTree()
{
    return *_impl->tree;
}

void GUIAppHost::shutdown()
{
    if (!_impl->bInitialized) {
        return;
    }

    // Clean shutdown, reverse order.
    _impl->render->waitIdle();
    Render2D::destroy();
    _impl->commandBuffers.clear();   // releases command-buffer resource retention
    _impl->presentationTargets.clear();
    FontManager::get()->clearCache();
    TextureLibrary::get().shutdown();
    DeferredDeletionQueue::get().flushAll();
    _impl->render->destroy();
    delete _impl->render;
    _impl->render = nullptr;
    SDL_StopTextInput(static_cast<SDL_Window*>(_impl->window.getNativeWindowHandle()));
    _impl->window.destroy();

    _impl->tree.reset();
    _impl->bInitialized = false;
}

} // namespace ya
