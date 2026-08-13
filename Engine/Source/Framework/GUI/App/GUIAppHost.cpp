#include "GUI/App/GUIAppHost.h"
#include "GUI/App/GUIPresentationTarget.h"

#include "AppRuntime/AppBootstrap.h"
#include "Core/Application/BmpDiff.h"
#include "Core/Application/AutomationControlServer.h"
#include "Core/Application/AutomationRun.h"
#include "Core/Application/GuiEventDriver.h"
#include "Core/FName.h"
#include "Core/KeyCode.h"
#include "Core/Log.h"
#include "Core/Common/DeferredDeletionQueue.h"
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
#include "GUI/Widgets/WidgetTreeDump.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <format>
#include <vector>

namespace ya
{

namespace
{

constexpr uint32_t DEFAULT_WINDOW_WIDTH  = 1024;
constexpr uint32_t DEFAULT_WINDOW_HEIGHT = 768;

nlohmann::json makeAutomationSuccess(const AppAutomationControlServer::Request& request,
                                     nlohmann::json result = nlohmann::json::object())
{
    return {
        {"id", request.id},
        {"ok", true},
        {"result", std::move(result)},
    };
}

nlohmann::json makeAutomationError(const AppAutomationControlServer::Request& request, std::string_view message)
{
    return {
        {"id", request.id},
        {"ok", false},
        {"error", std::string(message)},
    };
}

Extent2D queryWindowLogicalExtent(IRender& render)
{
    int width  = 0;
    int height = 0;
    render.getWindowSize(width, height);
    return {
        .width  = static_cast<uint32_t>(std::max(width, 0)),
        .height = static_cast<uint32_t>(std::max(height, 0)),
    };
}

/// Host built-in texture resolver: asset path aliases resolve to
/// TextureLibrary entries so image widgets work without an asset system.
/// The built-in textures live as long as the host, so the returned aliasing
/// shared_ptrs (no-op deleter) are safe for the snapshot lifetime.
std::shared_ptr<Texture> resolveBuiltinTexture(const std::string& assetPath)
{
    if (assetPath == "builtin/white") {
        return TextureLibrary::get().getWhiteTexture();
    }
    const auto alias = [](ya::Ptr<Texture> texture) -> std::shared_ptr<Texture>
    {
        return texture ? std::shared_ptr<Texture>(texture.get(), [](Texture*) {}) : nullptr;
    };
    if (assetPath == "builtin/black") {
        return alias(TextureLibrary::get().getBlackTexture());
    }
    if (assetPath == "builtin/multipixel") {
        return alias(TextureLibrary::get().getMultiPixelTexture());
    }
    if (assetPath == "builtin/checkerboard") {
        return alias(TextureLibrary::get().getCheckerboardTexture());
    }
    return nullptr;
}

/// Debug rasterizer: draws the snapshot items into a 24-bit BMP so the UI
/// layout (positions, overlaps, bounds) can be inspected without a display.
/// Text items are drawn as bright translucent blocks; sprites use their tint.
void dumpSnapshotToBMP(const UIFrameSnapshot& snapshot, const std::string& path, uint64_t frame)
{
    const int w = static_cast<int>(snapshot.logicalExtent.width);
    const int h = static_cast<int>(snapshot.logicalExtent.height);
    if (w <= 0 || h <= 0) {
        return;
    }
    YA_CORE_INFO("GUIAppHost snapshot dump: {} items at frame {}", snapshot.items.size(), frame);
    for (size_t i = 0; i < snapshot.items.size(); ++i) {
        const auto& item = snapshot.items[i];
        YA_CORE_INFO("  [{}] kind={} pos=({}, {}) size=({}, {}) text='{}'",
                     i,
                     item.kind == UIFrameDrawItem::EKind::Text ? "Text" : "Sprite",
                     item.pos.x,
                     item.pos.y,
                     item.size.x,
                     item.size.y,
                     item.text);
    }
    const int rowStride = ((w * 3 + 3) / 4) * 4;
    std::vector<uint8_t> pixels(static_cast<size_t>(rowStride) * static_cast<size_t>(h), 0);

    const auto blend = [&pixels, rowStride, w, h](int x, int y, glm::vec4 color)
    {
        if (x < 0 || y < 0 || x >= w || y >= h) {
            return;
        }
        const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(rowStride) + static_cast<size_t>(x) * 3;
        const float  a   = std::clamp(color.a, 0.0f, 1.0f);
        pixels[idx + 0] = static_cast<uint8_t>(pixels[idx + 0] * (1.0f - a) + color.b * 255.0f * a);
        pixels[idx + 1] = static_cast<uint8_t>(pixels[idx + 1] * (1.0f - a) + color.g * 255.0f * a);
        pixels[idx + 2] = static_cast<uint8_t>(pixels[idx + 2] * (1.0f - a) + color.r * 255.0f * a);
    };

    for (const UIFrameDrawItem& item : snapshot.items) {
        const int x0 = std::max(0, static_cast<int>(item.pos.x));
        const int y0 = std::max(0, static_cast<int>(item.pos.y));
        const int x1 = std::min(w, static_cast<int>(item.pos.x + item.size.x));
        const int y1 = std::min(h, static_cast<int>(item.pos.y + item.size.y));
        const glm::vec4 color = item.kind == UIFrameDrawItem::EKind::Text
                                    ? glm::vec4(1.0f, 0.95f, 0.65f, 0.85f)
                                    : item.color;
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                blend(x, y, color);
            }
        }
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        YA_CORE_ERROR("dumpSnapshotToBMP: cannot open '{}'", path);
        return;
    }
    const uint32_t fileSize = 54 + static_cast<uint32_t>(rowStride) * static_cast<uint32_t>(h);
    const uint8_t  header[54] = {
        'B', 'M',
        static_cast<uint8_t>(fileSize & 0xFF), static_cast<uint8_t>((fileSize >> 8) & 0xFF),
        static_cast<uint8_t>((fileSize >> 16) & 0xFF), static_cast<uint8_t>((fileSize >> 24) & 0xFF),
        0, 0, 0, 0,
        54, 0, 0, 0,
        40, 0, 0, 0,
        static_cast<uint8_t>(w & 0xFF), static_cast<uint8_t>((w >> 8) & 0xFF),
        static_cast<uint8_t>((w >> 16) & 0xFF), static_cast<uint8_t>((w >> 24) & 0xFF),
        static_cast<uint8_t>(h & 0xFF), static_cast<uint8_t>((h >> 8) & 0xFF),
        static_cast<uint8_t>((h >> 16) & 0xFF), static_cast<uint8_t>((h >> 24) & 0xFF),
        1, 0,
        24, 0,
        0, 0, 0, 0,
        static_cast<uint8_t>(rowStride * h & 0xFF), static_cast<uint8_t>((rowStride * h >> 8) & 0xFF),
        static_cast<uint8_t>((rowStride * h >> 16) & 0xFF), static_cast<uint8_t>((rowStride * h >> 24) & 0xFF),
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
    };
    file.write(reinterpret_cast<const char*>(header), sizeof(header));
    // BMP rows are bottom-up.
    for (int y = h - 1; y >= 0; --y) {
        file.write(reinterpret_cast<const char*>(pixels.data() + static_cast<size_t>(y) * rowStride),
                   rowStride);
    }
    YA_CORE_INFO("GUIAppHost dumped snapshot to '{}' ({}x{})", path, w, h);
}

