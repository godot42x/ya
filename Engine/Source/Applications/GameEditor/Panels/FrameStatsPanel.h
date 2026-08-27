#pragma once

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Compose/GUIRenderSurface.h"

#include <memory>

namespace ya
{

struct EditorLayer;
struct App;
struct IRender;
struct ICommandBuffer;
struct RenderTexture;
struct UIText;

/// Frame Stats panel rendered with the YA_GUI framework instead of ImGui
/// immediate-mode calls. The panel owns a small retained widget tree (four
/// text rows) composed into an offscreen surface; the resulting texture is
/// bridged back into the ImGui dockspace shell via Image(), exactly like
/// GUIWorkbenchPanel. This is the first editor panel migrated off ImGui.
struct FrameStatsPanel
{
  private:
    EditorLayer*                     _owner = nullptr;
    std::unique_ptr<WidgetTree>      _tree;
    std::shared_ptr<GUIRenderSurface> _surface;
    std::shared_ptr<RenderTexture>   _displayImage;
    Extent2D                         _logicalExtent{};

    // Retained text rows, kept alive across frames.
    std::shared_ptr<UIText>          _frameIndexText;
    std::shared_ptr<UIText>          _deltaText;
    std::shared_ptr<UIText>          _fpsText;
    std::shared_ptr<UIText>          _avgFpsText;

    static constexpr size_t kHistorySize = 120;
    std::array<float, kHistorySize> _fpsHistory{};
    size_t                           _historyHead = 0;
    size_t                           _historyFill = 0;

  public:
    explicit FrameStatsPanel(EditorLayer* owner);

    /// Render the ImGui window shell and bridge the composed YA_GUI texture in.
    void onImGuiRender(const App& app, float dt);
    /// Compose the retained tree into the offscreen surface. Call from the
    /// editor module's presentation pass, after onImGuiRender() has sized it.
    void compose(IRender& render, ICommandBuffer& commandBuffer);
    [[nodiscard]] bool hasRenderableExtent() const { return _logicalExtent.width > 0 && _logicalExtent.height > 0; }
    [[nodiscard]] Extent2D getLogicalExtent() const { return _logicalExtent; }
    [[nodiscard]] UIFrameSnapshot buildSnapshot();

  private:
    void ensureTree();
    void updateStats(const App& app, float dt);
    void ensureTarget(IRender& render, const Extent2D& extent);
};

} // namespace ya
