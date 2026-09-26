#include "GUI/Host/GUIAppHost.h"
#include "GUI/Host/GUIPresentationTarget.h"
#include "GUI/Host/GUIWindowChrome.h"
#include "GUI/Host/GUIWindowManager.h"
#include "GUI/Host/GUIWindowPresent.h"
#include "GUI/Host/GUIWindowSession.h"

#include "GUI/Host/AppBootstrap.h"
#include "App/Control/BmpDiff.h"
#include "App/Control/AutomationControlServer.h"
#include "App/Control/AutomationMethodRegistry.h"
#include "App/Control/AutomationRun.h"
#include "App/Control/GuiEventDriver.h"
#include "App/Kernel/SdlEventSource.h"
#include "App/Kernel/GuiScenarioEventSource.h"
#include "Core/FName.h"
#include "Core/KeyCode.h"
#include "Core/Log.h"
#include "Core/Common/DeferredDeletionQueue.h"
#include "RHI/Render.h"
#include "RHI/RenderDefines.h"
#include "RHI/Shader.h"
#include "GUI/Host/OsClipboard.h"
#include "RHI/NativeWindow.h"
#include "Core/Os/OsCursor.h"
#include "RHI/Core/Texture.h"
#include "RHI/Backend/TextureLibrary.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/PresentFrame.h"
#include "RHI/Core/Swapchain.h"

#include "GUI/Compose/GuiFrameInspectorOverlay.h"
#include "GUI/Compose/Render2DComposePass.h"
#include "Render/Resources/FontManager.h"
#include "Render2D/Render2D.h"
#include "GUI/Widgets/GuiFrameInspector.h"
#include "GUI/Widgets/GuiTextureCatalog.h"
#include "GUI/Widgets/UIFrameSnapshotDump.h"
#include "GUI/Widgets/WidgetTreeDump.h"
#include "GUI/Widgets/WidgetTree.h"

#include "Core/Os/OsEvent.h"
#include <stb_image.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <format>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ya
{

namespace
{

constexpr uint32_t DEFAULT_WINDOW_WIDTH  = 1024;
constexpr uint32_t DEFAULT_WINDOW_HEIGHT = 768;

Extent2D queryWindowLogicalExtent(INativeWindow& window)
{
    int width  = 0;
    int height = 0;
    window.getWindowSize(width, height);
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

/// Standalone GUI host adapter: stbi_load + Texture::fromData complete on the
/// GUIApp / WidgetTree owner thread before requestLoad returns (Caller).
struct HostGuiTextureSource final : IGuiTextureSource
{
    IRender* render = nullptr;
    std::unordered_map<std::string, std::shared_ptr<Texture>> disk;

    [[nodiscard]] static std::string diskPath(const std::string& path)
    {
        constexpr std::string_view kFile = "file:";
        if (path.starts_with(kFile)) {
            return path.substr(kFile.size());
        }
        return path;
    }

    [[nodiscard]] FGuiTextureLookup lookup(const std::string& path) override
    {
        if (auto texture = resolveBuiltinTexture(path)) {
            return {std::move(texture), EGuiTextureState::Ready};
        }
        if (auto it = disk.find(path); it != disk.end() && it->second) {
            return {it->second, EGuiTextureState::Ready};
        }
        return {nullptr, EGuiTextureState::Pending};
    }

    [[nodiscard]] EGuiTextureCompletionThread completionThread() const override
    {
        return EGuiTextureCompletionThread::Caller;
    }

    void requestLoad(const std::string& path, FGuiTextureReady ready) override
    {
        if (auto texture = resolveBuiltinTexture(path)) {
            ready(path, {std::move(texture), EGuiTextureState::Ready});
            return;
        }
        if (auto it = disk.find(path); it != disk.end() && it->second) {
            ready(path, {it->second, EGuiTextureState::Ready});
            return;
        }
        if (!render) {
            ready(path, {nullptr, EGuiTextureState::Failed});
            return;
        }

        const std::string file = diskPath(path);
        int               width = 0;
        int               height = 0;
        int               channels = 0;
        stbi_set_flip_vertically_on_load_thread(false);
        stbi_uc* raw = stbi_load(file.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!raw || width <= 0 || height <= 0) {
            if (raw) {
                stbi_image_free(raw);
            }
            ready(path, {nullptr, EGuiTextureState::Failed});
            return;
        }

        std::vector<ColorU8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height));
        std::memcpy(pixels.data(), raw, pixels.size() * sizeof(ColorU8_t));
        stbi_image_free(raw);

        auto texture = Texture::fromData(*render,
                                         static_cast<uint32_t>(width),
                                         static_cast<uint32_t>(height),
                                         pixels,
                                         path);
        if (!texture) {
            ready(path, {nullptr, EGuiTextureState::Failed});
            return;
        }
        disk[path] = texture;
        ready(path, {std::move(texture), EGuiTextureState::Ready});
    }
};

void appendDebugRenderOverlay(UIFrameSnapshot& snapshot, const WidgetTree& tree)
{
    const float w = static_cast<float>(snapshot.logicalExtent.width);
    const float h = static_cast<float>(snapshot.logicalExtent.height);
    if (w <= 0.0f || h <= 0.0f) {
        return;
    }

    const auto addRect = [&snapshot](glm::vec2 pos, glm::vec2 size, glm::vec4 color)
    {
        UIFrameDrawItem item;
        item.kind  = UIFrameDrawItem::EKind::Sprite;
        item.pos   = pos;
        item.size  = size;
        item.color = color;
        snapshot.items.push_back(std::move(item));
    };

    const auto addOutline = [&addRect](const Rect2D& rect, glm::vec4 color)
    {
        if (rect.extent.x <= 0.0f || rect.extent.y <= 0.0f) {
            return;
        }
        constexpr float outlineThickness = 1.0f;
        addRect(rect.pos, {rect.extent.x, outlineThickness}, color);
        addRect({rect.pos.x, rect.pos.y + std::max(0.0f, rect.extent.y - outlineThickness)},
                {rect.extent.x, outlineThickness},
                color);
        addRect(rect.pos, {outlineThickness, rect.extent.y}, color);
        addRect({rect.pos.x + std::max(0.0f, rect.extent.x - outlineThickness), rect.pos.y},
                {outlineThickness, rect.extent.y},
                color);
    };

    std::vector<Rect2D> uniqueClipRects;
    uniqueClipRects.reserve(snapshot.items.size());
    for (const UIFrameDrawItem& item : snapshot.items) {
        if (!item.bClipped || item.clip.extent.x <= 0.0f || item.clip.extent.y <= 0.0f) {
            continue;
        }
        const auto sameRect = [&item](const Rect2D& existing)
        {
            return existing.pos == item.clip.pos && existing.extent == item.clip.extent;
        };
        if (std::ranges::find_if(uniqueClipRects, sameRect) == uniqueClipRects.end()) {
            uniqueClipRects.push_back(item.clip);
        }
    }

    for (size_t i = 0; i < uniqueClipRects.size(); ++i) {
        const glm::vec4 color =
            (i % 5) == 0 ? glm::vec4(1.0f, 0.35f, 0.20f, 0.95f) :
            (i % 5) == 1 ? glm::vec4(0.25f, 0.85f, 1.0f, 0.95f) :
            (i % 5) == 2 ? glm::vec4(0.35f, 1.0f, 0.45f, 0.95f) :
            (i % 5) == 3 ? glm::vec4(1.0f, 0.85f, 0.25f, 0.95f) :
                           glm::vec4(0.95f, 0.45f, 1.0f, 0.95f);
        addOutline(uniqueClipRects[i], color);
    }

    constexpr float t = 1.0f;
    const float midX = std::max(0.0f, std::floor(w * 0.5f));
    const float midY = std::max(0.0f, std::floor(h * 0.5f));

    addRect({0.0f, 0.0f}, {w, t}, {1.0f, 0.15f, 0.15f, 0.95f});
    addRect({0.0f, std::max(0.0f, h - t)}, {w, t}, {0.15f, 0.55f, 1.0f, 0.95f});
    addRect({0.0f, 0.0f}, {t, h}, {1.0f, 0.15f, 0.15f, 0.95f});
    addRect({std::max(0.0f, w - t), 0.0f}, {t, h}, {0.15f, 0.55f, 1.0f, 0.95f});

    addRect({0.0f, midY}, {w, t}, {0.10f, 0.85f, 0.30f, 0.65f});
    addRect({midX, 0.0f}, {t, h}, {0.10f, 0.85f, 0.30f, 0.65f});

    addRect({0.0f, 0.0f}, {12.0f, 12.0f}, {1.0f, 0.95f, 0.20f, 0.95f});
    addRect({midX - 3.0f, midY - 3.0f}, {7.0f, 7.0f}, {0.95f, 0.95f, 0.95f, 0.85f});

    const auto addPath = [&addOutline](const std::vector<UIElement*>& path, glm::vec4 color)
    {
        for (size_t index = 0; index < path.size(); ++index) {
            glm::vec4 stepColor = color;
            stepColor.a *= 0.35f + 0.65f *
                                        (static_cast<float>(index + 1) /
                                         static_cast<float>(std::max<size_t>(path.size(), 1)));
            addOutline(path[index]->_layoutRect, stepColor);
        }
    };

    // Route overlay is intentionally derived from tree-owned diagnostics and
    // converted to snapshot items before command recording. Render2D never
    // reads the live tree.
    addPath(tree.getPointerPath(), {1.0f, 0.55f, 0.12f, 0.95f});
    addPath(tree.getFocusPath(), {0.20f, 0.82f, 1.0f, 0.95f});
    if (const UIElement* captured = tree.getPointerCapture()) {
        addOutline(captured->_layoutRect, {1.0f, 0.18f, 0.72f, 0.98f});
    }
    if (const UIElement* hovered = tree.getHovered()) {
        addOutline(hovered->_layoutRect, {0.95f, 0.95f, 0.22f, 0.98f});
    }
    if (tree.getPointerState().bKnown) {
        const glm::vec2 p = tree.getPointerState().logicalPoint;
        addRect(p - glm::vec2(4.0f, 0.5f), {8.0f, 1.0f}, {1.0f, 0.72f, 0.18f, 0.95f});
        addRect(p - glm::vec2(0.5f, 4.0f), {1.0f, 8.0f}, {1.0f, 0.72f, 0.18f, 0.95f});
    }
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
}


} // namespace