/// Write readback pixels (top-left origin) as a 24-bit bottom-up BMP.
/// `bBgraSource` selects the byte order of the readback buffer: the image's
/// native format byte order (BGRA8 for the macOS swapchain), not RGBA.
void writeRGBAtoBMP(const uint8_t* rgba, uint32_t width, uint32_t height,
                    bool bBgraSource, const std::string& path)
{
    const int w = static_cast<int>(width);
    const int h = static_cast<int>(height);
    const int rowStride = ((w * 3 + 3) / 4) * 4;
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        YA_CORE_ERROR("writeRGBAtoBMP: cannot open '{}'", path);
        return;
    }
    const uint32_t fileSize = 54 + static_cast<uint32_t>(rowStride) * static_cast<uint32_t>(h);
    const uint8_t  header[54] = {
        'B', 'M',
        static_cast<uint8_t>(fileSize & 0xFF), static_cast<uint8_t>((fileSize >> 8) & 0xFF),
        static_cast<uint8_t>((fileSize >> 16) & 0xFF), static_cast<uint8_t>((fileSize >> 24) & 0xFF),
        0, 0, 0, 0,
        54, 0, 0, 0,
        40, 0, 0, 0,
        static_cast<uint8_t>(w & 0xFF), static_cast<uint8_t>((w >> 8) & 0xFF),
        static_cast<uint8_t>((w >> 16) & 0xFF), static_cast<uint8_t>((w >> 24) & 0xFF),
        static_cast<uint8_t>(h & 0xFF), static_cast<uint8_t>((h >> 8) & 0xFF),
        static_cast<uint8_t>((h >> 16) & 0xFF), static_cast<uint8_t>((h >> 24) & 0xFF),
        1, 0,
        24, 0,
        0, 0, 0, 0,
        static_cast<uint8_t>(rowStride * h & 0xFF), static_cast<uint8_t>((rowStride * h >> 8) & 0xFF),
        static_cast<uint8_t>((rowStride * h >> 16) & 0xFF), static_cast<uint8_t>((rowStride * h >> 24) & 0xFF),
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
    };
    file.write(reinterpret_cast<const char*>(header), sizeof(header));
    // BMP rows are bottom-up; the RGBA source is top-down.
    for (int y = h - 1; y >= 0; --y) {
        const uint8_t* src = rgba + static_cast<size_t>(y) * width * 4;
        std::vector<uint8_t> row(static_cast<size_t>(rowStride), 0);
        for (int x = 0; x < w; ++x) {
            const size_t i = static_cast<size_t>(x) * 4;
            const uint8_t r = bBgraSource ? src[i + 2] : src[i + 0];
            const uint8_t g = src[i + 1];
            const uint8_t b = bBgraSource ? src[i + 0] : src[i + 2];
            row[static_cast<size_t>(x) * 3 + 0] = b;
            row[static_cast<size_t>(x) * 3 + 1] = g;
            row[static_cast<size_t>(x) * 3 + 2] = r;
        }
        file.write(reinterpret_cast<const char*>(row.data()), rowStride);
    }
    YA_CORE_INFO("GUIAppHost wrote GPU shot to '{}' ({}x{})", path, w, h);
}

