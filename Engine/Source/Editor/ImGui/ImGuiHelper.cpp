#include "Editor/ImGui/ImGuiHelper.h"

#include "Core/Profiling/Instrumentor.h"

#include <array>
#include <filesystem>
#include <span>

#include <imgui_freetype.h>

namespace ya
{

namespace
{

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
        SDL_Window* window = render->getNativeWindow<SDL_Window*>();
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

EventProcessState ImGuiManager::processEvents(const SDL_Event& event)
{
    ImGui_ImplSDL3_ProcessEvent(&event);

    auto io = &ImGui::GetIO();

    if (io->WantCaptureMouse) {
        if (!(ImGuizmo::IsOver() && !ImGuizmo::IsUsingAny())) {
            return EventProcessState::Handled;
        }
    }
    if (io->WantCaptureKeyboard) {
        if (event.type == SDL_EVENT_KEY_DOWN ||
            event.type == SDL_EVENT_KEY_UP ||
            event.type == SDL_EVENT_TEXT_INPUT) {
            if (!(ImGuizmo::IsOver() && !ImGuizmo::IsUsingAny())) {
                return EventProcessState::Handled;
            }
        }
    }

    return EventProcessState::Continue;
}

EventProcessState ImGuiManager::processEvent(const Event& event)
{
    if (!bBlockEvents) {
        return EventProcessState::Continue;
    }
    auto io = &ImGui::GetIO();
    if (event.isInCategory(EEventCategory::Mouse) && io->WantCaptureMouse) {
        return EventProcessState::Handled;
    }
    if (event.isInCategory(EEventCategory::Keyboard) && io->WantCaptureKeyboard) {
        return EventProcessState::Handled;
    }
    return EventProcessState::Continue;
}

bool ImGuiManager::isWantInput() const
{
    ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureMouse || io.WantCaptureKeyboard;
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