SwapchainCreateInfo makeHostWindowSurfaceDesc(const FGUIWindowHostConfig& config)
{
    return SwapchainCreateInfo{
        .imageFormat = EFormat::R8G8B8A8_UNORM,
        // GUI hover is high-frequency interaction: prefer Mailbox (low latency,
        // no tearing) over the default FIFO present queue. The backend falls
        // back to FIFO when the driver lacks Mailbox.
        .presentMode        = EPresentMode::Mailbox,
        .bVsync             = config.bVsync,
        .minImageCount      = 3,
        .bEnableTransferSrc = true,
        .width              = config.width != 0 ? config.width : DEFAULT_WINDOW_WIDTH,
        .height             = config.height != 0 ? config.height : DEFAULT_WINDOW_HEIGHT,
    };
}

/// Runtime automation screenshot request (GUI offscreen parity). The control
/// server defers completion until the frame loop has captured the requested
/// surfaces and (optionally) diffed them, so the request carries its waiter.
struct PendingGuiCapture
{
    AppAutomationControlServer::RequestPtr waiter;
    std::string gpuPath;       // non-empty = capture the presentation surface
    std::string offscreenPath; // non-empty = capture the offscreen surface
    std::string diffPath;      // non-empty = gpu vs offscreen zero-tolerance diff
    uint64_t    earliestFrame = 0;
};

struct GUIWindowHost::FImpl
{
    const FGUIWindowHostConfig* config = nullptr;
    IGUIAppDelegate*         delegate = nullptr;

    SDLNativeWindow          window;
    FWindowChromeState       chrome;
    IRender*                 render  = nullptr;
    IRenderSurfaceContext*   present = nullptr;
    AppAutomationControlServer automationServer;
    /// The methods this host answers on the automation port (see
    /// registerAutomationMethods): the framework-side verbs over one window.
    AutomationMethodRegistry automationMethods;
    std::shared_ptr<ShaderStorage> shaderStorage;
    std::unique_ptr<WidgetTree> tree;
    /// The layout this window last published (see tickContent). Held across the
    /// tick/present split so a window's presentation uses the frame that was
    /// laid out for it rather than rebuilding one mid-loop.
    UIFrameSnapshot snapshot;
    bool            bSnapshotBuilt = false;
    HostGuiTextureSource        textureSource;

    /// This window's present resources and the shared present sequence's
    /// per-frame state (command buffers, imported compose targets, the
    /// swapchain identity it re-checks). One struct, not four loose fields --
    /// the same shape every GUI window presents through (see
    /// `FGUISurfacePresentResources` / `presentGuiSnapshot`).
    FGUISurfacePresentResources presentResources;
    Render2DPassSlot presentPassSlot   = kInvalidRender2DPassSlot;
    Render2DPassSlot offscreenPassSlot = kInvalidRender2DPassSlot;
    uint64_t frameCount = 0;
    float    lastMouseX = -1.0f;
    float    lastMouseY = -1.0f;
    bool     bSwapchainRecreatePending = false;
    bool     bWindowMinimized = false;
    bool     bInitialized = false;
    std::shared_ptr<IBuffer> gpuShotBuffer;
    std::shared_ptr<GUIRenderSurface> offscreenSurface;
    std::shared_ptr<IBuffer>           offscreenShotBuffer;

    std::unique_ptr<IAppEventSource> eventSource;
    /// The SDL source's filter, when the event source is the SDL pump (not the
    /// scenario driver): GUIApp toggles it so extras' events reach its router.
    SdlEventSource* sdlEventSource = nullptr;
    std::string captureRequestPath;
    std::optional<PendingGuiCapture> pendingCapture;
    bool    bLoggedFirstSnapshot = false;
    bool    bQuitRequested       = false;
    bool    bScenarioMode        = false;
    bool    bScenarioFailed      = false;

    // Real device pixel ratio from the window-system DPI (set at init + on
    // every resize/monitor move). This is the logical->framebuffer mapping
    // only; it is published to WidgetTree::setDpiScale, NOT used as the UI
    // zoom. Keeping it separate from uiUserScale is what avoids the classic
    // Qt DPI trap (DPI change leaking into user zoom and vice-versa).
    // Defaults to 1.0 (no native window: scenario/headless).
    float devicePixelRatio = 1.0f;
    // App/settings-level UI zoom, orthogonal to devicePixelRatio. Default 1.0.
    float uiUserScale = 1.0f;
};

GUIWindowHost::GUIWindowHost(const FGUIWindowHostConfig& config, IGUIAppDelegate& delegate)
    : _impl(std::make_unique<FImpl>())
{
    _impl->config   = &config;
    _impl->delegate = &delegate;
}

GUIWindowHost::~GUIWindowHost()
{
    shutdown();
}