/// Maps SDL events to Core Events. Pointer press/release/scroll carry no
/// position in the Core event structs; the host tracks the current pointer
/// position from MouseMoveEvent and uses it for those.
struct SdlEventSource final : IAppEventSource
{
    uint32_t hostWindowID = 0;

    void pollEvents(const std::function<void(const Event&)>& emit) override
    {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            const bool bHostWindowEvent = [&]() {
                switch (event.type) {
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                case SDL_EVENT_WINDOW_RESIZED:
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:
                case SDL_EVENT_WINDOW_MINIMIZED:
                case SDL_EVENT_WINDOW_MAXIMIZED:
                case SDL_EVENT_WINDOW_RESTORED:
                    return hostWindowID == 0 || event.window.windowID == hostWindowID;
                default:
                    return true;
                }
            }();

            switch (event.type) {
            case SDL_EVENT_QUIT:
                emit(AppQuitEvent{});
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (bHostWindowEvent) {
                    emit(WindowCloseEvent(event.window.windowID));
                }
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                if (bHostWindowEvent) {
                    emit(WindowResizeEvent(event.window.windowID, event.window.data1, event.window.data2));
                }
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:
            case SDL_EVENT_WINDOW_MAXIMIZED:
            case SDL_EVENT_WINDOW_RESTORED:
                if (bHostWindowEvent) {
                    emit(WindowRestoreEvent(event.window.windowID));
                }
                break;
            case SDL_EVENT_WINDOW_MINIMIZED:
                if (bHostWindowEvent) {
                    emit(WindowMinimizeEvent(event.window.windowID));
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                emit(MouseMoveEvent(event.motion.x, event.motion.y));
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                emit(MouseButtonPressedEvent(event.button.button));
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                emit(MouseButtonReleasedEvent(event.button.button));
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                emit(MouseScrolledEvent(event.wheel.x, event.wheel.y));
                break;
            case SDL_EVENT_KEY_DOWN: {
                KeyPressedEvent ev;
                ev._keyCode = EKey::fromSDLKeycode(event.key.key);
                ev._mod     = event.key.mod;
                ev.bRepeat  = event.key.repeat;
                emit(ev);
                break;
            }
            case SDL_EVENT_KEY_UP: {
                KeyReleasedEvent ev;
                ev._keyCode = EKey::fromSDLKeycode(event.key.key);
                ev._mod     = event.key.mod;
                emit(ev);
                break;
            }
            case SDL_EVENT_TEXT_INPUT:
                emit(KeyTypedEvent(event.text.text));
                break;
            default:
                break;
            }
        }
    }
};

