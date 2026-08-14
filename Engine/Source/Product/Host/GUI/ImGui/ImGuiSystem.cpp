#include "Host/GUI/ImGui/ImGuiSystem.h"

#include "Core/Profiling/Instrumentor.h"

#include <cfloat>
#include <array>
#include <filesystem>
#include <span>

#include <imgui_freetype.h>

namespace ya
{

namespace
{

ImGuiKey mapKeyCodeToImGuiKey(EKey::T key)
{
    switch (key) {
    case EKey::Tab: return ImGuiKey_Tab;
    case EKey::Left: return ImGuiKey_LeftArrow;
    case EKey::Right: return ImGuiKey_RightArrow;
    case EKey::Up: return ImGuiKey_UpArrow;
    case EKey::Down: return ImGuiKey_DownArrow;
    case EKey::Pageup: return ImGuiKey_PageUp;
    case EKey::PagedowN: return ImGuiKey_PageDown;
    case EKey::Home: return ImGuiKey_Home;
    case EKey::End: return ImGuiKey_End;
    case EKey::Insert: return ImGuiKey_Insert;
    case EKey::Delete: return ImGuiKey_Delete;
    case EKey::Backspace: return ImGuiKey_Backspace;
    case EKey::Space: return ImGuiKey_Space;
    case EKey::Enter: return ImGuiKey_Enter;
    case EKey::Escape: return ImGuiKey_Escape;
    case EKey::LCtrl: return ImGuiKey_LeftCtrl;
    case EKey::LShift: return ImGuiKey_LeftShift;
    case EKey::LAlt: return ImGuiKey_LeftAlt;
    case EKey::LMeta: return ImGuiKey_LeftSuper;
    case EKey::RCtrl: return ImGuiKey_RightCtrl;
    case EKey::RShift: return ImGuiKey_RightShift;
    case EKey::RAlt: return ImGuiKey_RightAlt;
    case EKey::RMeta: return ImGuiKey_RightSuper;
    case EKey::K_0: return ImGuiKey_0;
    case EKey::K_1: return ImGuiKey_1;
    case EKey::K_2: return ImGuiKey_2;
    case EKey::K_3: return ImGuiKey_3;
    case EKey::K_4: return ImGuiKey_4;
    case EKey::K_5: return ImGuiKey_5;
    case EKey::K_6: return ImGuiKey_6;
    case EKey::K_7: return ImGuiKey_7;
    case EKey::K_8: return ImGuiKey_8;
    case EKey::K_9: return ImGuiKey_9;
    case EKey::K_A: return ImGuiKey_A;
    case EKey::K_B: return ImGuiKey_B;
    case EKey::K_C: return ImGuiKey_C;
    case EKey::K_D: return ImGuiKey_D;
    case EKey::K_E: return ImGuiKey_E;
    case EKey::K_F: return ImGuiKey_F;
    case EKey::K_G: return ImGuiKey_G;
    case EKey::K_H: return ImGuiKey_H;
    case EKey::K_I: return ImGuiKey_I;
    case EKey::K_J: return ImGuiKey_J;
    case EKey::K_K: return ImGuiKey_K;
    case EKey::K_L: return ImGuiKey_L;
    case EKey::K_M: return ImGuiKey_M;
    case EKey::K_N: return ImGuiKey_N;
    case EKey::K_O: return ImGuiKey_O;
    case EKey::K_P: return ImGuiKey_P;
    case EKey::K_Q: return ImGuiKey_Q;
    case EKey::K_R: return ImGuiKey_R;
    case EKey::K_S: return ImGuiKey_S;
    case EKey::K_T: return ImGuiKey_T;
    case EKey::K_U: return ImGuiKey_U;
    case EKey::K_V: return ImGuiKey_V;
    case EKey::K_W: return ImGuiKey_W;
    case EKey::K_X: return ImGuiKey_X;
    case EKey::K_Y: return ImGuiKey_Y;
    case EKey::K_Z: return ImGuiKey_Z;
    case EKey::F1: return ImGuiKey_F1;
    case EKey::F2: return ImGuiKey_F2;
    case EKey::F3: return ImGuiKey_F3;
    case EKey::F4: return ImGuiKey_F4;
    case EKey::F5: return ImGuiKey_F5;
    case EKey::F6: return ImGuiKey_F6;
    case EKey::F7: return ImGuiKey_F7;
    case EKey::F8: return ImGuiKey_F8;
    case EKey::F9: return ImGuiKey_F9;
    case EKey::F10: return ImGuiKey_F10;
    case EKey::F11: return ImGuiKey_F11;
    case EKey::F12: return ImGuiKey_F12;
    case EKey::K_GRAVE: return ImGuiKey_GraveAccent;
    case EKey::NONE:
    case EKey::CapsLock:
    default:
        return ImGuiKey_None;
    }
}

void submitKeyModifiers(uint32_t modifiers)
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, (modifiers & EKeyMod::Ctrl) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (modifiers & EKeyMod::Shift) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (modifiers & EKeyMod::Alt) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (modifiers & EKeyMod::LMeta) != 0 || (modifiers & EKeyMod::RMeta) != 0);
}