bool GUIWindowHost::init()
{
    if (_impl->bInitialized) {
        return true;
    }

    const FGUIWindowHostConfig& config = *_impl->config;

    // Shared process bootstrap: bundled graphics runtime env and deferred
    // reflection registration. Standalone GUI apps intentionally do not pull
    // in the engine/game VFS by default.
    AppBootstrap::initializeProcessCore();
    if (!config.guiFrameInspector.empty()) {
        applyGuiFrameInspectorSpec(config.guiFrameInspector);
    }

    // 1. Window provider (SDL3 + Vulkan surface).
    SDLNativeWindow& window = _impl->window;
    if (!window.init()) {
        return false;
    }
    if (!window.recreate(WindowCreateInfo{
            .index       = 0,
            .renderAPI   = config.renderAPI,
            .title       = config.title,
            .width       = config.width != 0 ? config.width : DEFAULT_WINDOW_WIDTH,
            .height      = config.height != 0 ? config.height : DEFAULT_WINDOW_HEIGHT,
            .scale       = config.scale,
            .bResizable  = config.bResizable,
            .bBorderless = resolveWindowChromeMode(config.chromeMode.value_or(defaultWindowChromeMode())) ==
                           EWindowChromeMode::ClientDrawn,
        })) {
        window.destroy();
        return false;
    }
    _impl->chrome = applyWindowChrome(window,
                                      config.chromeMode.value_or(defaultWindowChromeMode()),
                                      config.bResizable);
    // Enable Unicode text input (KeyTypedEvent) so focused text fields can
    // edit; the events are routed like every other keyboard event.
    window.startTextInput();

    // 2. Shader compile/cache service (the Slang processor serves the GUI
    //    Sprite2D shaders; injected into the backend before pipeline build).
    _impl->shaderStorage = std::make_shared<ShaderStorage>(
        ShaderProcessorFactory()
            .withShaderStoragePath("Engine/Shader/Slang")
            .withCachedStoragePath("Engine/Intermediate/Shader/Slang")
            .FactoryNew());

    // 3. Render backend. This window is a startup surface: it exists before the
    //    device does, so the device creates its surface while it is being
    //    created and the window asks for the id back below.
    RenderCreateInfo renderCI{
        .renderAPI = config.renderAPI,
        .startupSurfaces = {
            StartupSurfaceDesc{
                .window      = &window,
                .swapchainCI = makeHostWindowSurfaceDesc(config),
            },
        },
    };
    IRender* render = IRender::create(renderCI);
    if (!render) {
        YA_CORE_ERROR("GUIAppHost: failed to create IRender instance");
        window.destroy();
        return false;
    }
    _impl->render  = render;
    render->setShaderStorage(_impl->shaderStorage);
    if (!render->init(renderCI)) {
        YA_CORE_ERROR("GUIAppHost: failed to initialize render backend");
        render->destroy();
        delete render;
        _impl->render = nullptr;
        window.destroy();
        return false;
    }
    // The host owns this window, so it names its own surface by that window.
    _impl->present = render->findSurface(window);
    if (!_impl->present || !_impl->present->getSwapchain()) {
        YA_CORE_ERROR("GUIAppHost: the device registered no surface for the host window");
        render->destroy();
        delete render;
        _impl->render  = nullptr;
        _impl->present = nullptr;
        window.destroy();
        return false;
    }

    // 4. Builtin textures/samplers and the runtime fonts (one atlas entry per
    //    configured size; UIText resolves fonts by exact name+size).
    TextureLibrary::get().init(render);
    // Size-driven flavor split (font-framework plan §1): the primary font is
    // loaded WITHOUT a forced mode, so getFont()/getAdaptiveFont() pick bitmap
    // for small text (hinted, no SDF bite-out) and SDF for large text. No
    // face-level hard SDF, so 13px glyphs keep their thin strokes. The 128px
    // load here simply pre-warms the SDF flavor; small sizes are lazily built
    // as bitmap bases on first request.
    // The whole stack (primary face + CJK/emoji fallbacks + the monospace
    // family) comes from one catalog id, so the host, the game runtime and any
    // later face switch all build it the same way. POLICY (one CJK face,
    // bundled emoji, which faces exist) stays in FontManager.
    if (!config.uiFontFace.empty() &&
        !FontManager::get()->loadUiFontStack(*render, config.uiFontFace, 128)) {
        YA_CORE_WARN("GUIAppHost: failed to load UI font face '{}'; text drawing disabled", config.uiFontFace);
    }

    // Acquire the system DPI scale ONCE at startup (real device pixel ratio
    // from the window-system, not an extent ratio) and publish it to the font
    // manager before any glyph is rasterized. Refreshed again on resize /
    // monitor move (see onResize).
    refreshDevicePixelRatio();

    // 5. GUI Draw2D renderer (screen-space sprites, depth-less pipeline),
    //    matching the swapchain's real surface format.
    ISwapchain* swapchain = _impl->present->getSwapchain();
    YA_CORE_ASSERT(swapchain != nullptr, "GUIAppHost requires a present swapchain");
    Render2D::init(render, swapchain->getFormat(), EFormat::Undefined);
    _impl->presentPassSlot   = Render2D::acquirePassSlot();
    _impl->offscreenPassSlot = Render2D::acquirePassSlot();

    // 5b. Game UI WidgetTree closure: layout + immutable snapshot without any
    //     Scene / ECS / Host / Render3D dependency. SDL input is routed into
    //     the same tree that produces the snapshot.
    _impl->tree = std::make_unique<WidgetTree>(Extent2D{
        .width  = swapchain->getExtent().width,
        .height = swapchain->getExtent().height,
    });
    _impl->textureSource.render = render;
    _impl->tree->setTextureSource(&_impl->textureSource);
    bindSdlClipboard(*_impl->tree);
    if (!_impl->automationServer.init(config.automation.controlPort)) {
        YA_CORE_ERROR("GUIAppHost: failed to initialize automation control server on port {}",
                      config.automation.controlPort);
        shutdown();
        return false;
    }
    registerAutomationMethods();
    _impl->delegate->buildUI(*_impl->tree);
    if (!config.scenarioPath.empty()) {
        auto scenario = std::make_unique<GuiScenarioEventSource>();
        std::string scenarioError;
        scenario->steps = loadGuiScenarioFile(config.scenarioPath, &scenarioError);
        if (!scenarioError.empty()) {
            YA_CORE_ERROR("GUIAppHost: failed to load scenario '{}': {}", config.scenarioPath, scenarioError);
            shutdown();
            return false;
        }
        scenario->onCheckpoint = [this](const std::string& tag) { dumpScenarioCheckpoint(tag); };
        scenario->onAssertValidationClean = [this]() {
            const uint64_t mismatches = _impl->tree->getValidationMismatches();
            if (mismatches != 0) {
                YA_CORE_ERROR("GUIAppHost scenario assert_validation_clean failed: {} validation mismatch(es)",
                              mismatches);
                _impl->bScenarioFailed = true;
                return false;
            }
            return true;
        };
        scenario->onAssert = [this](std::string_view assertion) {
            std::string error;
            const bool bPass = assertScenarioTree(*_impl->tree, assertion, error);
            if (!bPass) {
                YA_CORE_ERROR("GUIAppHost scenario assertion failed: {}", error);
                _impl->bScenarioFailed = true;
            }
            else {
                YA_CORE_INFO("GUIAppHost scenario assertion passed: {}", assertion);
            }
            return bPass;
        };
        scenario->onSetWindowSize = [this](uint32_t width, uint32_t height) {
            if (!requestWindowSize(width, height, "scenario")) {
                _impl->bQuitRequested = true;
            }
        };
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
        sdl->setHostWindowId(_impl->window.getWindowID());
        _impl->sdlEventSource = sdl.get();
        _impl->eventSource = std::move(sdl);
    }

    // The shared present path reads this window's device and surface from the
    // resources struct itself (the same shape the extra windows' sessions fill
    // in) -- name them before building anything through it.
    _impl->presentResources.render  = render;
    _impl->presentResources.present = _impl->present;

    // Presentation resources for this window's surface: command buffers plus
    // one imported compose target per swapchain image, and the swapchain
    // identity the shared present path re-checks every frame. Built by the
    // same helper every GUI window rebuilds with.
    rebuildGuiSurfacePresentation(_impl->presentResources, "GUIApp", /*bWaitForGpu=*/false);
    if (_impl->presentResources.presentationTargets.empty()) {
        YA_CORE_ERROR("GUIAppHost: failed to build presentation targets for this window's surface");
        return false;
    }
    _impl->tree->setLogicalExtent(queryWindowLogicalExtent(_impl->window));

    _impl->bInitialized = true;
    return true;
}

void GUIWindowHost::dispatchToTree(const Event& event, float mouseX, float mouseY)
{
    const glm::vec2 point{mouseX, mouseY};
    bool            bPopupOpen = false;
    if (UIElement* popup = _impl->tree->getLayer(WidgetTree::ELayer::Popup)) {
        bPopupOpen = !popup->getChildren().empty();
    }
    if (handleGuiFrameInspectorHudInput(event, point, _impl->tree->getLogicalExtent(), bPopupOpen)) {
        _impl->delegate->onRoutedEvent(event, EWidgetRouteResult::HandledExclusive);
        updateCursor();
        return;
    }

    WidgetEventContext ctx;
    ctx.logicalPoint = point;
    const EWidgetRouteResult result = _impl->tree->dispatchEvent(event, ctx);
    _impl->delegate->onRoutedEvent(event, result);
    updateCursor();
}

void GUIWindowHost::updateCursor()
{
    if (!_impl->tree) {
        return;
    }
    ECursorType cursor = ECursorType::Arrow;
    if (const UIElement* hovered = _impl->tree->getHovered()) {
        cursor = hovered->getCursor();
    }
    OsCursor::set(cursor);
}