/// Drives a JSONL scenario as an event source. Pointer steps first emit a
/// MouseMoveEvent to their position so the host's tracked pointer follows,
/// then the press/release/scroll. A Frame step returns (letting the caller
/// render that frame); Checkpoint invokes the host dump hook.
struct ScenarioEventSource final : IAppEventSource
{
    std::vector<GuiScenarioStep> steps;
    size_t index = 0;
    std::function<void(const std::string&)> onCheckpoint;
    std::function<void()> onCaptureFinal;
    std::function<void()> onDone;

    void pollEvents(const std::function<void(const Event&)>& emit) override
    {
        while (index < steps.size()) {
            const GuiScenarioStep& step = steps[index++];
            switch (step.kind) {
            case EGuiScenarioStepKind::Frame:
                if (index == steps.size() && onCaptureFinal) {
                    onCaptureFinal();
                }
                return;
            case EGuiScenarioStepKind::Checkpoint:
                if (onCheckpoint) {
                    onCheckpoint(step.tag);
                }
                break;
            case EGuiScenarioStepKind::MouseMove:
                emit(MouseMoveEvent(step.point.x, step.point.y));
                break;
            case EGuiScenarioStepKind::MousePress:
                emit(MouseMoveEvent(step.point.x, step.point.y));
                emit(MouseButtonPressedEvent(step.button));
                break;
            case EGuiScenarioStepKind::MouseRelease:
                emit(MouseMoveEvent(step.point.x, step.point.y));
                emit(MouseButtonReleasedEvent(step.button));
                break;
            case EGuiScenarioStepKind::MouseWheel:
                emit(MouseMoveEvent(step.point.x, step.point.y));
                emit(MouseScrolledEvent(step.wheel.x, step.wheel.y));
                break;
            case EGuiScenarioStepKind::KeyPress: {
                KeyPressedEvent ev;
                ev._keyCode = step.key;
                ev._mod     = 0;
                ev.bRepeat  = false;
                emit(ev);
                break;
            }
            case EGuiScenarioStepKind::KeyRelease: {
                KeyReleasedEvent ev;
                ev._keyCode = step.key;
                ev._mod     = 0;
                emit(ev);
                break;
            }
            case EGuiScenarioStepKind::KeyTyped: {
                KeyTypedEvent ev(step.text);
                ev._mod = 0;
                emit(ev);
                break;
            }
            case EGuiScenarioStepKind::Drag: {
                emit(MouseMoveEvent(step.point.x, step.point.y));
                emit(MouseButtonPressedEvent(step.button));
                const int n = std::max(1, step.dragSteps);
                for (int i = 1; i <= n; ++i) {
                    const float     t = static_cast<float>(i) / static_cast<float>(n);
                    const glm::vec2 p = step.point + (step.dragTo - step.point) * t;
                    emit(MouseMoveEvent(p.x, p.y));
                }
                emit(MouseButtonReleasedEvent(step.button));
                break;
            }
            }
        }
        if (onDone) {
            onDone();
        }
    }
};

} // namespace

struct GUIAppHost::FImpl
{
    const FGUIAppHostConfig* config = nullptr;
    IGUIAppDelegate*         delegate = nullptr;

    SDLWindowProvider        window;
    IRender*                 render  = nullptr;
    AppAutomationControlServer automationServer;
    std::shared_ptr<ShaderStorage> shaderStorage;
    std::unique_ptr<WidgetTree> tree;