int mapMouseButtonToImGuiButton(int button)
{
    switch (static_cast<EMouse::T>(button)) {
    case EMouse::Left: return 0;
    case EMouse::Right: return 1;
    case EMouse::Middle: return 2;
    case EMouse::X1: return 3;
    case EMouse::X2: return 4;
    default: return -1;
    }
}

ImFont* addMergedFont(ImGuiIO*                     io,
                      std::span<const char* const> candidatePaths,
                      float                        fontSize,
                      const ImWchar*               ranges,
                      bool                         bLoadColor,
                      const char*                  label)
{
    for (const char* path : candidatePaths) {
        if (!std::filesystem::exists(path)) {
            continue;
        }

        ImFontConfig cfg;
        cfg.MergeMode   = true;
        cfg.PixelSnapH  = true;
        cfg.OversampleH = 2;
        cfg.OversampleV = 2;
        if (bLoadColor) {
            cfg.FontLoaderFlags |= ImGuiFreeTypeLoaderFlags_LoadColor;
        }

        if (ImFont* font = io->Fonts->AddFontFromFileTTF(path, fontSize, &cfg, ranges)) {
            YA_CORE_INFO("ImGui: loaded {} font '{}'", label, path);
            return font;
        }
    }

    YA_CORE_WARN("ImGui: no {} font candidate could be loaded", label);
    return nullptr;
}

void metricsHelpMarker(const char* desc)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

} // namespace

void ImGuiManager::initImGuiCore()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO* io = &ImGui::GetIO();
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();

    ImFont* mainFont = nullptr;
    {
        ImFontConfig fontConfig;
        fontConfig.OversampleH = 2;
        fontConfig.OversampleV = 2;

        mainFont = io->Fonts->AddFontFromFileTTF(
            "Engine/Content/Fonts/JetBrainsMono-Medium.ttf",
            16.0f,
            &fontConfig);
    }
    if (!mainFont) {
        YA_CORE_ERROR("Failed to load main font");
        mainFont = io->Fonts->AddFontDefault();
    }
    if (mainFont) {
        io->FontDefault = mainFont;
        ImGuiStyle& style = ImGui::GetStyle();
        style.FontSizeBase = mainFont->LegacySize;
        style._NextFrameFontSizeBase = mainFont->LegacySize;
    }

    static constexpr std::array<const char*, 6> cjkFontCandidates = {
        "Engine/Content/Fonts/NotoSansSC-Regular.otf",
        "Engine/Content/Fonts/SourceHanSansSC-Regular.otf",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/msyh.ttf",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc",
    };
    addMergedFont(io, cjkFontCandidates, 16.0f, io->Fonts->GetGlyphRangesChineseFull(), false, "CJK");

    static constexpr std::array<const char*, 1> emojiFontCandidates = {
        "Engine/Content/Fonts/seguiemj.ttf",
    };
    addMergedFont(io, emojiFontCandidates, 16.0f, io->Fonts->GetGlyphRangesDefault(), true, "emoji");

    ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());

    auto colors               = ImGui::GetStyle().Colors;
    colors[ImGuiCol_WindowBg] = ImVec4{0.1f, 0.105f, 0.11f, 1.0f};
    colors[ImGuiCol_Header] = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
    colors[ImGuiCol_HeaderHovered] = ImVec4{0.3f, 0.305f, 0.31f, 1.0f};
    colors[ImGuiCol_HeaderActive] = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
    colors[ImGuiCol_Button] = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
    colors[ImGuiCol_ButtonHovered] = ImVec4{0.3f, 0.305f, 0.31f, 1.0f};
    colors[ImGuiCol_ButtonActive] = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
    colors[ImGuiCol_FrameBg] = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
    colors[ImGuiCol_FrameBgHovered] = ImVec4{0.3f, 0.305f, 0.31f, 1.0f};
    colors[ImGuiCol_FrameBgActive] = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
    colors[ImGuiCol_Tab] = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
    colors[ImGuiCol_TabHovered] = ImVec4{0.38f, 0.3805f, 0.381f, 1.0f};
    colors[ImGuiCol_TabActive] = ImVec4{0.28f, 0.2805f, 0.281f, 1.0f};
    colors[ImGuiCol_TabUnfocused] = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
    colors[ImGuiCol_TitleBg] = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
    colors[ImGuiCol_TitleBgActive] = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
}

