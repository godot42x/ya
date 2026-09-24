#pragma once

#include "GameEditor/UI/Dock/EditorNativeTearOff.h"
#include "GameEditor/UI/Shell/EditorRootSession.h"

#include <cstddef>
#include <nlohmann/json.hpp>

namespace ya
{

struct EditorWindowRegistry;
struct INativeWindow;
class IGUIWindowCoordinator;

enum class EEditorWindowRole : uint8_t
{
    Main,
    TornOff,
};

/// Versioned editor.dockLayout envelope (v5): OS-window topology in `windows[]`,
/// each record embedding v2 dock documents (`tree` node structure + `dockSpace`
/// stack data). Dock overlay `floating[]` stays tree-local (MW-707). Extra OS
/// windows restore only through `IGUIWindowCoordinator`; GameEditor never
/// creates SDL windows.
inline constexpr int kEditorWindowLayoutVersion = 5;
[[nodiscard]] nlohmann::json exportEditorWindowLayout(const EditorWindowRegistry& windows,
                                                      INativeWindow*              mainNative,
                                                      IGUIWindowCoordinator*      coordinator);

void persistEditorWindowLayout(const EditorWindowRegistry& windows,
                               INativeWindow*              mainNative,
                               IGUIWindowCoordinator*      coordinator);

[[nodiscard]] const nlohmann::json* findMainEditorWindowRecord(const nlohmann::json& document);

struct FEditorWindowRestoreStats
{
    size_t restored               = 0;
    size_t skippedClosing         = 0;
    size_t skippedMissingDocument = 0;
    size_t skippedMissingOwner    = 0;
    size_t skippedEmpty           = 0;
    size_t relocatedMonitor       = 0;
};

/// Restore torn-off OS windows from a v5 envelope. Main window is left to
/// `EditorDockWorkspace::applyWorkspaceLayout`. Missing owner/document skips
/// that extra (does not rebind to another root). `closing: true` extras are
/// not revived. Product present/input of extras is still the caller's job.
[[nodiscard]] size_t restoreEditorExtraWindows(FEditorNativeTearOff&      env,
                                               const nlohmann::json&      document,
                                               FEditorWindowRestoreStats* stats = nullptr);

/// Relocate a persisted window record onto a live display. Size always;
/// missing monitors do not keep overlay-like origins.
bool recoverEditorWindowPlacement(INativeWindow& native, const nlohmann::json& windowRecord);

} // namespace ya