    std::vector<std::shared_ptr<ICommandBuffer>>       commandBuffers;
    std::vector<std::shared_ptr<GUIPresentationTarget>> presentationTargets;
    void*    cachedSwapchainHandle = nullptr;
    Extent2D cachedSwapchainExtent{};
    uint64_t frameCount = 0;
    float    lastMouseX = -1.0f;
    float    lastMouseY = -1.0f;
    bool     bSwapchainRecreatePending = false;
    bool     bWindowMinimized = false;
    bool     bInitialized = false;
    std::shared_ptr<IBuffer> gpuShotBuffer;

    std::unique_ptr<IAppEventSource> eventSource;
    std::string captureRequestPath;
    bool    bLoggedFirstSnapshot = false;
    bool    bQuitRequested       = false;
    bool    bScenarioMode        = false;
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

    // Shared process bootstrap: bundled graphics runtime env and deferred
    // reflection registration. Standalone GUI apps intentionally do not pull
    // in the engine/game VFS by default.
    AppBootstrap::initializeProcessCore();

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
            .bVsync        = config.bVsync,
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
    if (!_impl->automationServer.init(config.automation.controlPort)) {
        YA_CORE_ERROR("GUIAppHost: failed to initialize automation control server on port {}",
                      config.automation.controlPort);
        shutdown();
        return false;
    }
    _impl->delegate->buildUI(*_impl->tree);
    if (!config.scenarioPath.empty()) {
        auto scenario = std::make_unique<ScenarioEventSource>();
        std::string scenarioError;
        scenario->steps = loadGuiScenarioFile(config.scenarioPath, &scenarioError);
        if (!scenarioError.empty()) {
            YA_CORE_ERROR("GUIAppHost: failed to load scenario '{}': {}", config.scenarioPath, scenarioError);
            shutdown();
            return false;
        }
        scenario->onCheckpoint   = [this](const std::string& tag) { dumpScenarioCheckpoint(tag); };
        scenario->onCaptureFinal = [this]() {
            if (!_impl->config->scenarioCapturePath.empty()) {
                _impl->captureRequestPath = _impl->config->scenarioCapturePath;
            }
        };
        scenario->onDone = [this]() { _impl->bQuitRequested = true; };
        _impl->eventSource   = std::move(scenario);
        _impl->bScenarioMode = true;
    }
    else {
        auto sdl = std::make_unique<SdlEventSource>();
        sdl->hostWindowID = _impl->window.getWindowID();
        _impl->eventSource = std::move(sdl);
    }

    render->allocateCommandBuffers(render->getSwapchainImageCount(), _impl->commandBuffers);

    // Presentation render targets: one imported swapchain image per frame.
    GUIPresentationTarget::buildAll(*render, *swapchain, "GUIApp", _impl->presentationTargets);
    _impl->cachedSwapchainHandle = swapchain->getHandle();
    _impl->cachedSwapchainExtent = swapchain->getExtent();
    _impl->tree->setLogicalExtent(queryWindowLogicalExtent(*render));

    _impl->bInitialized = true;
    return true;
}

void GUIAppHost::dispatchToTree(const Event& event, float mouseX, float mouseY)
{
    WidgetEventContext ctx;
    ctx.logicalPoint = {mouseX, mouseY};
    const EWidgetRouteResult result = _impl->tree->dispatchEvent(event, ctx);
    _impl->delegate->onRoutedEvent(event, result);
}

void GUIAppHost::rebuildPresentationResources(bool bWaitForGpu)
{
    // Frame boundary only: wait for in-flight work, then release command
    // buffers (and their retained resources) and the imported images/views
    // before rebuilding from the current swapchain.
    if (bWaitForGpu) {
        _impl->render->waitIdle();
    }
    _impl->commandBuffers.clear();
    _impl->presentationTargets.clear();

    auto* swapchain = _impl->render->getSwapchain()->as<VulkanSwapChain>();
    _impl->render->allocateCommandBuffers(_impl->render->getSwapchainImageCount(), _impl->commandBuffers);
    GUIPresentationTarget::buildAll(*_impl->render, *swapchain, "GUIApp", _impl->presentationTargets);
    _impl->cachedSwapchainHandle = swapchain->getHandle();
    _impl->cachedSwapchainExtent = swapchain->getExtent();
}

