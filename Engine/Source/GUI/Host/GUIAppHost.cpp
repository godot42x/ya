#include "GUI/Host/GUIAppHost.h"
#include "GUI/Host/GUIPresentationTarget.h"

#include "GUI/Host/AppBootstrap.h"
#include "App/Control/BmpDiff.h"
#include "App/Control/AutomationControlServer.h"
#include "App/Control/AutomationRun.h"
#include "App/Control/GuiEventDriver.h"
#include "Core/FName.h"
#include "Core/KeyCode.h"
#include "Core/Log.h"
#include "Core/Common/DeferredDeletionQueue.h"
#include "RHI/Render.h"
#include "RHI/RenderDefines.h"
#include "RHI/Shader.h"
#include "RHI/NativeWindow.h"
#include "RHI/Backend/TextureLibrary.h"
#include "RHI/Backend/Vulkan/VulkanSwapChain.h"
#include "RHI/Core/CommandBuffer.h"

#include "GUI/Compose/Render2DComposePass.h"
#include "GUI/Resources/FontManager.h"
#include "GUI/Draw2D/Render2D.h"
#include "GUI/Widgets/UIFrameSnapshotDump.h"
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

bool jsonContains(const nlohmann::json& actual,
                  const nlohmann::json& expected,
                  std::string_view path,
                  std::string& error)
{
    // Numeric predicates keep resize/drag scenarios semantic: they can
    // assert that geometry changed without baking one machine's exact float
    // result into every checkpoint.
    if (expected.is_object() &&
        (expected.contains("$gt") || expected.contains("$gte") ||
         expected.contains("$lt") || expected.contains("$lte"))) {
        if (!actual.is_number()) {
            error = std::format("{}: comparison requires a number, got {}", path, actual.type_name());
            return false;
        }
        const double value = actual.get<double>();
        const auto check = [&](const char* op, const std::function<bool(double, double)>& predicate) {
            const auto it = expected.find(op);
            if (it == expected.end()) {
                return true;
            }
            if (!it->is_number()) {
                error = std::format("{}: {} must be numeric", path, op);
                return false;
            }
            if (!predicate(value, it->get<double>())) {
                error = std::format("{}: {} {} {} failed", path, value, op, it->dump());
                return false;
            }
            return true;
        };
        return check("$gt", [](double lhs, double rhs) { return lhs > rhs; }) &&
               check("$gte", [](double lhs, double rhs) { return lhs >= rhs; }) &&
               check("$lt", [](double lhs, double rhs) { return lhs < rhs; }) &&
               check("$lte", [](double lhs, double rhs) { return lhs <= rhs; });
    }
    if (expected.is_object()) {
        if (!actual.is_object()) {
            error = std::format("{}: expected object, got {}", path, actual.type_name());
            return false;
        }
        for (const auto& entry : expected.items()) {
            const auto actualIt = actual.find(entry.key());
            if (actualIt == actual.end()) {
                error = std::format("{}: missing field '{}'", path, entry.key());
                return false;
            }
            if (!jsonContains(*actualIt, entry.value(),
                              std::format("{}.{}", path, entry.key()), error)) {
                return false;
            }
        }
        return true;
    }
    if (expected.is_array()) {
        if (actual != expected) {
            error = std::format("{}: expected array {} but got {}", path, expected.dump(), actual.dump());
            return false;
        }
        return true;
    }
    if (actual != expected) {
        error = std::format("{}: expected {} but got {}", path, expected.dump(), actual.dump());
        return false;
    }
    return true;
}