bool GUIWindowHost::requestWindowSize(uint32_t width, uint32_t height, std::string_view reason)
{
    if (width == 0 || height == 0) {
        YA_CORE_ERROR("GUIAppHost {}: invalid window size {}x{}", reason, width, height);
        return false;
    }
    if (!_impl->window.setWindowSize(static_cast<int>(width), static_cast<int>(height))) {
        YA_CORE_ERROR("GUIAppHost {}: failed to set window size to {}x{}", reason, width, height);
        return false;
    }
    _impl->bWindowMinimized          = false;
    _impl->bSwapchainRecreatePending = true;
    return true;
}

// === Automation methods (registered in init, dispatched per frame) ===
//
// The control server's requests are the same verbs the CLI flags cover, so a
// running app can be driven without restarting it. Each handler completes its
// request; the frame loop only dispatches (see dispatchAutomationRequests).

void GUIWindowHost::registerAutomationMethods()
{
    using RequestPtr = AppAutomationControlServer::RequestPtr;
    AutomationMethodRegistry& methods = _impl->automationMethods;
    methods.add("ping", [this](const RequestPtr& request) { onAutomationPing(request); });
    methods.add("quit", [this](const RequestPtr& request) { onAutomationQuit(request); });
    methods.add("dump_tree", [this](const RequestPtr& request) { onAutomationDumpTree(request); });
    methods.add("set_window_size", [this](const RequestPtr& request) { onAutomationSetWindowSize(request); });
    methods.add("mouse_move", [this](const RequestPtr& request) { onAutomationMouseMove(request); });
    methods.add("mouse_press", [this](const RequestPtr& request) { onAutomationMousePress(request); });
    methods.add("mouse_release", [this](const RequestPtr& request) { onAutomationMouseRelease(request); });
    methods.add("capture_screenshot", [this](const RequestPtr& request) { onAutomationCaptureScreenshot(request); });
}

void GUIWindowHost::dispatchAutomationRequests()
{
    for (auto& request : _impl->automationServer.consumePendingRequests()) {
        _impl->automationMethods.dispatch(request, _impl->automationServer);
    }
}

void GUIWindowHost::onAutomationPing(const AppAutomationControlServer::RequestPtr& request)
{
    _impl->automationServer.completeRequest(
        request,
        makeAutomationSuccess(*request,
                              {
                                  {"service", "gui-automation-control"},
                                  {"port", _impl->automationServer.getPort()},
                                  {"title", _impl->config->title},
                              }));
}

void GUIWindowHost::onAutomationQuit(const AppAutomationControlServer::RequestPtr& request)
{
    _impl->bQuitRequested = true;
    _impl->automationServer.completeRequest(request, makeAutomationSuccess(*request));
}

void GUIWindowHost::onAutomationDumpTree(const AppAutomationControlServer::RequestPtr& request)
{
    // Live tree dump for on-device assertions (the scenario dump
    // equivalent when driving the app through the automation port).
    _impl->automationServer.completeRequest(
        request, makeAutomationSuccess(*request, dumpWidgetTree(*_impl->tree)));
}

void GUIWindowHost::onAutomationSetWindowSize(const AppAutomationControlServer::RequestPtr& request)
{
    const auto widthIt  = request->params.find("width");
    const auto heightIt = request->params.find("height");
    if (widthIt == request->params.end() || heightIt == request->params.end() ||
        !widthIt->is_number_integer() || !heightIt->is_number_integer()) {
        _impl->automationServer.completeRequest(
            request,
            makeAutomationError(*request, "set_window_size requires integer params {width,height}"));
        return;
    }
    const int width  = widthIt->get<int>();
    const int height = heightIt->get<int>();
    if (width <= 0 || height <= 0) {
        _impl->automationServer.completeRequest(
            request,
            makeAutomationError(*request, "set_window_size expects positive width and height"));
        return;
    }
    if (!requestWindowSize(static_cast<uint32_t>(width), static_cast<uint32_t>(height), "automation")) {
        _impl->automationServer.completeRequest(
            request,
            makeAutomationError(*request, std::format("failed to set window size to {}x{}", width, height)));
        return;
    }
    _impl->automationServer.completeRequest(
        request,
        makeAutomationSuccess(*request, {{"width", width}, {"height", height}}));
}

void GUIWindowHost::onAutomationMouseMove(const AppAutomationControlServer::RequestPtr& request)
{
    // Pointer injection drives hover/click regressions deterministically:
    // the same core events SDL emits, but scheduled from the control
    // protocol so a test harness can assert on the hover owner afterward.
    const auto xIt = request->params.find("x");
    const auto yIt = request->params.find("y");
    if (xIt == request->params.end() || yIt == request->params.end() ||
        !xIt->is_number() || !yIt->is_number()) {
        _impl->automationServer.completeRequest(
            request,
            makeAutomationError(*request, "mouse_move requires number params {x,y}"));
        return;
    }
    const float x = xIt->get<float>();
    const float y = yIt->get<float>();
    _impl->lastMouseX = x;
    _impl->lastMouseY = y;
    dispatchToTree(MouseMoveEvent(x, y), x, y);
    const UIElement* hovered = _impl->tree->getHovered();
    _impl->automationServer.completeRequest(
        request,
        makeAutomationSuccess(*request,
                              {{"hovered", hovered ? hovered->_name : std::string{}}}));
}

void GUIWindowHost::onAutomationMousePress(const AppAutomationControlServer::RequestPtr& request)
{
    const auto buttonIt = request->params.find("button");
    const auto button   = (buttonIt != request->params.end() && buttonIt->is_number_integer())
                              ? static_cast<EMouse::T>(buttonIt->get<int>())
                              : EMouse::Left; // SDL codes: 1 = left, 3 = right
    dispatchToTree(MouseButtonPressedEvent(button), _impl->lastMouseX, _impl->lastMouseY);
    _impl->automationServer.completeRequest(request, makeAutomationSuccess(*request));
}

void GUIWindowHost::onAutomationMouseRelease(const AppAutomationControlServer::RequestPtr& request)
{
    const auto buttonIt = request->params.find("button");
    const auto button   = (buttonIt != request->params.end() && buttonIt->is_number_integer())
                              ? static_cast<EMouse::T>(buttonIt->get<int>())
                              : EMouse::Left; // SDL codes: 1 = left, 3 = right
    dispatchToTree(MouseButtonReleasedEvent(button), _impl->lastMouseX, _impl->lastMouseY);
    _impl->automationServer.completeRequest(request, makeAutomationSuccess(*request));
}

void GUIWindowHost::onAutomationCaptureScreenshot(const AppAutomationControlServer::RequestPtr& request)
{
    // GUI offscreen parity capture: the request is deferred until the
    // frame loop reaches the warmup frame, captures the requested
    // surface(s) and (for parity) diffs them, then completes the request.
    const auto targetIt = request->params.find("target");
    const std::string target = (targetIt != request->params.end() && targetIt->is_string())
                                   ? targetIt->get<std::string>()
                                   : "parity";
    if (target != "gpu" && target != "offscreen" && target != "parity") {
        _impl->automationServer.completeRequest(
            request,
            makeAutomationError(*request,
                                "capture_screenshot params.target must be 'gpu', 'offscreen' or 'parity'"));
        return;
    }
    const auto pathIt = request->params.find("path");
    if (pathIt == request->params.end() || !pathIt->is_string() ||
        pathIt->get<std::string>().empty()) {
        _impl->automationServer.completeRequest(
            request,
            makeAutomationError(*request, "capture_screenshot requires non-empty params.path"));
        return;
    }
    if (_impl->pendingCapture) {
        _impl->automationServer.completeRequest(
            request,
            makeAutomationError(*request, "a capture request is already in flight"));
        return;
    }

    const std::string basePath      = pathIt->get<std::string>();
    const uint64_t    warmupFrames  = request->params.value("warmup_frames", static_cast<uint64_t>(2));

    PendingGuiCapture capture;
    capture.waiter = request;
    if (target == "gpu") {
        capture.gpuPath = basePath;
    }
    else if (target == "offscreen") {
        capture.offscreenPath = basePath;
    }
    else { // parity
        capture.gpuPath       = basePath + ".gpu.bmp";
        capture.offscreenPath = basePath + ".offscreen.bmp";
        capture.diffPath      = basePath + ".diff.bmp";
    }
    capture.earliestFrame = _impl->frameCount + warmupFrames;
    _impl->pendingCapture = std::move(capture);
}

