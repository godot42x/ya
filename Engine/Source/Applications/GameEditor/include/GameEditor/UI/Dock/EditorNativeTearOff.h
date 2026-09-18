#pragma once

#include "GUI/Host/GUIWindowSession.h"
#include "GUI/Host/GUIWindowChrome.h"
#include "GUI/Widgets/Controls/DockSpace/DockNode.h"
#include "GameEditor/UI/Shell/EditorRootSession.h"
#include "GameEditor/UI/Shell/EditorTabSpawnerRegistry.h"
#include "GameEditor/UI/Shell/EditorWindowRegistry.h"

#include <cstdint>
#include <memory>
#include <glm/glm.hpp>
#include <string_view>

namespace ya
{

struct FDockContext;
struct IRender;
struct INativeWindow;
struct EditorDocumentRegistry;
struct WidgetTree;

/// Inputs for native-window tear-off. GameEditor never listens to SDL or
/// creates OS windows; it calls the coordinator, the Host placement adapter,
/// and dock transfer.
struct FEditorNativeTearOff
{
    IGUIWindowCoordinator*     coordinator = nullptr;
    IGUIAppDelegate*           content     = nullptr;
    EditorWindowRegistry*      windows     = nullptr;
    EditorTabSpawnerRegistry*  spawners    = nullptr;
    EditorDocumentRegistry*    documents   = nullptr;
    IRender*                   render      = nullptr;
};

struct FEditorTearOffResult
{
    EditorWindowId editorWindowId = kInvalidEditorWindowId;
    GUIWindowId    guiWindowId    = 0;
    DockPanelId    targetPanelId  = kInvalidDockPanelId;
};

struct FEditorTearOffGeometry
{
    glm::vec2 pos{8.0f, 8.0f};
    glm::vec2 size{320.0f, 240.0f};
    bool      bScreenSpace = false;
};

enum class EEditorWindowCloseResult : uint8_t
{
    Reclaimed,
    RejectedLocked,
    Failed,
};

struct FEditorRedockResult
{
    DockPanelId targetPanelId         = kInvalidDockPanelId;
    bool        bReclaimedSourceWindow = false;
};

[[nodiscard]] FEditorTabDragPayload makeEditorTabDragPayload(const FEditorTabSpawner& spawner,
                                                             std::string_view documentKey = {},
                                                             EditorWindowId sourceWindowId = kInvalidEditorWindowId,
                                                             EditorRootId ownerOverride = kInvalidEditorRootId);

/// Tear a detachable root editor / owned tool / window tool into a new native
/// window. Retains opaque owner/document identity. Never dual-mounts the live
/// widget. Locked tabs return an empty result and leave the source unchanged.
[[nodiscard]] FEditorTearOffResult tearOffEditorPanelToNativeWindow(
    FEditorNativeTearOff& env,
    EditorWindowSession& sourceSession,
    FDockContext& sourceDock,
    DockPanelId panelId,
    const FEditorTearOffGeometry& geometry = {});

/// DockSpace NoTarget entry. Returns true if overlay tear-off must be skipped
/// (native window created, or Locked/Level rejected). Returns false when the
/// coordinator is missing so DockSpace can fall back to InProcessOverlay.
[[nodiscard]] bool handleDockNoTargetTearOff(FEditorNativeTearOff& env,
                                             EditorWindowSession& sourceSession,
                                             FDockContext& sourceDock,
                                             DockPanelId panelId,
                                             const glm::vec2& treeLocalPos,
                                             const glm::vec2& size,
                                             INativeWindow* sourceNative = nullptr,
                                             FEditorTearOffResult* out = nullptr);

/// Move a torn panel back to its owner dock. Owned tools only return to that
/// owner's nested context. Reclaims an empty extra native window. Locked tabs
/// and the default window are left unchanged.
[[nodiscard]] FEditorRedockResult redockEditorPanelToOwner(FEditorNativeTearOff& env,
                                                           EditorWindowSession& sourceSession,
                                                           FDockContext& sourceDock,
                                                           DockPanelId panelId);

/// Close an extra editor window: redock remaining panels home, then destroy
/// the coordinator session. The default / Level window is RejectedLocked.
[[nodiscard]] EEditorWindowCloseResult closeEditorWindow(FEditorNativeTearOff& env,
                                                         EditorWindowId editorWindowId);

[[nodiscard]] bool reclaimEditorWindowIfEmpty(FEditorNativeTearOff& env, EditorWindowId editorWindowId);

/// Extra OS windows host a dock on the coordinator tree with the same
/// client-drawn title tab bar as the main editor. Hybrid traffic lights
/// occupy the title row only — never apply `contentInsets.left` to the dock body.
void hostEditorDockOnTree(WidgetTree& tree,
                          const std::shared_ptr<FDockContext>& dock,
                          const FWindowChromeLayout& chrome,
                          INativeWindow* native = nullptr);
void applyEditorDockFillSlots(WidgetTree& tree, const FWindowChromeLayout& chrome);

} // namespace ya
