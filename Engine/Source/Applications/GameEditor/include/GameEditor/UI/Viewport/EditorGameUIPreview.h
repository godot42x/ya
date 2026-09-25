#pragma once

// ============================================================================
// EditorGameUIPreview - the current Scene's Game UI mounts, instantiated once
// into a persistent preview tree.
//
// Three trees can exist for the same `.yaui` document, and they must not be
// shared because their state is different:
//
//   EditorUIDesignerSession   one DOCUMENT being edited: selection, drag,
//                             resize, dirty. Authoring only.
//   EditorGameUIPreview       the current SCENE's mounts (this file): layout,
//                             paint and geometry, reused frame to frame.
//   GameUIHost                the RUNTIME instance: input, focus, capture,
//                             tick, animation, game state.
//
// This host is deliberately in Authoring mode: it lays out and paints, and it
// neither ticks nor dispatches input. A preview that ran behaviours or click
// handlers would be a second runtime, and the editor would be showing state the
// game does not have (or worse, game state the editor invented).
//
// Rebuild triggers are the mount inputs, not the frame: the scene, the mount
// list, each mounted document's revision, or the preview extent. Rebuilding
// every compose is what this type exists to avoid -- it discards preview state
// and re-instantiates every document each frame.
// ============================================================================

#include "Core/Common/Types.h"

#include "GUI/Widgets/UIFrameSnapshot.h"

#include <memory>
#include <string>

#include <glm/glm.hpp>

namespace ya
{

struct Scene;
struct UIDocumentStore;
struct WidgetTree;

struct EditorGameUIPreview
{
    EditorGameUIPreview();
    ~EditorGameUIPreview();

    EditorGameUIPreview(const EditorGameUIPreview&)            = delete;
    EditorGameUIPreview& operator=(const EditorGameUIPreview&) = delete;

    void shutdown();

    /// Layout + paint the current Scene's auto-mount entries as this frame's
    /// immutable packet. The tree is instantiated only when a mount input
    /// changed; otherwise the existing instances are reused.
    [[nodiscard]] UIFrameSnapshot buildSnapshot(Scene&                    scene,
                                                UIDocumentStore*          documents,
                                                const Extent2D&           logicalExtent,
                                                const glm::vec2&          uiScale,
                                                const glm::vec2&          offset);

    /// Whether a tree currently exists (diagnostics / tests).
    [[nodiscard]] bool hasPreview() const { return _tree != nullptr; }
    /// How many times the preview tree was instantiated. A stable scene must
    /// not grow this across frames; tests read it instead of guessing.
    [[nodiscard]] uint64_t rebuildCount() const { return _rebuildCount; }

  private:
    /// Identity of everything the mounted tree depends on. Equal signature +
    /// equal extent means the existing tree is still the right tree.
    [[nodiscard]] std::string computeMountSignature(const Scene& scene, UIDocumentStore* documents) const;
    void detachMountedTree();

    std::unique_ptr<WidgetTree> _tree;
    Scene*                      _mountedScene  = nullptr;
    Extent2D                    _mountedExtent{};
    std::string                 _mountSignature;
    /// Last reported mount diagnostic, so a broken reference logs once instead
    /// of once per frame.
    std::string                 _reportedErrors;
    uint64_t                    _rebuildCount = 0;
};

} // namespace ya