int GUIAppHost::run()
{
    if (!_impl->bInitialized) {
        YA_CORE_ERROR("GUIAppHost::run called before a successful init()");
        return 1;
    }

    AppKernel kernel({.eventSource = _impl->eventSource.get()}, *this);
    const int result = kernel.run(_impl->config->automation);
    if (result != 0) {
        return result;
    }

    if (_impl->bScenarioMode &&
        !_impl->config->scenarioGoldenPath.empty() &&
        !_impl->config->scenarioDiffPath.empty() &&
        !_impl->config->scenarioCapturePath.empty()) {
        const BmpDiffResult diff = diffBmpFiles(_impl->config->scenarioGoldenPath,
                                                _impl->config->scenarioCapturePath,
                                                _impl->config->scenarioDiffPath,
                                                16, 0.0f);
        YA_CORE_INFO("GUIAppHost scenario diff: pass={} differing={} ratio={:.4f}",
                     diff.bPass, diff.differingPixels, diff.diffRatio);
        if (!diff.bPass) {
            return 2;
        }
    }

    return 0;
}

void GUIAppHost::onInit() {}
void GUIAppHost::onShutdown() {}

void GUIAppHost::onEvent(const Event& event)
{
    switch (event.getEventType()) {
    case EEvent::AppQuit:
    case EEvent::WindowClose:
        _impl->bQuitRequested = true;
        return;
    case EEvent::WindowResize: {
        const auto& resize = static_cast<const WindowResizeEvent&>(event);
        _impl->bWindowMinimized = resize.GetWidth() == 0 || resize.GetHeight() == 0;
        _impl->bSwapchainRecreatePending = true;
        return;
    }
    case EEvent::WindowMinimize:
        _impl->bWindowMinimized = true;
        _impl->bSwapchainRecreatePending = true;
        return;
    case EEvent::WindowRestore:
        _impl->bWindowMinimized = false;
        _impl->bSwapchainRecreatePending = true;
        return;
    case EEvent::KeyPressed: {
        const auto& key = static_cast<const KeyPressedEvent&>(event);
        if (_impl->config->bEscapeQuits && key.getKeyCode() == EKey::Escape && !key.isRepeat()) {
            _impl->bQuitRequested = true;
            return;
        }
        dispatchToTree(event, -1.0f, -1.0f);
        return;
    }
    case EEvent::MouseMoved: {
        const auto& move = static_cast<const MouseMoveEvent&>(event);
        _impl->lastMouseX = move.getX();
        _impl->lastMouseY = move.getY();
        dispatchToTree(event, move.getX(), move.getY());
        return;
    }
    case EEvent::MouseButtonPressed:
    case EEvent::MouseButtonReleased:
    case EEvent::MouseScrolled:
        dispatchToTree(event, _impl->lastMouseX, _impl->lastMouseY);
        return;
    case EEvent::KeyReleased:
    case EEvent::KeyTyped:
        dispatchToTree(event, -1.0f, -1.0f);
        return;
    default:
        return;
    }
}

bool GUIAppHost::shouldClose() const
{
    return _impl->bQuitRequested || _impl->delegate->shouldRequestClose();
}