bool assertScenarioTree(const WidgetTree& tree, std::string_view assertion, std::string& error)
{
    nlohmann::json expected;
    try {
        expected = nlohmann::json::parse(assertion);
    }
    catch (const std::exception& e) {
        error = std::format("invalid assertion JSON: {}", e.what());
        return false;
    }

    const nlohmann::json treeDump = dumpWidgetTree(tree);
    if (const auto widgetIt = expected.find("widget"); widgetIt != expected.end()) {
        if (!widgetIt->is_string()) {
            error = "widget assertion selector must be a string";
            return false;
        }
        const std::string widgetName = widgetIt->get<std::string>();
        const nlohmann::json* node = findWidgetNode(treeDump, widgetName);
        if (!node) {
            error = std::format("widget '{}' not found", widgetName);
            return false;
        }
        expected.erase(widgetIt);
        return jsonContains(*node, expected, std::format("widget[{}]", widgetName), error);
    }
    return jsonContains(treeDump, expected, "tree", error);
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

/// Maps SDL events to Core Events. Pointer press/release/scroll carry no
/// position in the Core event structs; the host tracks the current pointer
/// position from MouseMoveEvent and uses it for those.
struct SdlEventSource final : IAppEventSource
{
    uint32_t hostWindowID = 0;
    bool     bPointerKnown = false;

    static bool isMouseFocusedHostWindow(uint32_t hostWindowID)
    {
        SDL_Window* focusedWindow = SDL_GetMouseFocus();
        return focusedWindow != nullptr && hostWindowID != 0 && SDL_GetWindowID(focusedWindow) == hostWindowID;
    }

    void pollEvents(const std::function<void(const Event&)>& emit) override
    {
        SDL_PumpEvents();
        if (!bPointerKnown && isMouseFocusedHostWindow(hostWindowID)) {
            float mouseX = -1.0f;
            float mouseY = -1.0f;
            SDL_GetMouseState(&mouseX, &mouseY);
            emit(MouseMoveEvent(mouseX, mouseY));
            bPointerKnown = true;
        }

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
                case SDL_EVENT_WINDOW_MOUSE_ENTER:
                case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                    return hostWindowID == 0 || event.window.windowID == hostWindowID;
                default:
                    return true;
                }
            }();

            const bool bHostPointerEvent = [&]() {
                switch (event.type) {
                case SDL_EVENT_MOUSE_MOTION:
                    return hostWindowID == 0 || event.motion.windowID == hostWindowID;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    return hostWindowID == 0 || event.button.windowID == hostWindowID;
                case SDL_EVENT_MOUSE_WHEEL:
                    return hostWindowID == 0 || event.wheel.windowID == hostWindowID;
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
            case SDL_EVENT_WINDOW_MOUSE_ENTER:
                if (bHostWindowEvent) {
                    float mouseX = -1.0f;
                    float mouseY = -1.0f;
                    SDL_GetMouseState(&mouseX, &mouseY);
                    emit(MouseMoveEvent(mouseX, mouseY));
                    bPointerKnown = true;
                }
                break;
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                if (bHostWindowEvent) {
                    emit(MouseMoveEvent(-1000000.0f, -1000000.0f));
                    bPointerKnown = false;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (bHostPointerEvent) {
                    emit(MouseMoveEvent(event.motion.x, event.motion.y));
                    bPointerKnown = true;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (bHostPointerEvent) {
                    emit(MouseMoveEvent(event.button.x, event.button.y));
                    emit(MouseButtonPressedEvent(event.button.button));
                    bPointerKnown = true;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (bHostPointerEvent) {
                    emit(MouseMoveEvent(event.button.x, event.button.y));
                    emit(MouseButtonReleasedEvent(event.button.button));
                    bPointerKnown = true;
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                if (bHostPointerEvent) {
                    emit(MouseMoveEvent(event.wheel.mouse_x, event.wheel.mouse_y));
                    emit(MouseScrolledEvent(event.wheel.x, event.wheel.y));
                    bPointerKnown = true;
                }
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

/// Drives a JSONL scenario as an event source. Frame steps return so the
/// caller renders that frame; checkpoint and resize stay at the host layer,
/// while pointer/key/drag steps emit the same Core events as SDL.
struct ScenarioEventSource final : IAppEventSource
{
    std::vector<GuiScenarioStep> steps;
    size_t index = 0;
    uint32_t remainingFrames = 0;
    bool bPendingDoneAfterLastFrame = false;
    bool bDone = false;
    std::function<void(const std::string&)> onCheckpoint;
    std::function<bool(std::string_view)> onAssert;
    std::function<void(uint32_t, uint32_t)> onSetWindowSize;
    std::function<void()> onCaptureFinal;
    std::function<void()> onDone;

    void finishLastFrameIfNeeded()
    {
        if (remainingFrames == 0 && index == steps.size()) {
            if (onCaptureFinal) {
                onCaptureFinal();
            }
            bPendingDoneAfterLastFrame = true;
        }
    }

    void pollEvents(const std::function<void(const Event&)>& emit) override
    {
        if (bDone) {
            return;
        }
        if (bPendingDoneAfterLastFrame) {
            bPendingDoneAfterLastFrame = false;
            bDone                      = true;
            if (onDone) {
                onDone();
            }
            return;
        }
        if (remainingFrames > 0) {
            --remainingFrames;
            finishLastFrameIfNeeded();
            return;
        }
        while (index < steps.size()) {
            const GuiScenarioStep& step = steps[index++];
            switch (step.kind) {
            case EGuiScenarioStepKind::Frame:
                remainingFrames = std::max(step.frame, 1u) - 1;
                finishLastFrameIfNeeded();
                return;
            case EGuiScenarioStepKind::SetWindowSize:
                if (onSetWindowSize) {
                    onSetWindowSize(step.width, step.height);
                }
                break;
            case EGuiScenarioStepKind::Checkpoint:
                if (onCheckpoint) {
                    onCheckpoint(step.tag);
                }
                break;
            case EGuiScenarioStepKind::Assert:
                if (onAssert && !onAssert(step.assertion)) {
                    bDone = true;
                    if (onDone) {
                        onDone();
                    }
                    return;
                }
                break;
            case EGuiScenarioStepKind::MouseMove:
            case EGuiScenarioStepKind::MousePress:
            case EGuiScenarioStepKind::MouseRelease:
            case EGuiScenarioStepKind::MouseWheel:
            case EGuiScenarioStepKind::KeyPress:
            case EGuiScenarioStepKind::KeyRelease:
            case EGuiScenarioStepKind::KeyTyped:
            case EGuiScenarioStepKind::Drag: {
                struct ScenarioSink final : IGuiEventSink
                {
                    const std::function<void(const Event&)>& emitFn;

                    explicit ScenarioSink(const std::function<void(const Event&)>& inEmitFn)
                        : emitFn(inEmitFn)
                    {
                    }

                    void dispatch(const Event& event, const glm::vec2& /*logicalPoint*/) override
                    {
                        emitFn(event);
                    }
                } sink{emit};
                emitGuiScenarioStep(sink, step);
                break;
            }
            }
        }
        bDone = true;
        if (onDone) {
            onDone();
        }
    }
};

} // namespace

struct GUIWindowHost::FImpl
{
    const FGUIWindowHostConfig* config = nullptr;
    IGUIAppDelegate*         delegate = nullptr;

    SDLNativeWindow          window;
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
    std::shared_ptr<GUIRenderSurface> offscreenSurface;
    std::shared_ptr<IBuffer>           offscreenShotBuffer;

    std::unique_ptr<IAppEventSource> eventSource;
    std::string captureRequestPath;
    bool    bLoggedFirstSnapshot = false;
    bool    bQuitRequested       = false;
    bool    bScenarioMode        = false;
    bool    bScenarioFailed      = false;

    // Mouse cursor state (system cursors created lazily in init()).
    ECursorType activeCursor       = ECursorType::Arrow;
    SDL_Cursor* sdlArrowCursor     = nullptr;
    SDL_Cursor* sdlResizeEWCursor  = nullptr;
    SDL_Cursor* sdlResizeNSCursor  = nullptr;
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

    // 1. Window provider (SDL3 + Vulkan surface).
    SDLNativeWindow& window = _impl->window;
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
    // System cursors for hover feedback (split dividers request resize cursors).
    _impl->sdlArrowCursor    = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    _impl->sdlResizeEWCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
    _impl->sdlResizeNSCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);

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
            .imageFormat = EFormat::R8G8B8A8_UNORM,
            // GUI hover is high-frequency interaction: prefer Mailbox (low
            // latency, no tearing) over the default FIFO present queue. The
            // backend falls back to FIFO when the driver lacks Mailbox.
            .presentMode        = EPresentMode::Mailbox,
            .bVsync             = config.bVsync,
            .minImageCount      = 3,
            .bEnableTransferSrc = true,
            .width              = config.width != 0 ? config.width : DEFAULT_WINDOW_WIDTH,
            .height             = config.height != 0 ? config.height : DEFAULT_WINDOW_HEIGHT,
        },
        .nativeWindow   = &window,
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
    if (!FontManager::get()->loadFont(*render, config.fontPath, DEFAULT_RUNTIME_FONT_NAME, DEFAULT_RUNTIME_FONT_SIZE)) {
        YA_CORE_WARN("GUIAppHost: failed to load runtime font '{}'; text drawing disabled", config.fontPath);
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
        scenario->onCheckpoint = [this](const std::string& tag) { dumpScenarioCheckpoint(tag); };
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

void GUIWindowHost::dispatchToTree(const Event& event, float mouseX, float mouseY)
{
    WidgetEventContext ctx;
    ctx.logicalPoint = {mouseX, mouseY};
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
    if (cursor == _impl->activeCursor) {
        return;
    }
    _impl->activeCursor = cursor;

    SDL_Cursor* sdlCursor = _impl->sdlArrowCursor;
    switch (cursor) {
    case ECursorType::Arrow:
        sdlCursor = _impl->sdlArrowCursor;
        break;
    case ECursorType::ResizeEastWest:
        sdlCursor = _impl->sdlResizeEWCursor;
        break;
    case ECursorType::ResizeNorthSouth:
        sdlCursor = _impl->sdlResizeNSCursor;
        break;
    }
    if (sdlCursor) {
        SDL_SetCursor(sdlCursor);
    }
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

void GUIWindowHost::rebuildPresentationResources(bool bWaitForGpu)
{
    // Frame boundary only: wait for in-flight work, then release command
    // buffers (and their retained resources) and the imported images/views
    // before rebuilding from the current swapchain.
    if (bWaitForGpu) {
        _impl->render->waitIdle();
    }
    _impl->commandBuffers.clear();
    _impl->presentationTargets.clear();
    _impl->offscreenSurface.reset();
    _impl->offscreenShotBuffer.reset();

    auto* swapchain = _impl->render->getSwapchain()->as<VulkanSwapChain>();
    _impl->render->allocateCommandBuffers(_impl->render->getSwapchainImageCount(), _impl->commandBuffers);
    GUIPresentationTarget::buildAll(*_impl->render, *swapchain, "GUIApp", _impl->presentationTargets);
    _impl->cachedSwapchainHandle = swapchain->getHandle();
    _impl->cachedSwapchainExtent = swapchain->getExtent();
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

bool GUIWindowHost::shouldClose() const
{
    return _impl->bQuitRequested || _impl->delegate->shouldRequestClose();
}

void GUIWindowHost::onTick(float /*dt*/)
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
            if (!requestWindowSize(static_cast<uint32_t>(width), static_cast<uint32_t>(height), "automation")) {
                _impl->automationServer.completeRequest(
                    request,
                    makeAutomationError(*request, std::format("failed to set window size to {}x{}", width, height)));
                continue;
            }
            _impl->automationServer.completeRequest(
                request,
                makeAutomationSuccess(*request, {{"width", width}, {"height", height}}));
            continue;
        }
        // Pointer injection drives hover/click regressions deterministically:
        // the same core events SDL emits, but scheduled from the control
        // protocol so a test harness can assert on the hover owner afterward.
        if (request->method == "mouse_move") {
            const auto xIt = request->params.find("x");
            const auto yIt = request->params.find("y");
            if (xIt == request->params.end() || yIt == request->params.end() ||
                !xIt->is_number() || !yIt->is_number()) {
                _impl->automationServer.completeRequest(
                    request,
                    makeAutomationError(*request, "mouse_move requires number params {x,y}"));
                continue;
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
            continue;
        }
        if (request->method == "mouse_press") {
            const auto buttonIt = request->params.find("button");
            const int  button   = (buttonIt != request->params.end() && buttonIt->is_number_integer())
                                      ? buttonIt->get<int>()
                                      : 1; // SDL_BUTTON_LEFT
            dispatchToTree(MouseButtonPressedEvent(button), _impl->lastMouseX, _impl->lastMouseY);
            _impl->automationServer.completeRequest(request, makeAutomationSuccess(*request));
            continue;
        }
        if (request->method == "mouse_release") {
            const auto buttonIt = request->params.find("button");
            const int  button   = (buttonIt != request->params.end() && buttonIt->is_number_integer())
                                      ? buttonIt->get<int>()
                                      : 1; // SDL_BUTTON_LEFT
            dispatchToTree(MouseButtonReleasedEvent(button), _impl->lastMouseX, _impl->lastMouseY);
            _impl->automationServer.completeRequest(request, makeAutomationSuccess(*request));
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

    _impl->delegate->updateUI();
    const auto& presentation = _impl->presentationTargets[static_cast<size_t>(imageIndex)];
    if (!presentation || !presentation->renderSurface || !presentation->renderSurface->isValid()) {
        YA_CORE_ERROR("GUIAppHost: presentation surface {} is invalid", imageIndex);
        _impl->render->waitIdle();
        return;
    }
    const auto& renderSurface = presentation->renderSurface;
    const auto& renderImage   = renderSurface->getRenderImage();
    const Extent2D presentExtent = renderImage->getExtent();
    const Extent2D logicalExtent = _impl->tree->getLogicalExtent();
    renderSurface->prepare(FRender2DComposePassDesc{
        .kind = ERender2DComposePassKind::RuntimeUIComposite,
    });
    UIFrameSnapshot snapshot = _impl->tree->buildSnapshot(UIFrameBuildContext{
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

    auto cmdBuf = _impl->commandBuffers[static_cast<size_t>(imageIndex)];
    cmdBuf->reset();
    cmdBuf->begin();

    cmdBuf->retainResource(renderImage->getImageShared());
    cmdBuf->retainResource(renderImage->getImageViewShared());
    cmdBuf->transitionImageLayoutAuto(renderImage->getImage(), EImageLayout::ColorAttachmentOptimal);
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
                    .image         = renderImage->getImage(),
                    .imageView     = renderImage->getImageView(),
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

    renderSurface->record(
        cmdBuf.get(),
        /*depthTarget=*/nullptr,
        &snapshot,
        FRender2DComposePassDesc{
            .kind                  = ERender2DComposePassKind::RuntimeUIComposite,
            .logicalViewportExtent = _impl->tree->getLogicalExtent(),
        });

    const bool bCaptureOffscreen =
        _impl->config->offscreenShotFrame != 0 &&
        _impl->frameCount == _impl->config->offscreenShotFrame &&
        !_impl->config->offscreenShotPath.empty();
    std::shared_ptr<RenderImage> offscreenImage;
    if (bCaptureOffscreen) {
        if (!_impl->offscreenSurface ||
            !_impl->offscreenSurface->isValid() ||
            _impl->offscreenSurface->getRenderImage()->getExtent() != presentExtent ||
            _impl->offscreenSurface->getRenderImage()->getFormat() != renderImage->getFormat()) {
            _impl->offscreenSurface = GUIRenderSurface::createOffscreen(
                *_impl->render->getResourceFactory(),
                FGUIRenderSurfaceDesc{
                    .label       = "GUIAppHost_OffscreenMirror",
                    .extent      = presentExtent,
                    .colorFormat = renderImage->getFormat(),
                });
        }
        if (!_impl->offscreenSurface || !_impl->offscreenSurface->isValid()) {
            YA_CORE_ERROR("GUIAppHost: unable to create offscreen parity surface");
        }
        else {
            _impl->offscreenSurface->prepare(FRender2DComposePassDesc{
                .kind = ERender2DComposePassKind::RuntimeUIOffscreen,
            });
            _impl->offscreenSurface->record(
                cmdBuf.get(),
                nullptr,
                &snapshot,
                FRender2DComposePassDesc{
                    .kind                  = ERender2DComposePassKind::RuntimeUIOffscreen,
                    .logicalViewportExtent = _impl->tree->getLogicalExtent(),
                });
            offscreenImage = _impl->offscreenSurface->getRenderImage();

            const uint32_t requiredReadbackSize = presentExtent.width * presentExtent.height * 4;
            if (!_impl->offscreenShotBuffer || _impl->offscreenShotBuffer->getSize() != requiredReadbackSize) {
                _impl->offscreenShotBuffer = _impl->render->getResourceFactory()->createBuffer(
                    ya::BufferCreateInfo{
                        .label       = "GUIAppHost_OffscreenShot",
                        .usage       = EBufferUsage::TransferDst,
                        .size        = requiredReadbackSize,
                        .memoryUsage = EMemoryUsage::GpuToCpu,
                    });
            }
            cmdBuf->transitionImageLayoutAuto(offscreenImage->getImage(), EImageLayout::TransferSrc);
            cmdBuf->copyImageToBuffer(
                offscreenImage->getImage(),
                EImageLayout::TransferSrc,
                _impl->offscreenShotBuffer.get(),
                {ya::BufferImageCopy{
                    .imageSubresource  = {.aspectMask = 1, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
                    .imageOffsetX      = 0,
                    .imageOffsetY      = 0,
                    .imageOffsetZ      = 0,
                    .imageExtentWidth  = presentExtent.width,
                    .imageExtentHeight = presentExtent.height,
                    .imageExtentDepth  = 1,
                }});
            cmdBuf->transitionImageLayoutAuto(offscreenImage->getImage(),
                                               _impl->offscreenSurface->getFinalLayout());
        }
    }

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
        const uint32_t requiredReadbackSize = presentExtent.width * presentExtent.height * 4;
        if (!_impl->gpuShotBuffer || _impl->gpuShotBuffer->getSize() != requiredReadbackSize) {
            _impl->gpuShotBuffer = _impl->render->getResourceFactory()->createBuffer(
                ya::BufferCreateInfo{
                    .label       = "GUIAppHost_GpuShot",
                    .usage       = EBufferUsage::TransferDst,
                    .size        = requiredReadbackSize,
                    .memoryUsage = EMemoryUsage::GpuToCpu,
                });
        }
        cmdBuf->transitionImageLayoutAuto(renderImage->getImage(), EImageLayout::TransferSrc);
        cmdBuf->copyImageToBuffer(
            renderImage->getImage(),
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
        cmdBuf->transitionImageLayoutAuto(renderImage->getImage(), renderSurface->getFinalLayout());
    }

    cmdBuf->end();
    _impl->render->end(imageIndex, {cmdBuf->getHandle()});

    if (!capturePath.empty() || bCaptureOffscreen) {
        _impl->render->waitIdle();
        if (!capturePath.empty() && _impl->gpuShotBuffer) {
            if (uint8_t* pixels = _impl->gpuShotBuffer->map<uint8_t>()) {
                writeRGBAtoBMP(pixels,
                               presentExtent.width,
                               presentExtent.height,
                               swapchain->getFormat() == EFormat::B8G8R8A8_UNORM,
                               capturePath);
                _impl->gpuShotBuffer->unmap();
                YA_CORE_INFO("GUIAppHost wrote GPU shot to '{}' ({}x{})",
                             capturePath,
                             presentExtent.width,
                             presentExtent.height);
            }
        }
        if (bCaptureOffscreen && _impl->offscreenShotBuffer) {
            if (uint8_t* pixels = _impl->offscreenShotBuffer->map<uint8_t>()) {
                writeRGBAtoBMP(pixels,
                               presentExtent.width,
                               presentExtent.height,
                               false,
                               _impl->config->offscreenShotPath);
                _impl->offscreenShotBuffer->unmap();
                YA_CORE_INFO("GUIAppHost wrote offscreen shot to '{}' ({}x{})",
                             _impl->config->offscreenShotPath,
                             presentExtent.width,
                             presentExtent.height);
            }
        }
    }
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

    // Clean shutdown, reverse order. Every member owning GPU resources must be
    // released BEFORE the Vulkan device / VMA allocator is destroyed below
    // (a later ~VulkanBuffer would call vmaDestroyBuffer on a dead allocator).
    _impl->render->waitIdle();
    _impl->automationServer.shutdown();
    Render2D::destroy();
    _impl->commandBuffers.clear();   // releases command-buffer resource retention
    _impl->presentationTargets.clear();
    _impl->gpuShotBuffer.reset();    // readback staging buffer (RHI-owned)
    _impl->offscreenSurface.reset();
    _impl->offscreenShotBuffer.reset();
    _impl->shaderStorage.reset();
    _impl->tree.reset();             // widgets hold snapshot/resolver refs only, but stay ordered
    FontManager::get()->clearCache();
    TextureLibrary::get().shutdown();
    DeferredDeletionQueue::get().flushAll();
    _impl->render->destroy();
    delete _impl->render;
    _impl->render = nullptr;
    SDL_StopTextInput(static_cast<SDL_Window*>(_impl->window.getNativeWindowHandle()));
    SDL_DestroyCursor(_impl->sdlArrowCursor);
    SDL_DestroyCursor(_impl->sdlResizeEWCursor);
    SDL_DestroyCursor(_impl->sdlResizeNSCursor);
    _impl->sdlArrowCursor     = nullptr;
    _impl->sdlResizeEWCursor  = nullptr;
    _impl->sdlResizeNSCursor  = nullptr;
    _impl->window.destroy();

    _impl->bInitialized = false;
}

GUIApp::GUIApp(const FGUIWindowHostConfig& config, IGUIAppDelegate& delegate)
    : _primaryWindow(config, delegate)
{
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
    AppKernel kernel({.eventSource = _primaryWindow.getEventSource()}, _primaryWindow);
    return _primaryWindow.finishRun(kernel.run(_primaryWindow.getConfig().automation));
}

void GUIApp::shutdown()
{
    _primaryWindow.shutdown();
}

} // namespace ya