int GUIWindowHost::run()
{
    if (!_impl->bInitialized) {
        YA_CORE_ERROR("GUIWindowHost::run called before a successful init()");
        return 1;
    }

    AppKernel kernel({.eventSource = _impl->eventSource.get()}, *this);
    return finishRun(kernel.run(_impl->config->automation));
}

IAppEventSource* GUIWindowHost::getEventSource()
{
    return _impl->eventSource.get();
}

const FGUIWindowHostConfig& GUIWindowHost::getConfig() const
{
    return *_impl->config;
}

const FWindowChromeState& GUIWindowHost::windowChrome() const
{
    return _impl->chrome;
}

uint32_t GUIWindowHost::getWindowID() const
{
    return _impl->window.getWindowID();
}

INativeWindow* GUIWindowHost::getNativeWindow()
{
    return &_impl->window;
}

const INativeWindow* GUIWindowHost::getNativeWindow() const
{
    return &_impl->window;
}

IRender* GUIWindowHost::getRender() const
{
    return _impl->bInitialized ? _impl->render : nullptr;
}

void GUIWindowHost::setAcceptAllWindowEvents(bool enabled)
{
    if (!_impl->sdlEventSource) {
        return;
    }
    _impl->sdlEventSource->setHostWindowId(enabled ? 0u : _impl->window.getWindowID());
}