void ImGuiManager::init(IRender* render, IRenderPass* renderPass)
{
    YA_CORE_ASSERT(!_initialized, "ImGuiManager already initialized");

    ERenderAPI::T api = render->getAPI();

    switch (api) {
    case ERenderAPI::Vulkan: {
        auto* window = render->getNativeWindow();
        YA_CORE_ASSERT(window, "Render must provide a window for ImGui Vulkan backend");
        initVulkan(window, render, renderPass);
    } break;

    case ERenderAPI::None:
    case ERenderAPI::OpenGL:
    case ERenderAPI::DirectX12:
    case ERenderAPI::Metal:
    default:
        YA_CORE_ERROR("ImGui backend not implemented for API: {}", static_cast<int>(api));
        break;
    }
}

void ImGuiManager::shutdown()
{
    if (!_initialized) {
        return;
    }

    ImGuiHelper::ClearImageCache();
    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();

    _initialized = false;
    YA_CORE_INFO("ImGuiManager shutdown");
}

void ImGuiManager::beginFrame()
{
    YA_PROFILE_FUNCTION()
    YA_CORE_ASSERT(_initialized, "ImGuiManager not initialized");

    ImGuiHelper::BeginFrame();

    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
}

void ImGuiManager::endFrame()
{
    YA_PROFILE_FUNCTION()
    ImGui::EndFrame();
}

bool ImGuiManager::render()
{
    YA_PROFILE_FUNCTION()
    ImGui::Render();
    _drawData = ImGui::GetDrawData();

    const bool bMinimized =
        _drawData->DisplaySize.x <= 0.0f || _drawData->DisplaySize.y <= 0.0f;
    return bMinimized;
}

EventProcessState ImGuiManager::processEvent(const Event& event)
{
    submitEventToImGui(event);
    const FGuiInputClaim claim = describeInputClaim(event);
    if (claim.wantsEvent(event)) {
        return EventProcessState::Handled;
    }
    return EventProcessState::Continue;
}

FGuiInputClaim ImGuiManager::describeInputClaim(const Event& event) const
{
    const ImGuiIO& io = ImGui::GetIO();
    const bool bGizmoPassiveHover = ImGuizmo::IsOver() && !ImGuizmo::IsUsingAny();

    FGuiInputClaim claim;
    claim.pointer   = io.WantCaptureMouse && !bGizmoPassiveHover;
    claim.keyboard  = io.WantCaptureKeyboard && !bGizmoPassiveHover;
    claim.text      = io.WantTextInput && event.isInCategory(EEventCategory::Keyboard);
    claim.exclusive = claim.text || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    return claim;
}

bool ImGuiManager::isWantInput() const
{
    ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureMouse || io.WantCaptureKeyboard;
}

