#include "GameEditor/UI/Viewport/EditorGameUIPreview.h"

#include "Core/Log.h"
#include "GameRuntime/GUI/GameUI/GameUIHost.h"
#include "GUI/Widgets/UIDocumentStore.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Scene/Core/Scene.h"
#include "Scene/Core/SceneWidgetEntry.h"

namespace ya
{

EditorGameUIPreview::EditorGameUIPreview() = default;
EditorGameUIPreview::~EditorGameUIPreview() = default;

void EditorGameUIPreview::shutdown()
{
    detachMountedTree();
    _mountedScene = nullptr;
    _mountedExtent = {};
    _mountSignature.clear();
    _reportedErrors.clear();
}

void EditorGameUIPreview::detachMountedTree()
{
    _tree.reset();
}

std::string EditorGameUIPreview::computeMountSignature(const Scene& scene, UIDocumentStore* documents) const
{
    // Everything the mounted tree is built from, and nothing else. An entry with
    // no document mounts nothing, so it is not an input to this tree (and asking
    // the entry to serialize itself would log about it every frame).
    std::string signature;
    for (const SceneWidgetEntry& entry : scene.getWidgetEntries()) {
        if (!entry.autoMount || entry.documentPath.empty()) {
            continue;
        }
        signature += entry.toJson().dump();
        signature += '|';
        // The document's revision, not its identity: an edit that rewrites the
        // live document in place must still invalidate the preview.
        signature += std::to_string(documents ? documents->revision(entry.documentPath) : 0);
        signature += '\n';
    }
    return signature;
}

UIFrameSnapshot EditorGameUIPreview::buildSnapshot(Scene&           scene,
                                                   UIDocumentStore* documents,
                                                   const Extent2D&  logicalExtent,
                                                   const glm::vec2& uiScale,
                                                   const glm::vec2& offset)
{
    const std::string signature = computeMountSignature(scene, documents);
    const bool bRebuild = !_tree || _mountedScene != &scene ||
                          _mountedExtent != logicalExtent || _mountSignature != signature;
    if (bRebuild) {
        // Rebuild from scratch rather than diffing: the inputs that changed are
        // a mount list or a whole document, and a stale subtree kept alive
        // across a document replacement is exactly the mis-attribution this
        // type avoids.
        detachMountedTree();
        _tree = std::make_unique<WidgetTree>(logicalExtent);
        _tree->setTextureSource(&gameUITextureSource());
        _mountedScene    = &scene;
        _mountedExtent   = logicalExtent;
        _mountSignature  = signature;
        ++_rebuildCount;

        std::string errors;
        (void)mountSceneAutoMountEntries(scene, *_tree, documents,
                                        [&errors](std::string_view message) {
                                            errors.append(message);
                                            errors.push_back('\n');
                                        });
        if (errors != _reportedErrors) {
            _reportedErrors = std::move(errors);
            if (!_reportedErrors.empty()) {
                YA_CORE_WARN("Editor scene UI preview mount errors:\n{}", _reportedErrors);
            }
        }
    }

    UIFrameBuildContext ctx;
    ctx.uiScale         = uiScale;
    ctx.offset          = offset;
    ctx.textureResolver = &resolveGameUITexture;
    return _tree->buildSnapshot(ctx);
}

} // namespace ya