int GUIWindowHost::finishRun(int kernelResult)
{
    if (kernelResult != 0) {
        return kernelResult;
    }
    if (_impl->bScenarioFailed) {
        return 4;
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

    if (!_impl->config->offscreenDiffPath.empty() &&
        !_impl->config->gpuShotPath.empty() &&
        !_impl->config->offscreenShotPath.empty()) {
        const BmpDiffResult diff = diffBmpFiles(_impl->config->gpuShotPath,
                                                _impl->config->offscreenShotPath,
                                                _impl->config->offscreenDiffPath,
                                                0, 0.0f);
        YA_CORE_INFO("GUIAppHost offscreen parity diff: pass={} differing={} ratio={:.4f}",
                     diff.bPass, diff.differingPixels, diff.diffRatio);
        if (!diff.bPass) {
            return 3;
        }
    }

    return 0;
}

void GUIWindowHost::onInit() {}
void GUIWindowHost::onShutdown() {}

void GUIWindowHost::onEvent(const Event& event)
{
    switch (event.getEventType()) {
    case EEvent::AppQuit:
        _impl->bQuitRequested = true;
        return;
    case EEvent::WindowClose: {
        const uint32_t closeId = static_cast<const WindowCloseEvent&>(event).getWindowID();
        const uint32_t hostId  = _impl->window.getWindowID();
        if (closeId == 0 || closeId == hostId) {
            _impl->bQuitRequested = true;
        }
        return;
    }
    case EEvent::WindowResize: {
        const auto& resize = static_cast<const WindowResizeEvent&>(event);
        _impl->bWindowMinimized = resize.GetWidth() == 0 || resize.GetHeight() == 0;
        _impl->bSwapchainRecreatePending = true;
        // Per-monitor DPI: a window dragged to a different display keeps its
        // old scale until refreshed. Re-read the window-system content scale
        // and republish so fonts re-raster at the new device resolution.
        _impl->window.refreshDpiScale();
        refreshDevicePixelRatio();
        _impl->chrome = applyWindowChrome(_impl->window, _impl->chrome.mode, _impl->config->bResizable);
        return;
    }
    case EEvent::WindowMoved: {
        // Monitor move without a size change: SDL does not always emit
        // WindowResize here, so re-read the display scale explicitly to keep
        // DPI / font raster in sync with the new monitor (Qt-style trap).
        _impl->window.refreshDpiScale();
        refreshDevicePixelRatio();
        _impl->chrome = applyWindowChrome(_impl->window, _impl->chrome.mode, _impl->config->bResizable);
        return;
    }
    case EEvent::WindowMinimize:
        _impl->bWindowMinimized = true;
        _impl->bSwapchainRecreatePending = true;
        return;
    case EEvent::WindowFocusLost:
    case EEvent::WindowMouseLeave: {
        // Key focus loss and the pointer leaving this window both stop the
        // platform from delivering the pointer stream it started here. Compare
        // the tree's cached press against the physical button state: a press
        // whose button is already up can never be completed, and leaving it
        // armed is what turns the next click into a crash.
        const bool bFocusLost = event.getEventType() == EEvent::WindowFocusLost;
        if (_impl->tree) {
            _impl->tree->reconcilePointerButtons(
                OsEventPump::queryGlobalMouse().buttonMask,
                bFocusLost ? "window lost key focus" : "pointer left the window");
        }
        return;
    }
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
    case EEvent::MouseButtonPressed: {
        const auto& press = static_cast<const MouseButtonPressedEvent&>(event);
        if (press.GetMouseButton() == EMouse::Left && press.clickCount() >= 2 &&
            handleWindowChromeTitleDoubleClick(_impl->window, _impl->lastMouseX, _impl->lastMouseY)) {
            return;
        }
        dispatchToTree(event, _impl->lastMouseX, _impl->lastMouseY);
        return;
    }
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

float GUIWindowHost::refreshDevicePixelRatio()
{
    float scale = _impl->window.getDpiScale();
    if (scale <= 0.0f) {
        scale = 1.0f;
    }
    _impl->devicePixelRatio = scale;
    if (_impl->tree) {
        _impl->tree->publishDpiScale(scale);
    }
    else {
        // init() calls this before the tree exists; the font stack is the
        // consumer that early. The first tick publishes to the tree too.
        FontManager::get()->setActiveDpiScale(scale);
    }
    return scale;
}

bool GUIWindowHost::shouldClose() const
{
    return _impl->bQuitRequested || _impl->delegate->shouldRequestClose();
}

void GUIWindowHost::onTick(float dt)
{
    // One window's frame is exactly its content and its presentation (see the
    // header): an app with several windows calls the two halves separately, so
    // no window is presented before every window has ticked.
    //
    // The frame begins here too: this is the one place a single-window host
    // records a frame, so it is where the frame's GPU work is retired and its
    // flight slot freed. A GUI app that drives several windows through
    // `tickContent`/`presentSnapshot` calls this once for the whole frame (see
    // `GUIApp::onTick`) instead.
    if (_impl->render) {
        _impl->render->beginRecordedFrame();
    }
    tickContent(dt);
    presentSnapshot();
}

void GUIWindowHost::tickContent(float dt)
{
    // Events are delivered by the kernel event phase (via onEvent) before
    // this tick. Drive live widget lifecycle before application-level state
    // synchronization and snapshot generation.
    if (_impl->bQuitRequested) {
        return;
    }
    ++_impl->frameCount;
    _impl->tree->tick(dt);

    dispatchAutomationRequests();

    if (_impl->bSwapchainRecreatePending) {
        _impl->present->requestRecreate();
        _impl->bSwapchainRecreatePending = false;
    }
    if (_impl->config->bScenarioRender && _impl->bScenarioMode) {
        // G-C: scenario-render runs full logical frames WITHOUT touching the
        // swapchain (scenario windows may never be presentable, and
        // render->begin can fail or crash on an unpresented surface). The
        // frame degrades to a snapshot-only pass: buildSnapshot drives
        // layout + paint + the G2 validation frame, nothing is submitted.
        _impl->tree->setLogicalExtent(queryWindowLogicalExtent(_impl->window));
        _impl->delegate->updateUI();
        // Scenario frames have no presentable swapchain, but still use the
        // window's device-pixel-ratio (1.0 when headless) as the DPI mapping so
        // font scale matches the runtime path — no ad-hoc magic constant. The
        // user zoom stays separate (uiUserScale, default 1.0).
        _impl->tree->publishDpiScale(_impl->devicePixelRatio);
        _impl->snapshot = _impl->tree->buildSnapshot(UIFrameBuildContext{
            .uiScale         = {_impl->uiUserScale, _impl->uiUserScale},
            .offset          = {0.0f, 0.0f},
            .textureResolver = resolveBuiltinTexture,
        });
        _impl->bSnapshotBuilt = true;
        // Safe-point glyph flush (Core Rule 6): this path never records
        // commands, so pending glyph capture can run here too.
        FontManager::get()->flushPendingGlyphs(*_impl->render);
        (void)FontManager::get()->consumeNewGlyphCapture();
        ++_impl->frameCount;
        return;
    }

    _impl->tree->setLogicalExtent(queryWindowLogicalExtent(_impl->window));
    _impl->delegate->updateUI();
    _impl->tree->publishDpiScale(_impl->devicePixelRatio);
    UIFrameSnapshot snapshot = _impl->tree->buildSnapshot(UIFrameBuildContext{
        .uiScale         = {_impl->uiUserScale, _impl->uiUserScale},
        .offset          = {0.0f, 0.0f},
        .textureResolver = resolveBuiltinTexture,
    });
    if (_impl->config && _impl->config->bDebugRenderOverlay) {
        appendDebugRenderOverlay(snapshot, *_impl->tree);
    }
    if (_impl->config && !_impl->config->dumpSnapshotPath.empty() &&
        _impl->frameCount == _impl->config->dumpFrame) {
        dumpSnapshotToBMP(snapshot, _impl->config->dumpSnapshotPath, _impl->frameCount);
    }
    if (_impl->config && !_impl->config->dumpSnapshotJsonPath.empty() &&
        _impl->frameCount == _impl->config->dumpFrame) {
        std::ofstream output(_impl->config->dumpSnapshotJsonPath);
        if (output) {
            auto dump = dumpUIFrameSnapshot(snapshot);
            dump["structuralDigest"] = digestUIFrameSnapshot(snapshot);
            dump["semanticDigest"]   = semanticDigestUIFrameSnapshot(snapshot);
            output << dump.dump(2);
            YA_CORE_INFO("GUIAppHost wrote snapshot JSON to '{}' (structuralDigest={} semanticDigest={})",
                         _impl->config->dumpSnapshotJsonPath,
                         dump["structuralDigest"].get<uint64_t>(),
                         dump["semanticDigest"].get<uint64_t>());
        }
        else {
            YA_CORE_ERROR("GUIAppHost: cannot write snapshot JSON '{}'",
                          _impl->config->dumpSnapshotJsonPath);
        }
    }

    // This window's frame is published here: everything below presents it, and
    // a registry of windows runs that presentation for every window after every
    // window has reached this point.
    _impl->snapshot      = std::move(snapshot);
    _impl->bSnapshotBuilt = true;
}

const UIFrameSnapshot* GUIWindowHost::getSnapshot() const
{
    return _impl->bSnapshotBuilt ? &_impl->snapshot : nullptr;
}

WidgetTree* GUIWindowHost::tree() const
{
    return _impl->tree.get();
}

INativeWindow* GUIWindowHost::nativeWindow() const
{
    return &_impl->window;
}

IRenderSurfaceContext* GUIWindowHost::surfaceContext() const
{
    return _impl->present;
}

bool GUIWindowHost::isMinimized() const
{
    return _impl->bWindowMinimized;
}

bool GUIWindowHost::closeRequested() const
{
    return _impl->bQuitRequested;
}

void GUIWindowHost::presentSnapshot()
{
    // Scenario frames have no presentable swapchain (see tickContent); the
    // snapshot they built is still the window's current content.
    if (_impl->config && _impl->config->bScenarioRender && _impl->bScenarioMode) {
        return;
    }
    if (!_impl->bSnapshotBuilt || !_impl->render || !_impl->present) {
        return;
    }
    const UIFrameSnapshot& snapshot = _impl->snapshot;

    // The present sequence itself is the shared one every GUI window runs
    // (`presentGuiSnapshot`). What this window adds, per acquired image, is
    // captured by the two hooks below and spelled out in
    // `recordPresentExtensions`: the first-frame stats line, the inspector
    // overlay inside the compose pass, and the automation captures whose
    // readback copies must land in the same submission.
    std::optional<PendingGuiCapture> captured; // automation request consumed this frame
    std::string                      capturePath;   // non-empty: a GPU shot was recorded
    std::string                      offscreenPath; // decided path (mirror recorded iff bCaptureOffscreen)
    bool                             bCaptureOffscreen = false;
    Extent2D                         presentedExtent{};
    std::shared_ptr<RenderTexture>   offscreenImage;
    auto preSubmit = [this, &snapshot, &captured, &capturePath, &offscreenPath, &bCaptureOffscreen,
                      &presentedExtent, &offscreenImage](const FGUIPresentExtensionContext& ctx)
    {
        if (!_impl->bLoggedFirstSnapshot) {
            _impl->bLoggedFirstSnapshot = true;
            const GuiPerfStats& stats   = _impl->tree->getPerfStats();
            YA_CORE_INFO("GUIAppHost first snapshot: {} draw items, {} widgets painted, layout {:.3f}ms paint {:.3f}ms, {}x{} logical -> {}x{} render",
                         snapshot.items.size(),
                         stats.paintedWidgets,
                         stats.layoutMS,
                         stats.paintMS,
                         snapshot.logicalExtent.width,
                         snapshot.logicalExtent.height,
                         ctx.presentExtent.width,
                         ctx.presentExtent.height);
        }

        // Runtime automation capture (GUI offscreen parity): the control server
        // defers the request until this frame loop reaches its warmup frame.
        // Consumed here -- the point where the old inline sequence decided it --
        // so a skipped frame (minimized / acquire refused) keeps the request.
        if (_impl->pendingCapture && _impl->frameCount >= _impl->pendingCapture->earliestFrame) {
            captured = std::move(_impl->pendingCapture);
            _impl->pendingCapture.reset();
        }
        offscreenPath = captured && !captured->offscreenPath.empty()
                            ? captured->offscreenPath
                            : _impl->config->offscreenShotPath;
        bCaptureOffscreen =
            (captured && !captured->offscreenPath.empty()) ||
            (_impl->config->offscreenShotFrame != 0 &&
             _impl->frameCount == _impl->config->offscreenShotFrame &&
             !_impl->config->offscreenShotPath.empty());
        if (bCaptureOffscreen) {
            bCaptureOffscreen = recordOffscreenParityCapture(ctx, snapshot, offscreenImage);
        }

        if (captured && !captured->gpuPath.empty()) {
            capturePath = captured->gpuPath;
        }
        else if (_impl->config->gpuShotFrame != 0 &&
                 _impl->frameCount == _impl->config->gpuShotFrame &&
                 !_impl->config->gpuShotPath.empty()) {
            capturePath = _impl->config->gpuShotPath;
        }
        else if (!_impl->captureRequestPath.empty()) {
            capturePath = _impl->captureRequestPath;
            _impl->captureRequestPath.clear();
        }
        if (!capturePath.empty()) {
            recordGpuShotCopy(ctx);
        }
        presentedExtent = ctx.presentExtent;
    };

    presentGuiSnapshot(_impl->presentResources,
                       snapshot,
                       _impl->tree->getLogicalExtent(),
                       _impl->presentPassSlot,
                       _impl->bWindowMinimized,
                       _impl->bSwapchainRecreatePending,
                       /*composeExtra=*/[this, &snapshot](const FGUIPresentExtensionContext& ctx, Render2DList& list)
                       {
                           runGuiFrameInspectorOverlay(*_impl->tree,
                                                       snapshot,
                                                       list,
                                                       Extent2D{.width = ctx.presentExtent.width,
                                                                .height = ctx.presentExtent.height});
                       },
                       preSubmit);

    // The captures are complete once the submit retires: map the readback
    // staging buffers and encode. Parity additionally diffs gpu vs offscreen
    // at zero tolerance before completing the automation request.
    if (!capturePath.empty() || bCaptureOffscreen) {
        _impl->present->waitInFlight();
        if (!capturePath.empty() && _impl->gpuShotBuffer) {
            if (uint8_t* pixels = _impl->gpuShotBuffer->map<uint8_t>()) {
                auto* swapchain = _impl->present->getSwapchain();
                writeRGBAtoBMP(pixels,
                               presentedExtent.width,
                               presentedExtent.height,
                               swapchain && swapchain->getFormat() == EFormat::B8G8R8A8_UNORM,
                               capturePath);
                _impl->gpuShotBuffer->unmap();
                YA_CORE_INFO("GUIAppHost wrote GPU shot to '{}' ({}x{})",
                             capturePath,
                             presentedExtent.width,
                             presentedExtent.height);
            }
        }
        if (bCaptureOffscreen && _impl->offscreenShotBuffer) {
            if (uint8_t* pixels = _impl->offscreenShotBuffer->map<uint8_t>()) {
                writeRGBAtoBMP(pixels,
                               presentedExtent.width,
                               presentedExtent.height,
                               offscreenImage && offscreenImage->getFormat() == EFormat::B8G8R8A8_UNORM,
                               offscreenPath);
                _impl->offscreenShotBuffer->unmap();
                YA_CORE_INFO("GUIAppHost wrote offscreen shot to '{}' ({}x{})",
                             offscreenPath,
                             presentedExtent.width,
                             presentedExtent.height);
            }
        }
    }
    if (captured) {
        nlohmann::json result = {
            {"gpu_path", captured->gpuPath},
            {"offscreen_path", captured->offscreenPath},
        };
        if (!captured->diffPath.empty()) {
            const BmpDiffResult diff = diffBmpFiles(captured->gpuPath,
                                                    captured->offscreenPath,
                                                    captured->diffPath,
                                                    0, 0.0f);
            result["diff_path"]        = captured->diffPath;
            result["pass"]             = diff.bPass;
            result["differing_pixels"] = diff.differingPixels;
            result["diff_ratio"]       = diff.diffRatio;
            YA_CORE_INFO("GUIAppHost offscreen parity diff: pass={} differing={} ratio={:.4f}",
                         diff.bPass, diff.differingPixels, diff.diffRatio);
        }
        _impl->automationServer.completeRequest(captured->waiter,
                                                makeAutomationSuccess(*captured->waiter, std::move(result)));
    }
}

bool GUIWindowHost::recordOffscreenParityCapture(const FGUIPresentExtensionContext& ctx,
                                                 const UIFrameSnapshot&             snapshot,
                                                 std::shared_ptr<RenderTexture>&    outImage)
{
    // The mirror is sized and formatted from the presented image, and the
    // check below is what makes a swapchain rebuild self-heal on the next
    // capture -- there is no dedicated teardown for it.
    const auto& presentedImage = ctx.presentedSurface.getRenderImage();
    if (!_impl->offscreenSurface ||
        !_impl->offscreenSurface->isValid() ||
        _impl->offscreenSurface->getRenderImage()->getExtent() != ctx.presentExtent ||
        _impl->offscreenSurface->getRenderImage()->getFormat() != presentedImage->getFormat()) {
        _impl->offscreenSurface = GUIRenderSurface::createOffscreen(
            *_impl->render->getResourceFactory(),
            FGUIRenderSurfaceDesc{
                .label       = "GUIAppHost_OffscreenMirror",
                .extent      = ctx.presentExtent,
                .colorFormat = presentedImage->getFormat(),
            });
    }
    if (!_impl->offscreenSurface || !_impl->offscreenSurface->isValid()) {
        YA_CORE_ERROR("GUIAppHost: unable to create offscreen parity surface");
        return false;
    }
    _impl->offscreenSurface->prepare(FRender2DComposePassDesc{
        .kind     = ERender2DComposePassKind::RuntimeUIOffscreen,
        .passSlot = _impl->offscreenPassSlot,
    });
    _impl->offscreenSurface->record(
        &ctx.cmdBuf,
        nullptr,
        &snapshot,
        FRender2DComposePassDesc{
            .kind                  = ERender2DComposePassKind::RuntimeUIOffscreen,
            .passSlot              = _impl->offscreenPassSlot,
            .logicalExtent = _impl->tree->getLogicalExtent(),
        });
    outImage = _impl->offscreenSurface->getRenderImage();

    const uint32_t requiredReadbackSize = ctx.presentExtent.width * ctx.presentExtent.height * 4;
    if (!_impl->offscreenShotBuffer || _impl->offscreenShotBuffer->getSize() != requiredReadbackSize) {
        _impl->offscreenShotBuffer = _impl->render->getResourceFactory()->createBuffer(
            ya::BufferCreateInfo{
                .label       = "GUIAppHost_OffscreenShot",
                .usage       = EBufferUsage::TransferDst,
                .size        = requiredReadbackSize,
                .memoryUsage = EMemoryUsage::GpuToCpu,
            });
    }
    ctx.cmdBuf.transitionImageLayoutAuto(outImage->getImage(), EImageLayout::TransferSrc);
    ctx.cmdBuf.copyImageToBuffer(
        outImage->getImage(),
        EImageLayout::TransferSrc,
        _impl->offscreenShotBuffer.get(),
        {ya::BufferImageCopy{
            .imageSubresource  = {.aspectMask = 1, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
            .imageOffsetX      = 0,
            .imageOffsetY      = 0,
            .imageOffsetZ      = 0,
            .imageExtentWidth  = ctx.presentExtent.width,
            .imageExtentHeight = ctx.presentExtent.height,
            .imageExtentDepth  = 1,
        }});
    ctx.cmdBuf.transitionImageLayoutAuto(outImage->getImage(), _impl->offscreenSurface->getFinalLayout());
    return true;
}

void GUIWindowHost::recordGpuShotCopy(const FGUIPresentExtensionContext& ctx)
{
    const uint32_t requiredReadbackSize = ctx.presentExtent.width * ctx.presentExtent.height * 4;
    if (!_impl->gpuShotBuffer || _impl->gpuShotBuffer->getSize() != requiredReadbackSize) {
        _impl->gpuShotBuffer = _impl->render->getResourceFactory()->createBuffer(
            ya::BufferCreateInfo{
                .label       = "GUIAppHost_GpuShot",
                .usage       = EBufferUsage::TransferDst,
                .size        = requiredReadbackSize,
                .memoryUsage = EMemoryUsage::GpuToCpu,
            });
    }
    const auto& renderImage = ctx.presentedSurface.getRenderImage();
    ctx.cmdBuf.transitionImageLayoutAuto(renderImage->getImage(), EImageLayout::TransferSrc);
    ctx.cmdBuf.copyImageToBuffer(
        renderImage->getImage(),
        EImageLayout::TransferSrc,
        _impl->gpuShotBuffer.get(),
        {ya::BufferImageCopy{
            .imageSubresource  = {.aspectMask = 1, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
            .imageOffsetX      = 0,
            .imageOffsetY      = 0,
            .imageOffsetZ      = 0,
            .imageExtentWidth  = ctx.presentExtent.width,
            .imageExtentHeight = ctx.presentExtent.height,
            .imageExtentDepth  = 1,
        }});
    ctx.cmdBuf.transitionImageLayoutAuto(renderImage->getImage(), ctx.presentedSurface.getFinalLayout());
}

void GUIWindowHost::injectEvent(const Event& event, const glm::vec2& logicalPoint)
{
    dispatchToTree(event, logicalPoint.x, logicalPoint.y);
}

bool GUIWindowHost::isInitialized() const
{
    return _impl->bInitialized;
}

void GUIWindowHost::dumpScenarioCheckpoint(const std::string& tag)
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

WidgetTree& GUIWindowHost::getTree()
{
    return *_impl->tree;
}

void GUIWindowHost::shutdown()
{
    if (!_impl->bInitialized) {
        return;
    }

    // Drop this window's published frame first: a snapshot refers to the fonts
    // and textures it was laid out with (atlas pages, images), and this is the
    // last moment they are still alive. Holding it across the render teardown
    // below is how a font atlas ends up unfreed.
    _impl->snapshot = {};
    _impl->bSnapshotBuilt = false;

    // Clean shutdown, reverse order. Every member owning GPU resources must be
    // released BEFORE the Vulkan device / VMA allocator is destroyed below
    // (a later ~VulkanBuffer would call vmaDestroyBuffer on a dead allocator).
    _impl->render->waitIdle();
    Render2D::releasePassSlot(_impl->presentPassSlot);
    Render2D::releasePassSlot(_impl->offscreenPassSlot);
    _impl->presentPassSlot   = kInvalidRender2DPassSlot;
    _impl->offscreenPassSlot = kInvalidRender2DPassSlot;
    if (_impl->pendingCapture) {
        _impl->automationServer.completeRequest(
            _impl->pendingCapture->waiter,
            makeAutomationError(*_impl->pendingCapture->waiter,
                                "capture request canceled during shutdown"));
        _impl->pendingCapture.reset();
    }
    _impl->automationServer.shutdown();
    Render2D::destroy();
    _impl->presentResources.commandBuffers.clear();   // releases command-buffer resource retention
    _impl->presentResources.presentationTargets.clear();
    _impl->gpuShotBuffer.reset();    // readback staging buffer (RHI-owned)
    _impl->offscreenSurface.reset();
    _impl->offscreenShotBuffer.reset();
    _impl->shaderStorage.reset();
    _impl->tree.reset();             // widgets hold snapshot/resolver refs only, but stay ordered
    FontManager::get()->clearCache();
    TextureLibrary::get().shutdown();
    DeferredDeletionQueue::get().flushAll();
    _impl->present = nullptr;
    _impl->render->destroy();
    delete _impl->render;
    _impl->render = nullptr;
    clearWindowChrome(_impl->window);
    _impl->window.stopTextInput();
    _impl->window.destroy();

    _impl->bInitialized = false;
}

GUIApp::GUIApp(const FGUIWindowHostConfig& config, IGUIAppDelegate& delegate)
    : _primaryWindow(config, delegate)
    , _extraWindows(std::make_unique<GUIWindowManager>())
{
}

GUIApp::~GUIApp()
{
    shutdown();
}

bool GUIApp::init()
{
    return _primaryWindow.init();
}

int GUIApp::run()
{
    if (!_primaryWindow.isInitialized()) {
        YA_CORE_ERROR("GUIApp::run called before a successful init()");
        return 1;
    }
    AppKernel kernel({.eventSource = _primaryWindow.getEventSource()}, *this);
    return _primaryWindow.finishRun(kernel.run(_primaryWindow.getConfig().automation));
}

void GUIApp::shutdown()
{
    if (_extraWindows) {
        _extraWindows->shutdown();
    }
    _primaryWindow.shutdown();
}

GUIWindowId GUIApp::openWindow(const FGUIWindowHostConfig& config, IGUIAppDelegate& delegate)
{
    const GUIWindowId id = _extraWindows->create(config, delegate, _primaryWindow.getRender());
    if (id != 0) {
        _primaryWindow.setAcceptAllWindowEvents(true);
    }
    return id;
}

void GUIApp::closeWindow(GUIWindowId id)
{
    _extraWindows->requestClose(id);
}

WidgetTree* GUIApp::findTree(GUIWindowId id)
{
    IGUIWindowSession* session = findSession(id);
    return session ? session->tree() : nullptr;
}

size_t GUIApp::extraWindowCount() const
{
    return _extraWindows->extraWindowCount();
}

IGUIWindowCoordinator& GUIApp::windowCoordinator()
{
    return *_extraWindows;
}

IGUIWindowSession* GUIApp::findSession(GUIWindowId id)
{
    // One registry, so a caller that has a window id does not have to know
    // whether the window is the one the app started with.
    if (_primaryWindow.isInitialized()) {
        const GUIWindowId primaryId = _primaryWindow.getWindowID();
        if (id != 0 && id == primaryId) {
            return &_primaryWindow;
        }
    }
    return _extraWindows->findSession(id);
}

std::vector<IGUIWindowSession*> GUIApp::sessions() const
{
    std::vector<IGUIWindowSession*> out;
    forEachSession([&out](IGUIWindowSession& session) { out.push_back(&session); });
    return out;
}

size_t GUIApp::windowCount() const
{
    size_t count = 0;
    forEachSession([&count](IGUIWindowSession&) { ++count; });
    return count;
}

void GUIApp::forEachSession(const std::function<void(IGUIWindowSession&)>& fn) const
{
    if (_primaryWindow.isInitialized()) {
        fn(const_cast<GUIWindowHost&>(_primaryWindow));
    }
    _extraWindows->forEachSession(fn);
}

void GUIApp::onInit() {}

void GUIApp::onEvent(const Event& event)
{
    if (event.getEventType() == EEvent::AppQuit) {
        _primaryWindow.onEvent(event);
        return;
    }

    adoptDragSource();
    if (routeCrossWindowDrag(event)) {
        applyPointerUniverse();
        return;
    }

    const uint32_t eventId   = guiEventWindowId(event);
    const uint32_t primaryId = _primaryWindow.getWindowID();
    if (eventId != 0 && eventId != primaryId) {
        _extraWindows->dispatchEvent(event);
        adoptDragSource();
        applyPointerUniverse();
        return;
    }

    const bool bKeyEvent = event.getEventType() == EEvent::KeyPressed ||
                           event.getEventType() == EEvent::KeyReleased ||
                           event.getEventType() == EEvent::KeyTyped;
    if (eventId == 0 && bKeyEvent && _extraWindows->focusedWindowId() != 0) {
        if (_extraWindows->dispatchEvent(event)) {
            adoptDragSource();
            applyPointerUniverse();
            return;
        }
    }

    _primaryWindow.onEvent(event);
    adoptDragSource();
    applyPointerUniverse();
}

void GUIApp::onTick(float dt)
{
    applyDeferredCloses();
    // One frame for every window: the frame's bookkeeping runs once, before any
    // window acquires or presents, and no window runs it again (the per-window
    // halves below deliberately do not, see GUIWindowHost::tickContent).
    if (IRender* render = _primaryWindow.isInitialized() ? _primaryWindow.getRender() : nullptr) {
        render->beginRecordedFrame();
    }
    // Every window's content is ticked and snapshotted before any window
    // presents: presenting window A first would leave window B's chrome a frame
    // stale, and a window torn off in this frame would show a tree that was
    // never laid out for its surface. The two halves of a window's frame are
    // what makes that ordering expressible (see GUIWindowHost::tickContent).
    if (_primaryWindow.isInitialized()) {
        _primaryWindow.tickContent(dt);
    }
    // tickAll is this manager's "tick every window's content" (it flushes the
    // frame's close requests first); presentation is the separate step below,
    // which is what keeps every window's content a frame ahead of the first
    // window's presentation.
    _extraWindows->tickAll(dt);
    if (_primaryWindow.isInitialized()) {
        _primaryWindow.presentSnapshot();
    }
    _extraWindows->renderAll();
    if (_extraWindows->extraWindowCount() == 0) {
        _primaryWindow.setAcceptAllWindowEvents(false);
    }
    bindDragRouter();
    applyPointerUniverse();
}

void GUIApp::onShutdown() {}

bool GUIApp::shouldClose() const
{
    return _primaryWindow.shouldClose();
}

void GUIApp::bindDragRouter()
{
    WidgetTree* primaryTree = _primaryWindow.isInitialized() ? &_primaryWindow.getTree() : nullptr;
    INativeWindow* primaryNative = _primaryWindow.isInitialized() ? _primaryWindow.getNativeWindow() : nullptr;
    _dragRouter.bindPrimary(_primaryWindow.getWindowID(), primaryTree, primaryNative);
    _dragRouter.bindExtras(_extraWindows.get());
    _dragRouter.bindRender(_primaryWindow.isInitialized() ? _primaryWindow.getRender() : nullptr);
}

bool GUIApp::isCrossWindowDragActive() const
{
    return _dragRouter.isActive();
}

GUIWindowId GUIApp::crossWindowDragSourceId() const
{
    return _dragRouter.sourceId();
}

GUIWindowId GUIApp::crossWindowDragHoverId() const
{
    return _dragRouter.hoverId();
}

uint32_t GUIApp::crossWindowDragEnterCount() const
{
    return _dragRouter.enterCount();
}

uint32_t GUIApp::crossWindowDragLeaveCount() const
{
    return _dragRouter.leaveCount();
}

void GUIApp::runAfterDrag(std::function<void()> fn)
{
    _dragRouter.runAfterDrag(std::move(fn));
}

void GUIApp::adoptDragSource()
{
    bindDragRouter();
    _dragRouter.adoptSource();
}

void GUIApp::syncCrossWindowDrag()
{
    _dragRouter.sync();
}

void GUIApp::applyPointerUniverse()
{
    _dragRouter.sync();
    _dragRouter.syncTextInput();
    OsCursor::set(_dragRouter.cursor());
}

void GUIApp::finishCrossWindowDrag(EDragFinishResult result)
{
    _dragRouter.finish(result);
}

void GUIApp::cancelCrossWindowDrag()
{
    _dragRouter.cancel();
}

void GUIApp::applyDeferredCloses()
{
    _dragRouter.applyDeferredCloses();
}

bool GUIApp::routeCrossWindowDrag(const Event& event)
{
    bindDragRouter();
    return _dragRouter.route(event);
}

} // namespace ya