void ImGuiManager::submitEventToImGui(const Event& event)
{
    ImGuiIO& io = ImGui::GetIO();

    switch (event.getEventType()) {
    case EEvent::MouseMoved:
    {
        const auto& mouseEvent = static_cast<const MouseMoveEvent&>(event);
        io.AddMousePosEvent(mouseEvent.getX(), mouseEvent.getY());
    } break;
    case EEvent::MouseButtonPressed:
    {
        const auto& buttonEvent = static_cast<const MouseButtonPressedEvent&>(event);
        if (const int button = mapMouseButtonToImGuiButton(buttonEvent.GetMouseButton()); button >= 0) {
            io.AddMouseButtonEvent(button, true);
        }
    } break;
    case EEvent::MouseButtonReleased:
    {
        const auto& buttonEvent = static_cast<const MouseButtonReleasedEvent&>(event);
        if (const int button = mapMouseButtonToImGuiButton(buttonEvent.GetMouseButton()); button >= 0) {
            io.AddMouseButtonEvent(button, false);
        }
    } break;
    case EEvent::MouseScrolled:
    {
        const auto& scrollEvent = static_cast<const MouseScrolledEvent&>(event);
        io.AddMouseWheelEvent(-scrollEvent.getOffsetX(), scrollEvent.getOffsetY());
    } break;
    case EEvent::KeyPressed:
    {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        submitKeyModifiers(keyEvent._mod);
        if (const ImGuiKey key = mapKeyCodeToImGuiKey(keyEvent.getKeyCode()); key != ImGuiKey_None) {
            io.AddKeyEvent(key, true);
            io.SetKeyEventNativeData(key, static_cast<int>(keyEvent.getKeyCode()), 0, 0);
        }
    } break;
    case EEvent::KeyReleased:
    {
        const auto& keyEvent = static_cast<const KeyReleasedEvent&>(event);
        submitKeyModifiers(keyEvent._mod);
        if (const ImGuiKey key = mapKeyCodeToImGuiKey(keyEvent.getKeyCode()); key != ImGuiKey_None) {
            io.AddKeyEvent(key, false);
            io.SetKeyEventNativeData(key, static_cast<int>(keyEvent.getKeyCode()), 0, 0);
        }
    } break;
    case EEvent::KeyTyped:
    {
        const auto& typedEvent = static_cast<const KeyTypedEvent&>(event);
        if (!typedEvent.getText().empty()) {
            io.AddInputCharactersUTF8(typedEvent.getText().c_str());
        }
    } break;
    case EEvent::WindowFocus:
    {
        io.AddFocusEvent(true);
    } break;
    case EEvent::WindowFocusLost:
    {
        io.AddFocusEvent(false);
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    } break;
    default:
        break;
    }
}

ImGuiManager& ImGuiManager::get()
{
    static ImGuiManager instance;
    return instance;
}

void ImGuiManager::setGizmoRect(float x, float y, float width, float height)
{
    ImGuizmo::SetRect(x, y, width, height);
}

bool ImGuiManager::onRenderGUI()
{
    using namespace ImGui;

    auto& style    = ImGui::GetStyle();
    bool  bChanged = false;

    static bool bDarkMode = true;
    if (Checkbox("Dark Mode", &bDarkMode)) {
        if (bDarkMode) {
            ImGui::StyleColorsDark();
        }
        else {
            ImGui::StyleColorsLight();
        }
        bChanged = true;
    }

    ShowFontSelector("Fonts##Selector");
    if (DragFloat("FontSizeBase", &style.FontSizeBase, 0.20f, 5.0f, 100.0f, "%.0f")) {
        style._NextFrameFontSizeBase = style.FontSizeBase;
        bChanged = true;
    }
    SameLine(0.0f, 0.0f);
    Text(" (out %.2f)", GetFontSize());
    bChanged |= DragFloat("FontScaleMain", &style.FontScaleMain, 0.02f, 0.5f, 4.0f);
    bChanged |= DragFloat("FontScaleDpi", &style.FontScaleDpi, 0.02f, 0.5f, 4.0f);

    if (SliderFloat("FrameRounding", &style.FrameRounding, 0.0f, 12.0f, "%.0f")) {
        style.GrabRounding = style.FrameRounding;
        bChanged = true;
    }
    {
        bool border = (style.WindowBorderSize > 0.0f);
        if (Checkbox("WindowBorder", &border)) {
            style.WindowBorderSize = border ? 1.0f : 0.0f;
            bChanged = true;
        }
    }
    SameLine();
    {
        bool border = (style.FrameBorderSize > 0.0f);
        if (Checkbox("FrameBorder", &border)) {
            style.FrameBorderSize = border ? 1.0f : 0.0f;
            bChanged = true;
        }
    }
    SameLine();
    {
        bool border = (style.PopupBorderSize > 0.0f);
        if (Checkbox("PopupBorder", &border)) {
            style.PopupBorderSize = border ? 1.0f : 0.0f;
            bChanged = true;
        }
    }

    metricsHelpMarker("Adjust shared ImGui style settings.");
    return bChanged;
}

} // namespace ya