void GUIAppHost::onTick(float /*dt*/)
{
    // Events are delivered by the kernel event phase (via onEvent) before
    // this tick; here we only process commands and render one frame.
    if (_impl->bQuitRequested) {
        return;
    }
    ++_impl->frameCount;

    for (auto& request : _impl->automationServer.consumePendingRequests()) {
        if (request->method == "ping") {
            _impl->automationServer.completeRequest(
                request,
                makeAutomationSuccess(*request,
                                      {
                                          {"service", "gui-automation-control"},
                                          {"port", _impl->automationServer.getPort()},
                                          {"title", _impl->config->title},
                                      }));
            continue;
        }
        if (request->method == "quit") {
            _impl->bQuitRequested = true;
            _impl->automationServer.completeRequest(request, makeAutomationSuccess(*request));
            continue;
        }
        if (request->method == "set_window_size") {
            const auto widthIt  = request->params.find("width");
            const auto heightIt = request->params.find("height");
            if (widthIt == request->params.end() || heightIt == request->params.end() ||
                !widthIt->is_number_integer() || !heightIt->is_number_integer()) {
                _impl->automationServer.completeRequest(
                    request,
                    makeAutomationError(*request, "set_window_size requires integer params {width,height}"));
                continue;
            }
            const int width  = widthIt->get<int>();
            const int height = heightIt->get<int>();
            if (width <= 0 || height <= 0) {
                _impl->automationServer.completeRequest(
                    request,
                    makeAutomationError(*request, "set_window_size expects positive width and height"));
                continue;
            }
            if (!_impl->window.setWindowSize(width, height)) {
                _impl->automationServer.completeRequest(
                    request,
                    makeAutomationError(*request, std::format("failed to set window size to {}x{}", width, height)));
                continue;
            }
            _impl->bSwapchainRecreatePending = true;
            _impl->automationServer.completeRequest(
                request,
                makeAutomationSuccess(*request, {{"width", width}, {"height", height}}));
            continue;
        }
        _impl->automationServer.completeRequest(
            request,
            makeAutomationError(*request, std::format("unknown method: {}", request->method)));
    }

    if (_impl->bSwapchainRecreatePending) {
        auto* swapchain = _impl->render->getSwapchain()->as<VulkanSwapChain>();
        swapchain->requestRecreate();
        _impl->bSwapchainRecreatePending = false;
    }
    if (_impl->bWindowMinimized) {
        _impl->render->waitIdle();
        return;
    }

    int32_t imageIndex = -1;
    if (!_impl->render->begin(&imageIndex)) {
        return;
    }
    if (imageIndex < 0) {
        _impl->render->waitIdle();
        return;
    }

    auto* swapchain = _impl->render->getSwapchain()->as<VulkanSwapChain>();
    const Extent2D swapchainExtent = swapchain->getExtent();
    if (swapchain->getHandle() != _impl->cachedSwapchainHandle ||
        _impl->render->getSwapchainImageCount() != _impl->presentationTargets.size() ||
        swapchainExtent.width != _impl->cachedSwapchainExtent.width ||
        swapchainExtent.height != _impl->cachedSwapchainExtent.height) {
        rebuildPresentationResources(/*bWaitForGpu=*/true);
        swapchain = _impl->render->getSwapchain()->as<VulkanSwapChain>();
    }
    _impl->tree->setLogicalExtent(queryWindowLogicalExtent(*_impl->render));

    prepareRender2DComposePassPipeline(
        FRender2DComposePassDesc{
            .kind = ERender2DComposePassKind::RuntimeUIComposite,
        },
        swapchain->getFormat());
    _impl->delegate->updateUI();
    const auto&    presentation  = _impl->presentationTargets[static_cast<size_t>(imageIndex)];
    const Extent2D presentExtent = presentation->renderImage->getExtent();
    const Extent2D logicalExtent = _impl->tree->getLogicalExtent();
    const UIFrameSnapshot snapshot = _impl->tree->buildSnapshot(UIFrameBuildContext{
        .uiScale = {
            static_cast<float>(presentExtent.width) / static_cast<float>(std::max(logicalExtent.width, 1u)),
            static_cast<float>(presentExtent.height) / static_cast<float>(std::max(logicalExtent.height, 1u)),
        },
        .offset = {0.0f, 0.0f},
        .textureResolver = resolveBuiltinTexture,
    });
    if (!_impl->bLoggedFirstSnapshot) {
        _impl->bLoggedFirstSnapshot = true;
        YA_CORE_INFO("GUIAppHost first snapshot: {} draw items, {}x{} logical -> {}x{} render",
                     snapshot.items.size(),
                     snapshot.logicalExtent.width,
                     snapshot.logicalExtent.height,
                     presentExtent.width,
                     presentExtent.height);
    }
    if (_impl->config && !_impl->config->dumpSnapshotPath.empty() &&
        _impl->frameCount == _impl->config->dumpFrame) {
        dumpSnapshotToBMP(snapshot, _impl->config->dumpSnapshotPath, _impl->frameCount);
    }

    auto cmdBuf = _impl->commandBuffers[static_cast<size_t>(imageIndex)];
    cmdBuf->reset();
    cmdBuf->begin();

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

    std::string capturePath;
    if (_impl->config->gpuShotFrame != 0 &&
        _impl->frameCount == _impl->config->gpuShotFrame &&
        !_impl->config->gpuShotPath.empty()) {
        capturePath = _impl->config->gpuShotPath;
    }
    else if (!_impl->captureRequestPath.empty()) {
        capturePath = _impl->captureRequestPath;
        _impl->captureRequestPath.clear();
    }
    if (!capturePath.empty()) {
        if (!_impl->gpuShotBuffer) {
            _impl->gpuShotBuffer = _impl->render->getResourceFactory()->createBuffer(
                ya::BufferCreateInfo{
                    .label       = "GUIAppHost_GpuShot",
                    .usage       = EBufferUsage::TransferDst,
                    .size        = presentExtent.width * presentExtent.height * 4,
                    .memoryUsage = EMemoryUsage::GpuToCpu,
                });
        }
        cmdBuf->transitionImageLayoutAuto(presentation->renderImage->getImage(), EImageLayout::TransferSrc);
        cmdBuf->copyImageToBuffer(
            presentation->renderImage->getImage(),
            EImageLayout::TransferSrc,
            _impl->gpuShotBuffer.get(),
            {ya::BufferImageCopy{
                .imageSubresource  = {.aspectMask = 1, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
                .imageOffsetX      = 0,
                .imageOffsetY      = 0,
                .imageOffsetZ      = 0,
                .imageExtentWidth  = presentExtent.width,
                .imageExtentHeight = presentExtent.height,
                .imageExtentDepth  = 1,
            }});
        cmdBuf->transitionImageLayoutAuto(presentation->renderImage->getImage(), EImageLayout::PresentSrcKHR);
    }

    cmdBuf->end();
    _impl->render->end(imageIndex, {cmdBuf->getHandle()});

    if (!capturePath.empty()) {
        _impl->render->waitIdle();
        if (uint8_t* pixels = _impl->gpuShotBuffer->map<uint8_t>()) {
            writeRGBAtoBMP(pixels,
                           presentExtent.width,
                           presentExtent.height,
                           swapchain->getFormat() == EFormat::B8G8R8A8_UNORM,
                           capturePath);
            _impl->gpuShotBuffer->unmap();
        }
    }
}

void GUIAppHost::injectEvent(const Event& event, const glm::vec2& logicalPoint)
{
    dispatchToTree(event, logicalPoint.x, logicalPoint.y);
}

void GUIAppHost::dumpScenarioCheckpoint(const std::string& tag)
{
    if (_impl->config->scenarioDumpDir.empty() || tag.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(_impl->config->scenarioDumpDir, ec);
    const nlohmann::json dump = dumpWidgetTree(*_impl->tree);
    const std::string   path  = _impl->config->scenarioDumpDir + "/" + tag + ".json";
    std::ofstream       file(path);
    if (file) {
        file << dump.dump(2);
        YA_CORE_INFO("GUIAppHost scenario checkpoint '{}' -> {}", tag, path);
    }
    else {
        YA_CORE_ERROR("GUIAppHost scenario checkpoint: cannot write '{}'", path);
    }
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

    // Clean shutdown, reverse order. Every member owning GPU resources must be
    // released BEFORE the Vulkan device / VMA allocator is destroyed below
    // (a later ~VulkanBuffer would call vmaDestroyBuffer on a dead allocator).
    _impl->render->waitIdle();
    _impl->automationServer.shutdown();
    Render2D::destroy();
    _impl->commandBuffers.clear();   // releases command-buffer resource retention
    _impl->presentationTargets.clear();
    _impl->gpuShotBuffer.reset();    // readback staging buffer (RHI-owned)
    _impl->shaderStorage.reset();
    _impl->tree.reset();             // widgets hold snapshot/resolver refs only, but stay ordered
    FontManager::get()->clearCache();
    TextureLibrary::get().shutdown();
    DeferredDeletionQueue::get().flushAll();
    _impl->render->destroy();
    delete _impl->render;
    _impl->render = nullptr;
    SDL_StopTextInput(static_cast<SDL_Window*>(_impl->window.getNativeWindowHandle()));
    _impl->window.destroy();

    _impl->bInitialized = false;
}

} // namespace ya
